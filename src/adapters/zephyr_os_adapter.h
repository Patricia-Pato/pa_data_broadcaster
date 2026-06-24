#ifndef ZEPHYR_OS_ADAPTER_H
#define ZEPHYR_OS_ADAPTER_H

#include <zephyr/kernel.h>
#include "port_os.h"

/* ===== Deferred Work ===== */

struct zephyr_work_state {
	struct k_work work;
	pa_work_handler_t handler;
	void *user_data;
};

struct pa_port_work zephyr_work_create(struct zephyr_work_state *state);

/* ===== Critical Section ===== */

struct pa_port_critical zephyr_critical_create(void);

/* ===== Sleep ===== */

struct pa_port_sleep zephyr_sleep_create(void);

#endif /* ZEPHYR_OS_ADAPTER_H */
