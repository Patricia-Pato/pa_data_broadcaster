/*
 * Auracast Receiver — PA受信機 仕様書準拠実装
 *
 * GATT service with TOSHIBA Auracast Service UUIDs.
 * All control is via GATT commands from the assistant app.
 */

#include "streamctrl.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/sys/byteorder.h>

#include "broadcast_sink.h"
#include "zbus_common.h"
#include "peripherals.h"
#include "macros_common.h"
#include "audio_system.h"
#include "bt_mgmt.h"
#include "le_audio_rx.h"
#include "fw_info_app.h"
#include "auracast_gatt.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_BT_AUDIO_CONCURRENT_RX_STREAMS_MAX <= CONFIG_AUDIO_DECODE_CHANNELS_MAX);

#define BT_DATA_VS             0xFF
#define BT_DATA_BROADCAST_NAME 0x30
#define BT_UUID_BAAS_VAL       0x1852
#define BT_UUID_PBA_VAL        0x1856
#define PA_MAX_DATA_LEN        255

#define SCAN_CACHE_SIZE 16

/* ===== Receiver state ===== */

static uint8_t receiver_state;
static bool pa_data_notify_enabled;
static struct bt_conn *current_conn;
static bool command_busy;

static uint8_t current_volume;
static uint8_t current_mute;

static struct bt_le_per_adv_sync *pa_sync_handle;
static bt_addr_le_t pa_sync_addr;

static enum stream_state strm_state = STATE_PAUSED;

/* Scan result cache for broadcast_id lookup during PA sync */
struct scan_cache_entry {
	bt_addr_le_t addr;
	uint8_t sid;
	uint32_t broadcast_id;
	bool valid;
};

static struct scan_cache_entry scan_cache[SCAN_CACHE_SIZE];

static void cache_scan_result(const bt_addr_le_t *addr, uint8_t sid, uint32_t broadcast_id)
{
	for (int i = 0; i < SCAN_CACHE_SIZE; i++) {
		if (scan_cache[i].valid &&
		    bt_addr_le_eq(&scan_cache[i].addr, addr) &&
		    scan_cache[i].sid == sid) {
			scan_cache[i].broadcast_id = broadcast_id;
			return;
		}
	}

	for (int i = 0; i < SCAN_CACHE_SIZE; i++) {
		if (!scan_cache[i].valid) {
			bt_addr_le_copy(&scan_cache[i].addr, addr);
			scan_cache[i].sid = sid;
			scan_cache[i].broadcast_id = broadcast_id;
			scan_cache[i].valid = true;
			return;
		}
	}

	bt_addr_le_copy(&scan_cache[0].addr, addr);
	scan_cache[0].sid = sid;
	scan_cache[0].broadcast_id = broadcast_id;
}

static uint32_t lookup_broadcast_id(const bt_addr_le_t *addr, uint8_t sid)
{
	for (int i = 0; i < SCAN_CACHE_SIZE; i++) {
		if (scan_cache[i].valid &&
		    bt_addr_le_eq(&scan_cache[i].addr, addr) &&
		    scan_cache[i].sid == sid) {
			return scan_cache[i].broadcast_id;
		}
	}
	return BRDCAST_ID_NOT_USED;
}

/* ===== Notification helpers ===== */

static void notify_state_transition(void)
{
	if (!current_conn) {
		return;
	}
	uint8_t state = receiver_state;

	auracast_gatt_send_response(current_conn, AC_OP_STATE_TRANSITION_NTF,
				    &state, 1);
}

static void notify_pa_sync_state(uint8_t state, const bt_addr_le_t *addr,
				 uint32_t audio_location)
{
	if (!current_conn) {
		return;
	}
	uint8_t buf[11];

	buf[0] = state;
	memcpy(&buf[1], addr->a.val, 6);
	sys_put_be32(audio_location, &buf[7]);

	auracast_gatt_send_response(current_conn, AC_OP_PA_SYNC_STATE_NTF,
				    buf, sizeof(buf));
}

