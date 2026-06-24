#ifndef PORT_TIMER_H
#define PORT_TIMER_H

#include <stdint.h>

typedef void (*pa_timer_cb_t)(void *user_data);

struct pa_port_timer {
	int (*start_oneshot)(void *ctx, uint32_t interval_ms,
			     pa_timer_cb_t cb, void *user_data);
	int (*stop)(void *ctx);
	void *ctx;
};

#endif /* PORT_TIMER_H */
