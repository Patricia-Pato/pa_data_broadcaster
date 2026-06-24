#include "zephyr_ble_pa_adapter.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_pa_adapter, CONFIG_MAIN_LOG_LEVEL);

/* AD Type for vendor-specific data */
#define BT_DATA_VS 0xFF

static int adapter_set_data(void *ctx, uint8_t adv_sid,
			    const uint8_t *data, uint8_t data_len)
{
	struct zephyr_ble_pa_adapter_state *s = ctx;

	if (s->ext_adv == NULL) {
		LOG_ERR("ext_adv not ready");
		return -EAGAIN;
	}

	if (data_len > PA_MAX_DATA_SIZE) {
		return -EINVAL;
	}

	memcpy(s->custom_ad_buf, data, data_len);

	/* Compose combined PA data: BASE + custom */
	if (s->base_per_adv != NULL && s->base_per_adv_count > 0) {
		s->combined_per_adv[0] = s->base_per_adv[0];
	}

	s->combined_per_adv[s->base_per_adv_count].type = BT_DATA_VS;
	s->combined_per_adv[s->base_per_adv_count].data = s->custom_ad_buf;
	s->combined_per_adv[s->base_per_adv_count].data_len = data_len;

	size_t total_count = s->base_per_adv_count + 1;

	int ret = bt_le_per_adv_set_data(s->ext_adv, s->combined_per_adv,
					 total_count);
	if (ret) {
		LOG_ERR("Failed to set PA data: %d", ret);
		return ret;
	}

	LOG_INF("PA data set: %u bytes on SID %u", data_len, adv_sid);
	return 0;
}

static int adapter_get_status(void *ctx, uint8_t *num_adv,
			      struct pa_adv_info *info, uint8_t max_entries)
{
	struct zephyr_ble_pa_adapter_state *s = ctx;

	if (s->ext_adv == NULL) {
		*num_adv = 0;
		return 0;
	}

	*num_adv = 1;
	if (max_entries >= 1) {
		struct bt_le_ext_adv_info adv_info;
		int ret = bt_le_ext_adv_get_info(s->ext_adv, &adv_info);

		info[0].sid = (ret == 0) ? adv_info.id : 0;

		uint8_t overhead = s->base_data_overhead + 2; /* +2 for AD header */

		if (overhead >= PA_MAX_DATA_SIZE) {
			info[0].available_bytes = 0;
		} else {
			info[0].available_bytes = PA_MAX_DATA_SIZE - overhead;
		}
	}

	return 0;
}

static bool adapter_is_ready(void *ctx)
{
	struct zephyr_ble_pa_adapter_state *s = ctx;

	return s->ext_adv != NULL;
}

struct pa_port_ble_pa zephyr_ble_pa_adapter_create(
	struct zephyr_ble_pa_adapter_state *state)
{
	memset(state, 0, sizeof(*state));

	struct pa_port_ble_pa port = {
		.set_data = adapter_set_data,
		.get_status = adapter_get_status,
		.is_ready = adapter_is_ready,
		.ctx = state,
	};

	return port;
}

void zephyr_ble_pa_adapter_set_ext_adv(
	struct zephyr_ble_pa_adapter_state *state,
	struct bt_le_ext_adv *ext_adv)
{
	state->ext_adv = ext_adv;
	LOG_INF("BLE PA adapter: ext_adv set");
}

void zephyr_ble_pa_adapter_set_base_per_adv(
	struct zephyr_ble_pa_adapter_state *state,
	const struct bt_data *base_per_adv,
	size_t count,
	uint8_t base_overhead)
{
	state->base_per_adv = base_per_adv;
	state->base_per_adv_count = count;
	state->base_data_overhead = base_overhead;
}
