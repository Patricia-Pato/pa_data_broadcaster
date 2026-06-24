#include "zephyr_led_adapter.h"
#include "led.h"
#include "led_assignments.h"

static const uint8_t led_id_map[PA_LED_ID_COUNT] = {
	[PA_LED_CONN_STATUS] = LED_AUDIO_CONN_STATUS,
	[PA_LED_SYNC_STATUS] = LED_AUDIO_SYNC_STATUS,
	[PA_LED_APP_STATUS]  = LED_AUDIO_APP_STATUS,
};

static int adapter_led_on(void *ctx, enum pa_led_id led)
{
	(void)ctx;
	if (led >= PA_LED_ID_COUNT) {
		return -1;
	}
	return led_on(led_id_map[led]);
}

static int adapter_led_off(void *ctx, enum pa_led_id led)
{
	(void)ctx;
	if (led >= PA_LED_ID_COUNT) {
		return -1;
	}
	return led_off(led_id_map[led]);
}

static int adapter_led_blink(void *ctx, enum pa_led_id led)
{
	(void)ctx;
	if (led >= PA_LED_ID_COUNT) {
		return -1;
	}
	return led_blink(led_id_map[led]);
}

struct pa_port_led zephyr_led_adapter_create(void)
{
	struct pa_port_led port = {
		.on = adapter_led_on,
		.off = adapter_led_off,
		.blink = adapter_led_blink,
		.ctx = NULL,
	};

	return port;
}