static void notify_bis_sync_state(uint8_t state)
{
	if (!current_conn) {
		return;
	}
	auracast_gatt_send_response(current_conn, AC_OP_BIS_SYNC_STATE_NTF,
				    &state, 1);
}

static void notify_volume_change(void)
{
	if (!current_conn) {
		return;
	}
	uint8_t buf[2];

	buf[0] = current_volume;
	buf[1] = current_mute;

	auracast_gatt_send_response(current_conn, AC_OP_VOLUME_CHANGE_NTF,
				    buf, sizeof(buf));
}

/* ===== Connectable advertising via bt_mgmt ===== */

static const uint8_t ad_flags[] = {
	BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
};
static const uint8_t hid_uuid[] = { BT_UUID_16_ENCODE(0x1812) };
static const uint8_t appearance[] = { 0xC1, 0x03 };
static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_FLAGS, ad_flags, sizeof(ad_flags)),
	BT_DATA(BT_DATA_UUID16_ALL, hid_uuid, sizeof(hid_uuid)),
	BT_DATA(BT_DATA_GAP_APPEARANCE, appearance, sizeof(appearance)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int start_advertising(void)
{
	int ret;

	ret = bt_mgmt_adv_start(0, ad, ARRAY_SIZE(ad), NULL, 0, true);
	if (ret) {
		LOG_ERR("Failed to start advertising: %d", ret);
		return ret;
	}

	LOG_INF("Advertising started as \"%s\"", CONFIG_BT_DEVICE_NAME);
	return 0;
}

/* ===== Scanner for search function ===== */

static void scan_recv_cb(const struct bt_le_scan_recv_info *info,
			 struct net_buf_simple *buf)
{
	if (!(receiver_state & AC_STATE_SEARCHING) || !current_conn) {
		return;
	}

	uint8_t broadcast_name[129] = {0};
	uint8_t broadcast_name_len = 0;
	uint8_t broadcast_id_bytes[3] = {0};
	bool has_broadcast_id = false;
	uint8_t encrypted = 0;

	struct net_buf_simple_state save;

	net_buf_simple_save(buf, &save);

	while (buf->len > 1) {
		uint8_t len = net_buf_simple_pull_u8(buf);

		if (len == 0 || len > buf->len) {
			break;
		}

		uint8_t type = net_buf_simple_pull_u8(buf);
		uint8_t data_len = len - 1;

		if (type == BT_DATA_BROADCAST_NAME && data_len > 0) {
			broadcast_name_len = MIN(data_len, sizeof(broadcast_name) - 1);
			memcpy(broadcast_name, buf->data, broadcast_name_len);
		} else if (type == BT_DATA_SVC_DATA16 && data_len >= 2) {
			uint16_t uuid = sys_get_le16(buf->data);

			if (uuid == BT_UUID_BAAS_VAL && data_len >= 5) {
				memcpy(broadcast_id_bytes, &buf->data[2], 3);
				has_broadcast_id = true;
			} else if (uuid == BT_UUID_PBA_VAL && data_len >= 3) {
				encrypted = (buf->data[2] & 0x01) ? 0x01 : 0x00;
			}
		}

		net_buf_simple_pull(buf, data_len);
	}

	net_buf_simple_restore(buf, &save);

	if (!has_broadcast_id || broadcast_name_len == 0) {
		return;
	}

	uint32_t bcast_id = broadcast_id_bytes[0] |
			    ((uint32_t)broadcast_id_bytes[1] << 8) |
			    ((uint32_t)broadcast_id_bytes[2] << 16);

	cache_scan_result(info->addr, info->sid, bcast_id);

	uint8_t ntf[256];
	uint16_t pos = 0;

	ntf[pos++] = info->sid;
	ntf[pos++] = info->addr->type;
	memcpy(&ntf[pos], info->addr->a.val, 6);
	pos += 6;
	ntf[pos++] = broadcast_name_len;
	memcpy(&ntf[pos], broadcast_name, broadcast_name_len);
	pos += broadcast_name_len;
	memcpy(&ntf[pos], broadcast_id_bytes, 3);
	pos += 3;
	ntf[pos++] = encrypted;
	ntf[pos++] = (uint8_t)(int8_t)info->rssi;

	auracast_gatt_send_response(current_conn, AC_OP_SEARCH_RESULT_NTF,
				    ntf, pos);
}

static struct bt_le_scan_cb scan_cbs = {
	.recv = scan_recv_cb,
};

/* ===== PA data receive ===== */

static uint8_t prev_pa_data[PA_MAX_DATA_LEN];
static uint8_t prev_pa_data_len;
static bool pa_data_received_once;

static bool pa_data_parse_cb(struct bt_data *data, void *user_data)
{
	if (data->type != BT_DATA_VS) {
		return true;
	}

	bool changed = !pa_data_received_once ||
		       data->data_len != prev_pa_data_len ||
		       memcmp(data->data, prev_pa_data, data->data_len) != 0;

	if (changed) {
		LOG_INF("PA data (%u bytes)", data->data_len);

		memcpy(prev_pa_data, data->data, data->data_len);
		prev_pa_data_len = data->data_len;
		pa_data_received_once = true;

		if (pa_data_notify_enabled && current_conn) {
			int err = auracast_gatt_send_pa_data(current_conn,
							     data->data,
							     data->data_len);
			if (err && err != -ENOTCONN) {
				LOG_WRN("PA data notify failed: %d", err);
			}
		}
	}

	return false;
}

/* ===== PA sync callbacks ===== */

static void pa_synced_cb(struct bt_le_per_adv_sync *sync,
			 struct bt_le_per_adv_sync_synced_info *info)
{
	LOG_INF("PA synced (SID %u)", info->sid);

	pa_sync_handle = sync;
	receiver_state |= AC_STATE_PA_SYNCED;

	/* 0x01 = PA同期中 */
	notify_pa_sync_state(0x01, info->addr, 0x00000003);
	notify_state_transition();

	command_busy = false;
}

static void pa_sync_term_cb(struct bt_le_per_adv_sync *sync,
			    const struct bt_le_per_adv_sync_term_info *info)
{
	LOG_INF("PA sync lost");

	if (receiver_state & AC_STATE_BIS_SYNCED) {
		broadcast_sink_stop();
		receiver_state &= ~AC_STATE_BIS_SYNCED;
		/* 0x00 = BIS非同期中 */
		notify_bis_sync_state(0x00);
	}

	pa_data_notify_enabled = false;
	pa_data_received_once = false;
	pa_sync_handle = NULL;
	receiver_state &= ~AC_STATE_PA_SYNCED;

	/* 0x00 = PA非同期中 */
	notify_pa_sync_state(0x00, &pa_sync_addr, 0x00000003);
	notify_state_transition();

	command_busy = false;
}

static void pa_recv_cb(struct bt_le_per_adv_sync *sync,
		       const struct bt_le_per_adv_sync_recv_info *info,
		       struct net_buf_simple *buf)
{
	bt_data_parse(buf, pa_data_parse_cb, NULL);
}

static struct bt_le_per_adv_sync_cb pa_sync_cbs = {
	.synced = pa_synced_cb,
	.term = pa_sync_term_cb,
	.recv = pa_recv_cb,
};

/* ===== GATT command handlers ===== */

static void handle_device_info_req(struct bt_conn *conn)
{
	uint8_t rsp[128];
	uint16_t pos = 0;

	const char *vendor = "Nordic";

	rsp[pos++] = 0x00;
	rsp[pos++] = strlen(vendor);
	memcpy(&rsp[pos], vendor, strlen(vendor));
	pos += strlen(vendor);

	const char *chip = "nRF5340";

	rsp[pos++] = 0x01;
	rsp[pos++] = strlen(chip);
	memcpy(&rsp[pos], chip, strlen(chip));
	pos += strlen(chip);

	const char *fw_ver = "1.0.0";

	rsp[pos++] = 0x02;
	rsp[pos++] = strlen(fw_ver);
	memcpy(&rsp[pos], fw_ver, strlen(fw_ver));
	pos += strlen(fw_ver);

	/* Audio Location: Front Left & Front Right */
	rsp[pos++] = 0x03;
	rsp[pos++] = 0x04;
	sys_put_be32(0x00000003, &rsp[pos]);
	pos += 4;

	auracast_gatt_send_response(conn, AC_OP_DEVICE_INFO_RSP, rsp, pos);
}

static void handle_search_start_req(struct bt_conn *conn)
{
	if (receiver_state & AC_STATE_SEARCHING) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_SEARCH_START_RSP,
					    &result, 1);
		return;
	}

	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	int ret = bt_le_scan_start(&scan_param, NULL);
	uint8_t result = (ret == 0) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

	auracast_gatt_send_response(conn, AC_OP_SEARCH_START_RSP, &result, 1);

	if (result == AC_RESULT_SUCCESS) {
		receiver_state |= AC_STATE_SEARCHING;
		notify_state_transition();
	}
}

