#ifndef ZEPHYR_BUTTON_ADAPTER_H
#define ZEPHYR_BUTTON_ADAPTER_H

#include <zephyr/kernel.h>
#include "port_button.h"

struct zephyr_button_adapter_state {
	pa_button_cb_t cb;
	void *cb_user_data;
	struct k_thread thread_data;
	k_tid_t thread_id;
};

struct pa_port_button zephyr_button_adapter_create(
	struct zephyr_button_adapter_state *state);

#endif /* ZEPHYR_BUTTON_ADAPTER_H */
