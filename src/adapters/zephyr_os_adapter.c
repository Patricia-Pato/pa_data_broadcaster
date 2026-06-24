#include "zephyr_os_adapter.h"
#include <string.h>

/* ===== Deferred Work ===== */

static void work_handler_wrapper(struct k_work *work)
{
	struct zephyr_work_state *s =
		CONTAINER_OF(work, struct zephyr_work_state, work);

	if (s->handler != NULL) {
		s->handler(s->user_data);
	}
}

static int work_init(void *ctx, pa_work_handler_t handler, void *user_data)
{
	struct zephyr_work_state *s = ctx;

	s->handler = handler;
	s->user_data = user_data;
	k_work_init(&s->work, work_handler_wrapper);

	return 0;
}

static int work_submit(void *ctx)
{
	struct zephyr_work_state *s = ctx;

	return k_work_submit(&s->work);
}

struct pa_port_work zephyr_work_create(struct zephyr_work_state *state)
{
	memset(state, 0, sizeof(*state));

	struct pa_port_work port = {
		.init = work_init,
		.submit = work_submit,
		.ctx = state,
	};

	return port;
}

/* ===== Critical Section ===== */

static uint32_t critical_enter(void *ctx)
{
	ARG_UNUSED(ctx);
	return irq_lock();
}

static void critical_exit(void *ctx, uint32_t key)
{
	ARG_UNUSED(ctx);
	irq_unlock(key);
}

struct pa_port_critical zephyr_critical_create(void)
{
	struct pa_port_critical port = {
		.enter = critical_enter,
		.exit = critical_exit,
		.ctx = NULL,
	};

	return port;
}

/* ===== Sleep ===== */

static void sleep_ms(void *ctx, uint32_t milliseconds)
{
	ARG_UNUSED(ctx);
	k_msleep(milliseconds);
}

struct pa_port_sleep zephyr_sleep_create(void)
{
	struct pa_port_sleep port = {
		.ms = sleep_ms,
		.ctx = NULL,
	};

	return port;
}
