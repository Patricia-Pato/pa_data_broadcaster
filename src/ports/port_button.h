#ifndef PORT_BUTTON_H
#define PORT_BUTTON_H

#include <stdint.h>

enum pa_button_id {
	PA_BUTTON_PLAY_PAUSE,
	PA_BUTTON_VOLUME_UP,
	PA_BUTTON_VOLUME_DOWN,
	PA_BUTTON_ACTION,
	PA_BUTTON_ID_COUNT,
};

enum pa_button_action {
	PA_BUTTON_PRESSED = 1,
};

typedef void (*pa_button_cb_t)(enum pa_button_id button, enum pa_button_action action,
			       void *user_data);

struct pa_port_button {
	int (*init)(void *ctx, pa_button_cb_t cb, void *user_data);
	void *ctx;
};

#endif /* PORT_BUTTON_H */
