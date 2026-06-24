#include "zephyr_button_adapter.h"
#include "zbus_common.h"
#include "button_assignments.h"

#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(button_adapter, CONFIG_MAIN_LOG_LEVEL);

ZBUS_SUBSCRIBER_DEFINE(pa_button_evt_sub, CONFIG_BUTTON_MSG_SUB_QUEUE_SIZE);
ZBUS_CHAN_DECLARE(button_chan);

K_THREAD_STACK_DEFINE(pa_button_thread_stack, CONFIG_BUTTON_MSG_SUB_STACK_SIZE);

static struct zephyr_button_adapter_state *g_state;

static enum pa_button_id map_pin_to_id(uint32_t pin)
{
	if (pin == BUTTON_PLAY_PAUSE) {
		return PA_BUTTON_PLAY_PAUSE;
	} else if (pin == BUTTON_VOLUME_UP) {
		return PA_BUTTON_VOLUME_UP;
	} else if (pin == BUTTON_VOLUME_DOWN) {
		return PA_BUTTON_VOLUME_DOWN;
	} else if (pin == BUTTON_4 || pin == BUTTON_5) {
		return PA_BUTTON_ACTION;
	}
	return PA_BUTTON_ID_COUNT;
}

static void button_thread_fn(void *arg1, void *arg2, void *arg3)
{
	(void)arg1;
	(void)arg2;
	(void)arg3;

	int ret;
	const struct zbus_channel *chan;

	while (1) {
		ret = zbus_sub_wait(&pa_button_evt_sub, &chan, K_FOREVER);
		if (ret) {
			continue;
		}

		struct button_msg msg;

		ret = zbus_chan_read(chan, &msg, ZBUS_READ_TIMEOUT_MS);
		if (ret) {
			continue;
		}

		if (msg.button_action != BUTTON_PRESS) {
			continue;
		}

		enum pa_button_id id = map_pin_to_id(msg.button_pin);

		if (id >= PA_BUTTON_ID_COUNT) {
			LOG_WRN("Unknown button pin: %d", msg.button_pin);
			continue;
		}

		if (g_state->cb != NULL) {
			g_state->cb(id, PA_BUTTON_PRESSED, g_state->cb_user_data);
		}
	}
}

static int adapter_init(void *ctx, pa_button_cb_t cb, void *user_data)
{
	struct zephyr_button_adapter_state *s = ctx;

	s->cb = cb;
	s->cb_user_data = user_data;

	int ret = zbus_chan_add_obs(&button_chan, &pa_button_evt_sub,
				   ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("Failed to add button observer: %d", ret);
		return ret;
	}

	s->thread_id = k_thread_create(
		&s->thread_data, pa_button_thread_stack,
		CONFIG_BUTTON_MSG_SUB_STACK_SIZE,
		button_thread_fn, NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_BUTTON_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	k_thread_name_set(s->thread_id, "PA_BTN_SUB");

	LOG_INF("Button adapter initialized");
	return 0;
}

struct pa_port_button zephyr_button_adapter_create(
	struct zephyr_button_adapter_state *state)
{
	memset(state, 0, sizeof(*state));
	g_state = state;

	struct pa_port_button port = {
		.init = adapter_init,
		.ctx = state,
	};

	return port;
}