static void handle_search_stop_req(struct bt_conn *conn)
{
	if (!(receiver_state & AC_STATE_SEARCHING)) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_SEARCH_STOP_RSP,
					    &result, 1);
		return;
	}

	bt_le_scan_stop();

	uint8_t result = AC_RESULT_SUCCESS;

	auracast_gatt_send_response(conn, AC_OP_SEARCH_STOP_RSP, &result, 1);

	receiver_state &= ~AC_STATE_SEARCHING;
	notify_state_transition();
}

static void handle_pa_sync_start_req(struct bt_conn *conn,
				     const uint8_t *params, uint16_t param_len)
{
	if (param_len < 8) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_PA_SYNC_START_RSP,
					    &result, 1);
		return;
	}

	uint8_t adv_sid = params[0];
	bt_addr_le_t addr;

	addr.type = params[1];
	memcpy(addr.a.val, &params[2], 6);

	/* If already PA synced, release the old sync first (per spec) */
	if (pa_sync_handle) {
		if (receiver_state & AC_STATE_BIS_SYNCED) {
			broadcast_sink_stop();
			receiver_state &= ~AC_STATE_BIS_SYNCED;
		}
		pa_data_notify_enabled = false;
		bt_le_per_adv_sync_delete(pa_sync_handle);
		pa_sync_handle = NULL;
		receiver_state &= ~AC_STATE_PA_SYNCED;
	}

	struct bt_le_per_adv_sync_param sync_param = {0};

	bt_addr_le_copy(&sync_param.addr, &addr);
	sync_param.sid = adv_sid;
	sync_param.skip = 0;
	sync_param.timeout = BT_GAP_PER_ADV_MAX_TIMEOUT;

	int ret = bt_le_per_adv_sync_create(&sync_param, &pa_sync_handle);
	uint8_t result = (ret == 0) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

	if (result == AC_RESULT_SUCCESS) {
		bt_addr_le_copy(&pa_sync_addr, &addr);
		command_busy = true;
	}

	auracast_gatt_send_response(conn, AC_OP_PA_SYNC_START_RSP, &result, 1);
}

