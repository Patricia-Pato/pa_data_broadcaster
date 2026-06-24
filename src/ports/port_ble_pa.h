#ifndef PORT_BLE_PA_H
#define PORT_BLE_PA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct pa_adv_info {
	uint8_t sid;
	uint8_t available_bytes;
};

struct pa_port_ble_pa {
	int (*set_data)(void *ctx, uint8_t adv_sid,
			const uint8_t *data, uint8_t data_len);
	int (*get_status)(void *ctx, uint8_t *num_adv,
			  struct pa_adv_info *info, uint8_t max_entries);
	bool (*is_ready)(void *ctx);
	void *ctx;
};

#endif /* PORT_BLE_PA_H */
