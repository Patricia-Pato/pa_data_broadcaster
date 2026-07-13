#include "pa_data_gatt.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pa_data_gatt, CONFIG_MAIN_LOG_LEVEL);

#define PA_DATA_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0xf3641400, 0x0001, 0x4000, 0x8000, 0x000000000000)
#define PA_DATA_CHAR_UUID_VAL \
	BT_UUID_128_ENCODE(0xf3641400, 0x0002, 0x4000, 0x8000, 0x000000000000)

#define PA_DATA_SVC_UUID  BT_UUID_DECLARE_128(PA_DATA_SVC_UUID_VAL)
#define PA_DATA_CHAR_UUID BT_UUID_DECLARE_128(PA_DATA_CHAR_UUID_VAL)

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("PA Data notifications %s",
		value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(pa_data_svc,
	BT_GATT_PRIMARY_SERVICE(PA_DATA_SVC_UUID),
	BT_GATT_CHARACTERISTIC(PA_DATA_CHAR_UUID,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ccc_cfg_changed,
		     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int pa_data_gatt_notify(const uint8_t *data, uint16_t len)
{
	return bt_gatt_notify(NULL, &pa_data_svc.attrs[2], data, len);
}