static void handle_pa_sync_release_req(struct bt_conn *conn)
{
	if (!(receiver_state & AC_STATE_PA_SYNCED)) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_PA_SYNC_RELEASE_RSP,
					    &result, 1);
		return;
	}

	/* BIS sync release + PA data notification stop (per spec) */
	if (receiver_state & AC_STATE_BIS_SYNCED) {
		broadcast_sink_stop();
		receiver_state &= ~AC_STATE_BIS_SYNCED;
		notify_bis_sync_state(0x00);
	}
	pa_data_notify_enabled = false;

	int ret = bt_le_per_adv_sync_delete(pa_sync_handle);
	uint8_t result = (ret == 0) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

	auracast_gatt_send_response(conn, AC_OP_PA_SYNC_RELEASE_RSP, &result, 1);

	if (result == AC_RESULT_SUCCESS) {
		pa_sync_handle = NULL;
		pa_data_received_once = false;
		receiver_state &= ~AC_STATE_PA_SYNCED;

		notify_pa_sync_state(0x00, &pa_sync_addr, 0x00000003);
		notify_state_transition();
	}
}

static void handle_bis_sync_start_req(struct bt_conn *conn,
				      const uint8_t *params, uint16_t param_len)
{
	if (!(receiver_state & AC_STATE_PA_SYNCED)) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_START_RSP,
					    &result, 1);
		return;
	}

	if (receiver_state & AC_STATE_BIS_SYNCED) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_START_RSP,
					    &result, 1);
		return;
	}

	if (param_len >= 16) {
		uint8_t code[16];

		memcpy(code, params, 16);
		broadcast_sink_broadcast_code_set(code);
	}

	uint32_t bcast_id = lookup_broadcast_id(&pa_sync_addr, 0);
	int ret = broadcast_sink_pa_sync_set(pa_sync_handle, bcast_id);

	if (ret) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_START_RSP,
					    &result, 1);
		return;
	}

	ret = broadcast_sink_start();
	uint8_t result = (ret == 0) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

	auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_START_RSP, &result, 1);

	if (result == AC_RESULT_SUCCESS) {
		command_busy = true;
	}
}

