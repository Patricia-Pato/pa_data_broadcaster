#include "auracast_gatt.h"

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(auracast_gatt, CONFIG_MAIN_LOG_LEVEL);

/* UUIDs from PA受信機.xlsx spec */
#define AURACAST_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0x821C11EA, 0x5288, 0x43D9, 0x8595, 0x0583D986B462)
#define CMD_REQ_UUID_VAL \
	BT_UUID_128_ENCODE(0x8E2D4E6F, 0xD028, 0x44B0, 0x953E, 0xA82E24CE45B8)
#define CMD_RSP_UUID_VAL \
	BT_UUID_128_ENCODE(0xB6823B4B, 0x28A4, 0x45Ab, 0x9B60, 0x489B83FEB531)
#define PA_DATA_UUID_VAL \
	BT_UUID_128_ENCODE(0xE8A5534B, 0x35F9, 0x43DC, 0xAF2C, 0x81A69705D5C6)

#define AURACAST_SVC_UUID BT_UUID_DECLARE_128(AURACAST_SVC_UUID_VAL)
#define CMD_REQ_UUID      BT_UUID_DECLARE_128(CMD_REQ_UUID_VAL)
#define CMD_RSP_UUID      BT_UUID_DECLARE_128(CMD_RSP_UUID_VAL)
#define PA_DATA_UUID      BT_UUID_DECLARE_128(PA_DATA_UUID_VAL)

static auracast_gatt_cmd_cb_t cmd_cb;

static ssize_t write_cmd_req(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;

	if (len < 4) {
		LOG_WRN("Command too short: %u bytes", len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint16_t opcode = sys_get_be16(&data[0]);
	uint16_t param_len = sys_get_be16(&data[2]);

	LOG_INF("CMD recv: opcode=0x%04X param_len=%u total=%u", opcode, param_len, len);

	if (cmd_cb) {
		cmd_cb(conn, opcode, &data[4], param_len);
	}

	return len;
}

static void cmd_rsp_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("Response notifications %s",
		value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

static void pa_data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("PA Data notifications %s",
		value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

/*
 * Attribute index map:
 *  [0] Primary Service
 *  [1] Char decl  (cmd_req)
 *  [2] Char value (cmd_req)       <- write target
 *  [3] Char decl  (cmd_rsp)
 *  [4] Char value (cmd_rsp)       <- notify target
 *  [5] CCC        (cmd_rsp)
 *  [6] Char decl  (pa_data)
 *  [7] Char value (pa_data)       <- notify target
 *  [8] CCC        (pa_data)
 */
BT_GATT_SERVICE_DEFINE(auracast_svc,
	BT_GATT_PRIMARY_SERVICE(AURACAST_SVC_UUID),

	BT_GATT_CHARACTERISTIC(CMD_REQ_UUID,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_AUTHEN,
			       NULL, write_cmd_req, NULL),

	BT_GATT_CHARACTERISTIC(CMD_RSP_UUID,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(cmd_rsp_ccc_changed,
		     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(PA_DATA_UUID,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(pa_data_ccc_changed,
		     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void auracast_gatt_set_cmd_cb(auracast_gatt_cmd_cb_t cb)
{
	cmd_cb = cb;
}

int auracast_gatt_send_response(struct bt_conn *conn,
				uint16_t opcode, const uint8_t *params,
				uint16_t param_len)
{
	uint8_t buf[512];

	if (4 + param_len > sizeof(buf)) {
		return -ENOMEM;
	}

	sys_put_be16(opcode, &buf[0]);
	sys_put_be16(param_len, &buf[2]);
	if (param_len > 0 && params) {
		memcpy(&buf[4], params, param_len);
	}

	return bt_gatt_notify(conn, &auracast_svc.attrs[4], buf, 4 + param_len);
}

int auracast_gatt_send_pa_data(struct bt_conn *conn,
			       const uint8_t *data, uint16_t len)
{
	uint8_t buf[512];

	if (4 + len > sizeof(buf)) {
		return -ENOMEM;
	}

	sys_put_be16(AC_OP_PA_DATA_NTF, &buf[0]);
	sys_put_be16(len, &buf[2]);
	if (len > 0) {
		memcpy(&buf[4], data, len);
	}

	return bt_gatt_notify(conn, &auracast_svc.attrs[7], buf, 4 + len);
}
