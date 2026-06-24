#ifndef ZEPHYR_TIMER_ADAPTER_H
#define ZEPHYR_TIMER_ADAPTER_H

#include <zephyr/kernel.h>
#include "port_timer.h"

struct zephyr_timer_adapter_state {
	struct k_timer timer;
	struct k_work work;
	pa_timer_cb_t cb;
	void *cb_user_data;
};

struct pa_port_timer zephyr_timer_adapter_create(
	struct zephyr_timer_adapter_state *state);

#endif /* ZEPHYR_TIMER_ADAPTER_H */