static void handle_bis_sync_stop_req(struct bt_conn *conn)
{
	if (!(receiver_state & AC_STATE_BIS_SYNCED)) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_STOP_RSP,
					    &result, 1);
		return;
	}

	int ret = broadcast_sink_stop();
	uint8_t result = (ret == 0) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

	auracast_gatt_send_response(conn, AC_OP_BIS_SYNC_STOP_RSP, &result, 1);

	if (result == AC_RESULT_SUCCESS) {
		receiver_state &= ~AC_STATE_BIS_SYNCED;
		strm_state = STATE_PAUSED;
		audio_system_stop();

		notify_bis_sync_state(0x00);
		notify_state_transition();
	}
}

static void handle_pa_data_ntf_start_req(struct bt_conn *conn)
{
	uint8_t result;

	if (!(receiver_state & AC_STATE_PA_SYNCED)) {
		result = AC_RESULT_FAIL;
	} else {
		pa_data_notify_enabled = true;
		result = AC_RESULT_SUCCESS;
	}

	auracast_gatt_send_response(conn, AC_OP_PA_DATA_NTF_START_RSP,
				    &result, 1);
}

static void handle_pa_data_ntf_stop_req(struct bt_conn *conn)
{
	uint8_t result;

	if (!pa_data_notify_enabled) {
		result = AC_RESULT_FAIL;
	} else {
		pa_data_notify_enabled = false;
		result = AC_RESULT_SUCCESS;
	}

	auracast_gatt_send_response(conn, AC_OP_PA_DATA_NTF_STOP_RSP,
				    &result, 1);
}

static void handle_volume_set_req(struct bt_conn *conn,
				  const uint8_t *params, uint16_t param_len)
{
	if (param_len < 1) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_VOLUME_SET_RSP,
					    &result, 1);
		return;
	}

	current_volume = params[0];

	uint8_t result = AC_RESULT_SUCCESS;

	auracast_gatt_send_response(conn, AC_OP_VOLUME_SET_RSP, &result, 1);
	notify_volume_change();
}

static void handle_mute_set_req(struct bt_conn *conn,
				const uint8_t *params, uint16_t param_len)
{
	if (param_len < 1) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_MUTE_SET_RSP,
					    &result, 1);
		return;
	}

	current_mute = params[0];

	uint8_t result = AC_RESULT_SUCCESS;

	auracast_gatt_send_response(conn, AC_OP_MUTE_SET_RSP, &result, 1);
	notify_volume_change();
}

