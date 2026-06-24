#ifndef ZEPHYR_BLE_PA_ADAPTER_H
#define ZEPHYR_BLE_PA_ADAPTER_H

#include "pa_protocol.h"
#include "port_ble_pa.h"

#include <zephyr/bluetooth/bluetooth.h>

struct zephyr_ble_pa_adapter_state {
	struct bt_le_ext_adv *ext_adv;
	const struct bt_data *base_per_adv;
	size_t base_per_adv_count;
	uint8_t custom_ad_buf[PA_MAX_DATA_SIZE];
	struct bt_data combined_per_adv[2]; /* [0]=BASE, [1]=custom */
	uint8_t base_data_overhead;
};

struct pa_port_ble_pa zephyr_ble_pa_adapter_create(
	struct zephyr_ble_pa_adapter_state *state);

void zephyr_ble_pa_adapter_set_ext_adv(
	struct zephyr_ble_pa_adapter_state *state,
	struct bt_le_ext_adv *ext_adv);

void zephyr_ble_pa_adapter_set_base_per_adv(
	struct zephyr_ble_pa_adapter_state *state,
	const struct bt_data *base_per_adv,
	size_t count,
	uint8_t base_overhead);

#endif /* ZEPHYR_BLE_PA_ADAPTER_H */
