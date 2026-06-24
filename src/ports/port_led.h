#ifndef PORT_LED_H
#define PORT_LED_H

#include <stdint.h>

enum pa_led_id {
	PA_LED_CONN_STATUS,
	PA_LED_SYNC_STATUS,
	PA_LED_APP_STATUS,
	PA_LED_ID_COUNT,
};

struct pa_port_led {
	int (*on)(void *ctx, enum pa_led_id led);
	int (*off)(void *ctx, enum pa_led_id led);
	int (*blink)(void *ctx, enum pa_led_id led);
	void *ctx;
};

#endif /* PORT_LED_H */