static void handle_volume_get_req(struct bt_conn *conn)
{
	uint8_t rsp[2];

	rsp[0] = current_volume;
	rsp[1] = current_mute;

	auracast_gatt_send_response(conn, AC_OP_VOLUME_GET_RSP,
				    rsp, sizeof(rsp));
}

static void handle_audio_output_req(struct bt_conn *conn,
				    const uint8_t *params, uint16_t param_len)
{
	if (param_len < 4) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_AUDIO_OUTPUT_RSP,
					    &result, 1);
		return;
	}

	uint32_t audio_location = sys_get_be32(params);

	LOG_INF("Audio output setting: 0x%08X", audio_location);

	uint8_t result = AC_RESULT_SUCCESS;

	auracast_gatt_send_response(conn, AC_OP_AUDIO_OUTPUT_RSP, &result, 1);
}

static void gatt_cmd_handler(struct bt_conn *conn, uint16_t opcode,
			     const uint8_t *params, uint16_t param_len)
{
	if (command_busy) {
		uint16_t rsp_opcode = (opcode & 0x00FF) | 0x0800;
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, rsp_opcode, &result, 1);
		LOG_WRN("Command 0x%04X rejected (busy)", opcode);
		return;
	}

	switch (opcode) {
	case AC_OP_DEVICE_INFO_REQ:
		handle_device_info_req(conn);
		break;
	case AC_OP_SEARCH_START_REQ:
		handle_search_start_req(conn);
		break;
	case AC_OP_SEARCH_STOP_REQ:
		handle_search_stop_req(conn);
		break;
	case AC_OP_PA_SYNC_START_REQ:
		handle_pa_sync_start_req(conn, params, param_len);
		break;
	case AC_OP_PA_SYNC_RELEASE_REQ:
		handle_pa_sync_release_req(conn);
		break;
	case AC_OP_BIS_SYNC_START_REQ:
		handle_bis_sync_start_req(conn, params, param_len);
		break;
	case AC_OP_BIS_SYNC_STOP_REQ:
		handle_bis_sync_stop_req(conn);
		break;
	case AC_OP_PA_DATA_NTF_START_REQ:
		handle_pa_data_ntf_start_req(conn);
		break;
	case AC_OP_PA_DATA_NTF_STOP_REQ:
		handle_pa_data_ntf_stop_req(conn);
		break;
	case AC_OP_VOLUME_SET_REQ:
		handle_volume_set_req(conn, params, param_len);
		break;
	case AC_OP_MUTE_SET_REQ:
		handle_mute_set_req(conn, params, param_len);
		break;
	case AC_OP_VOLUME_GET_REQ:
		handle_volume_get_req(conn);
		break;
	case AC_OP_AUDIO_OUTPUT_REQ:
		handle_audio_output_req(conn, params, param_len);
		break;
	default:
		LOG_WRN("Unknown opcode: 0x%04X", opcode);
		break;
	}
}

/* ===== Pairing callbacks ===== */

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete (bonded: %s)", bonded ? "yes" : "no");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_info_cb auth_info_cbs = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ===== zbus event handling ===== */

ZBUS_SUBSCRIBER_DEFINE(button_evt_sub, CONFIG_BUTTON_MSG_SUB_QUEUE_SIZE);
ZBUS_MSG_SUBSCRIBER_DEFINE(le_audio_evt_sub);
ZBUS_MSG_SUBSCRIBER_DEFINE(bt_mgmt_evt_sub);

ZBUS_CHAN_DECLARE(button_chan);
ZBUS_CHAN_DECLARE(le_audio_chan);
ZBUS_CHAN_DECLARE(bt_mgmt_chan);
ZBUS_CHAN_DECLARE(volume_chan);
ZBUS_OBS_DECLARE(volume_evt_sub);

