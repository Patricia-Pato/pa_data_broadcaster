#include "zephyr_timer_adapter.h"

#include <zephyr/kernel.h>
#include <string.h>

static void timer_work_handler(struct k_work *work)
{
	struct zephyr_timer_adapter_state *s =
		CONTAINER_OF(work, struct zephyr_timer_adapter_state, work);

	if (s->cb != NULL) {
		s->cb(s->cb_user_data);
	}
}

static void timer_expiry_fn(struct k_timer *timer)
{
	struct zephyr_timer_adapter_state *s =
		CONTAINER_OF(timer, struct zephyr_timer_adapter_state, timer);

	k_work_submit(&s->work);
}

static int adapter_start_oneshot(void *ctx, uint32_t interval_ms,
				 pa_timer_cb_t cb, void *user_data)
{
	struct zephyr_timer_adapter_state *s = ctx;

	s->cb = cb;
	s->cb_user_data = user_data;

	k_timer_start(&s->timer, K_MSEC(interval_ms), K_NO_WAIT);

	return 0;
}

static int adapter_stop(void *ctx)
{
	struct zephyr_timer_adapter_state *s = ctx;

	k_timer_stop(&s->timer);

	return 0;
}

struct pa_port_timer zephyr_timer_adapter_create(
	struct zephyr_timer_adapter_state *state)
{
	memset(state, 0, sizeof(*state));
	k_timer_init(&state->timer, timer_expiry_fn, NULL);
	k_work_init(&state->work, timer_work_handler);

	struct pa_port_timer port = {
		.start_oneshot = adapter_start_oneshot,
		.stop = adapter_stop,
		.ctx = state,
	};

	return port;
}