static struct k_thread button_msg_sub_thread_data;
static struct k_thread le_audio_msg_sub_thread_data;
static struct k_thread bt_mgmt_msg_sub_thread_data;

static k_tid_t button_msg_sub_thread_id;
static k_tid_t le_audio_msg_sub_thread_id;
static k_tid_t bt_mgmt_msg_sub_thread_id;

K_THREAD_STACK_DEFINE(button_msg_sub_thread_stack, CONFIG_BUTTON_MSG_SUB_STACK_SIZE);
K_THREAD_STACK_DEFINE(le_audio_msg_sub_thread_stack, CONFIG_LE_AUDIO_MSG_SUB_STACK_SIZE);
K_THREAD_STACK_DEFINE(bt_mgmt_msg_sub_thread_stack, CONFIG_BT_MGMT_MSG_SUB_STACK_SIZE);

static void button_msg_sub_thread(void)
{
	int ret;
	const struct zbus_channel *chan;

	while (1) {
		ret = zbus_sub_wait(&button_evt_sub, &chan, K_FOREVER);
		ERR_CHK(ret);

		struct button_msg msg;

		ret = zbus_chan_read(chan, &msg, ZBUS_READ_TIMEOUT_MS);
		ERR_CHK(ret);

		if (msg.button_action != BUTTON_PRESS) {
			continue;
		}

		LOG_INF("Button %d pressed", msg.button_pin);
	}
}

static void le_audio_msg_sub_thread(void)
{
	int ret;
	const struct zbus_channel *chan;

	while (1) {
		struct le_audio_msg msg;

		ret = zbus_sub_wait_msg(&le_audio_evt_sub, &chan, &msg, K_FOREVER);
		ERR_CHK(ret);

		switch (msg.event) {
		case LE_AUDIO_EVT_STREAMING:
			LOG_INF("BIS streaming started");

			if (strm_state != STATE_STREAMING) {
				audio_system_start();
				strm_state = STATE_STREAMING;
			}

			receiver_state |= AC_STATE_BIS_SYNCED;
			/* 0x01 = BIS同期中 */
			notify_bis_sync_state(0x01);
			notify_state_transition();
			command_busy = false;
			break;

		case LE_AUDIO_EVT_NOT_STREAMING:
			LOG_INF("BIS streaming stopped");

			if (strm_state != STATE_PAUSED) {
				strm_state = STATE_PAUSED;
				audio_system_stop();
			}
			break;

		case LE_AUDIO_EVT_SYNC_LOST:
			LOG_INF("Sync lost");

			if (msg.pa_sync) {
				bt_mgmt_pa_sync_delete(msg.pa_sync);
			}

			if (strm_state != STATE_PAUSED) {
				strm_state = STATE_PAUSED;
				audio_system_stop();
			}

			pa_data_received_once = false;
			pa_data_notify_enabled = false;

			if (receiver_state & AC_STATE_BIS_SYNCED) {
				receiver_state &= ~AC_STATE_BIS_SYNCED;
				/* 0x02 = BIS同期失敗 */
				notify_bis_sync_state(0x02);
			}

			receiver_state &= ~AC_STATE_PA_SYNCED;
			pa_sync_handle = NULL;

			notify_state_transition();
			command_busy = false;
			break;

		default:
			break;
		}
	}
}

static void bt_mgmt_msg_sub_thread(void)
{
	int ret;
	const struct zbus_channel *chan;

	while (1) {
		struct bt_mgmt_msg msg;

		ret = zbus_sub_wait_msg(&bt_mgmt_evt_sub, &chan, &msg, K_FOREVER);
		ERR_CHK(ret);

		switch (msg.event) {
		case BT_MGMT_CONNECTED:
			LOG_INF("BLE connected");
			current_conn = bt_conn_ref(msg.conn);
			break;

		case BT_MGMT_DISCONNECTED:
			LOG_INF("BLE disconnected");
			if (current_conn) {
				bt_conn_unref(current_conn);
				current_conn = NULL;
			}
			pa_data_notify_enabled = false;
			break;

		case BT_MGMT_SECURITY_CHANGED:
			LOG_INF("Security changed");
			break;

		default:
			break;
		}
	}
}

static int zbus_subscribers_create(void)
{
	int ret;

	button_msg_sub_thread_id = k_thread_create(
		&button_msg_sub_thread_data, button_msg_sub_thread_stack,
		CONFIG_BUTTON_MSG_SUB_STACK_SIZE,
		(k_thread_entry_t)button_msg_sub_thread,
		NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_BUTTON_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(button_msg_sub_thread_id, "BUTTON_MSG_SUB");
	if (ret) {
		return ret;
	}

	le_audio_msg_sub_thread_id = k_thread_create(
		&le_audio_msg_sub_thread_data, le_audio_msg_sub_thread_stack,
		CONFIG_LE_AUDIO_MSG_SUB_STACK_SIZE,
		(k_thread_entry_t)le_audio_msg_sub_thread,
		NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_LE_AUDIO_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(le_audio_msg_sub_thread_id, "LE_AUDIO_MSG_SUB");
	if (ret) {
		return ret;
	}

	bt_mgmt_msg_sub_thread_id = k_thread_create(
		&bt_mgmt_msg_sub_thread_data, bt_mgmt_msg_sub_thread_stack,
		CONFIG_BT_MGMT_MSG_SUB_STACK_SIZE,
		(k_thread_entry_t)bt_mgmt_msg_sub_thread,
		NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_BT_MGMT_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(bt_mgmt_msg_sub_thread_id, "BT_MGMT_MSG_SUB");
	if (ret) {
		return ret;
	}

	return 0;
}

static int zbus_link_producers_observers(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_ZBUS)) {
		return -ENOTSUP;
	}

	ret = zbus_chan_add_obs(&button_chan, &button_evt_sub,
			       ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		return ret;
	}

	ret = zbus_chan_add_obs(&le_audio_chan, &le_audio_evt_sub,
			       ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		return ret;
	}

	ret = zbus_chan_add_obs(&bt_mgmt_chan, &bt_mgmt_evt_sub,
			       ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		return ret;
	}

	return 0;
}

/* ===== Stubs required by audio_system ===== */

uint8_t stream_state_get(void)
{
	return strm_state;
}

void streamctrl_send(struct net_buf const *const audio_frame)
{
	ARG_UNUSED(audio_frame);
}

/* ===== Entry point ===== */

int main(void)
{
	int ret;

	LOG_INF("Auracast Receiver starting");

	ret = peripherals_init();
	ERR_CHK(ret);

	ret = fw_info_app_print();
	ERR_CHK(ret);

	ret = bt_mgmt_init();
	ERR_CHK(ret);

	ret = audio_system_init();
	ERR_CHK(ret);

	bt_conn_auth_info_cb_register(&auth_info_cbs);
	bt_le_per_adv_sync_cb_register(&pa_sync_cbs);
	bt_le_scan_cb_register(&scan_cbs);

	auracast_gatt_set_cmd_cb(gatt_cmd_handler);

	ret = zbus_subscribers_create();
	ERR_CHK_MSG(ret, "Failed to create zbus subscriber threads");

	ret = zbus_link_producers_observers();
	ERR_CHK_MSG(ret, "Failed to link zbus producers and observers");

	ret = le_audio_rx_init();
	ERR_CHK_MSG(ret, "Failed to initialize rx path");

	ret = broadcast_sink_enable(le_audio_rx_data_handler);
	ERR_CHK_MSG(ret, "Failed to enable broadcast sink");

	ret = start_advertising();
	ERR_CHK_MSG(ret, "Failed to start advertising");

	LOG_INF("Auracast Receiver ready");

	return 0;
}
