/*
 * Auracast Receiver — PA受信機.xlsx spec implementation
 *
 * GATT service with TOSHIBA Auracast Service UUIDs.
 * HID stub for iOS Settings Bluetooth discovery.
 * Auto-scan for NRF5340_BROADCASTER after pairing.
 */

#include "streamctrl.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/byteorder.h>

#include "broadcast_sink.h"
#include "zbus_common.h"
#include "peripherals.h"
#include "led_assignments.h"
#include "led.h"
#include "button_assignments.h"
#include "macros_common.h"
#include "audio_system.h"
#include "bt_mgmt.h"
#include "bt_rendering_and_capture.h"
#include "audio_datapath.h"
#include "le_audio_rx.h"
#include "fw_info_app.h"
#include "pa_protocol.h"
#include "auracast_gatt.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_BT_AUDIO_CONCURRENT_RX_STREAMS_MAX <= CONFIG_AUDIO_DECODE_CHANNELS_MAX);

#define BT_DATA_VS 0xFF

/* ===== Receiver state ===== */

static uint8_t receiver_state;
static bool pa_data_notify_enabled;
static struct bt_conn *current_conn;
static bool paired;

static uint32_t last_broadcast_id = BRDCAST_ID_NOT_USED;

static void notify_state_transition(void)
{
	if (!current_conn) {
		return;
	}
	uint8_t state = receiver_state;

	auracast_gatt_send_response(current_conn, AC_OP_STATE_TRANSITION_NTF,
				    &state, 1);
}

/* ===== Legacy advertising (HID for iOS) ===== */

static struct bt_le_ext_adv *legacy_adv;

static int start_advertising(void)
{
	int ret;
	static const struct bt_le_adv_param legacy_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0,
		.options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
	};
	static const uint8_t ad_flags[] = {
		BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
	};
	static const uint8_t hid_uuid[] = { BT_UUID_16_ENCODE(0x1812) };
	static const uint8_t appearance[] = { 0xC1, 0x03 };
	const struct bt_data ad[] = {
		BT_DATA(BT_DATA_FLAGS, ad_flags, sizeof(ad_flags)),
		BT_DATA(BT_DATA_UUID16_ALL, hid_uuid, sizeof(hid_uuid)),
		BT_DATA(BT_DATA_GAP_APPEARANCE, appearance, sizeof(appearance)),
		BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
			sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	};

	if (!legacy_adv) {
		ret = bt_le_ext_adv_create(&legacy_param, NULL, &legacy_adv);
		if (ret) {
			LOG_ERR("Failed to create adv set: %d", ret);
			return ret;
		}
	}

	ret = bt_le_ext_adv_set_data(legacy_adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		LOG_ERR("Failed to set adv data: %d", ret);
		return ret;
	}

	ret = bt_le_ext_adv_start(legacy_adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to start advertising: %d", ret);
		return ret;
	}

	LOG_INF("Advertising started as \"%s\"", CONFIG_BT_DEVICE_NAME);
	return 0;
}

/* ===== Auto-scan after pairing ===== */

static void start_auto_scan(void)
{
	int ret;

	ret = bt_mgmt_scan_start(0, 0, BT_MGMT_SCAN_TYPE_BROADCAST,
				 CONFIG_BT_AUDIO_BROADCAST_NAME, BRDCAST_ID_NOT_USED);
	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to start scan: %d", ret);
		return;
	}

	receiver_state |= AC_STATE_SEARCHING;
	LOG_INF("Auto-scan started for \"%s\"", CONFIG_BT_AUDIO_BROADCAST_NAME);
	notify_state_transition();
}

/* ===== PA data receive ===== */

static uint8_t prev_pa_data[PA_MAX_DATA_SIZE];
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
							     data->data, data->data_len);
			if (err && err != -ENOTCONN) {
				LOG_WRN("PA data notify failed: %d", err);
			}
		}
	}

	return false;
}

static void pa_recv_cb(struct bt_le_per_adv_sync *sync,
		       const struct bt_le_per_adv_sync_recv_info *info,
		       struct net_buf_simple *buf)
{
	bt_data_parse(buf, pa_data_parse_cb, NULL);
}

static struct bt_le_per_adv_sync_cb pa_data_sync_cbs = {
	.recv = pa_recv_cb,
};

/* ===== GATT command handler ===== */

static void handle_device_info_req(struct bt_conn *conn)
{
	uint8_t rsp[128];
	uint16_t pos = 0;

	/* Vendor (type=0x00) */
	const char *vendor = "Nordic";
	rsp[pos++] = 0x00;
	rsp[pos++] = strlen(vendor);
	memcpy(&rsp[pos], vendor, strlen(vendor));
	pos += strlen(vendor);

	/* Chip (type=0x01) */
	const char *chip = "nRF5340";
	rsp[pos++] = 0x01;
	rsp[pos++] = strlen(chip);
	memcpy(&rsp[pos], chip, strlen(chip));
	pos += strlen(chip);

	/* FW version (type=0x02) */
	const char *fw_ver = "1.0.0";
	rsp[pos++] = 0x02;
	rsp[pos++] = strlen(fw_ver);
	memcpy(&rsp[pos], fw_ver, strlen(fw_ver));
	pos += strlen(fw_ver);

	/* Audio Location (type=0x03) */
	rsp[pos++] = 0x03;
	rsp[pos++] = 0x04;
	sys_put_be32(0x00000003, &rsp[pos]); /* Front Left & Front Right */
	pos += 4;

	auracast_gatt_send_response(conn, AC_OP_DEVICE_INFO_RSP, rsp, pos);
}

static void handle_search_start_req(struct bt_conn *conn)
{
	if (receiver_state & AC_STATE_SEARCHING) {
		uint8_t result = AC_RESULT_FAIL;

		auracast_gatt_send_response(conn, AC_OP_SEARCH_START_RSP, &result, 1);
		return;
	}

	int ret = bt_mgmt_scan_start(0, 0, BT_MGMT_SCAN_TYPE_BROADCAST,
				     CONFIG_BT_AUDIO_BROADCAST_NAME, BRDCAST_ID_NOT_USED);

	uint8_t result = (ret == 0 || ret == -EALREADY) ? AC_RESULT_SUCCESS : AC_RESULT_FAIL;

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

		auracast_gatt_send_response(conn, AC_OP_SEARCH_STOP_RSP, &result, 1);
		return;
	}

	bt_le_scan_stop();

	uint8_t result = AC_RESULT_SUCCESS;

	auracast_gatt_send_response(conn, AC_OP_SEARCH_STOP_RSP, &result, 1);

	receiver_state &= ~AC_STATE_SEARCHING;
	notify_state_transition();
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

	auracast_gatt_send_response(conn, AC_OP_PA_DATA_NTF_START_RSP, &result, 1);
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

	auracast_gatt_send_response(conn, AC_OP_PA_DATA_NTF_STOP_RSP, &result, 1);
}

static void handle_stub_cmd(struct bt_conn *conn, uint16_t opcode)
{
	uint16_t rsp_opcode = (opcode & 0x00FF) | 0x0800;
	uint8_t result = AC_RESULT_FAIL;

	LOG_WRN("Stub command 0x%04X", opcode);
	auracast_gatt_send_response(conn, rsp_opcode, &result, 1);
}

static void gatt_cmd_handler(struct bt_conn *conn, uint16_t opcode,
			     const uint8_t *params, uint16_t param_len)
{
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
	case AC_OP_PA_DATA_NTF_START_REQ:
		handle_pa_data_ntf_start_req(conn);
		break;
	case AC_OP_PA_DATA_NTF_STOP_REQ:
		handle_pa_data_ntf_stop_req(conn);
		break;
	case AC_OP_PA_SYNC_START_REQ:
	case AC_OP_PA_SYNC_RELEASE_REQ:
	case AC_OP_BIS_SYNC_START_REQ:
	case AC_OP_BIS_SYNC_STOP_REQ:
	case AC_OP_VOLUME_SET_REQ:
	case AC_OP_MUTE_SET_REQ:
	case AC_OP_VOLUME_GET_REQ:
	case AC_OP_AUDIO_OUTPUT_REQ:
		handle_stub_cmd(conn, opcode);
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
	paired = true;
	start_auto_scan();
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_info_cb auth_info_cbs = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ===== zbus event handling (kept from original) ===== */

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

static enum stream_state strm_state = STATE_PAUSED;

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
		case LE_AUDIO_EVT_SYNC_LOST:
			LOG_INF("Sync lost");

			ret = bt_mgmt_pa_sync_delete(msg.pa_sync);
			if (ret) {
				LOG_WRN("Failed to delete PA sync");
			}

			pa_data_received_once = false;
			pa_data_notify_enabled = false;
			receiver_state &= ~(AC_STATE_PA_SYNCED | AC_STATE_BIS_SYNCED);
			notify_state_transition();

			/* Restart scan */
			if (paired) {
				start_auto_scan();
			}
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
			LOG_INF("BLE Connected (bt_mgmt)");
			current_conn = bt_conn_ref(msg.conn);
			bt_conn_set_security(current_conn, BT_SECURITY_L2);
			break;

		case BT_MGMT_DISCONNECTED:
			LOG_INF("BLE Disconnected (bt_mgmt)");
			if (current_conn) {
				bt_conn_unref(current_conn);
				current_conn = NULL;
			}
			pa_data_notify_enabled = false;
			start_advertising();
			break;

		case BT_MGMT_SECURITY_CHANGED:
			LOG_INF("Security changed");
			if (!paired) {
				paired = true;
				start_auto_scan();
			}
			break;

		case BT_MGMT_PA_SYNCED:
			LOG_INF("PA synced");
			pa_data_received_once = false;
			receiver_state &= ~AC_STATE_SEARCHING;
			receiver_state |= AC_STATE_PA_SYNCED;
			notify_state_transition();

			ret = broadcast_sink_pa_sync_set(msg.pa_sync, msg.broadcast_id);
			if (ret) {
				last_broadcast_id = BRDCAST_ID_NOT_USED;
				LOG_WRN("Failed to set PA sync");
			} else {
				last_broadcast_id = msg.broadcast_id;
			}
			break;

		case BT_MGMT_PA_SYNC_LOST:
			LOG_INF("PA sync lost");
			pa_data_received_once = false;
			pa_data_notify_enabled = false;
			receiver_state &= ~(AC_STATE_PA_SYNCED | AC_STATE_BIS_SYNCED);
			notify_state_transition();

			if (paired) {
				start_auto_scan();
			}
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
		CONFIG_BUTTON_MSG_SUB_STACK_SIZE, (k_thread_entry_t)button_msg_sub_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(CONFIG_BUTTON_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(button_msg_sub_thread_id, "BUTTON_MSG_SUB");
	if (ret) {
		return ret;
	}

	le_audio_msg_sub_thread_id = k_thread_create(
		&le_audio_msg_sub_thread_data, le_audio_msg_sub_thread_stack,
		CONFIG_LE_AUDIO_MSG_SUB_STACK_SIZE, (k_thread_entry_t)le_audio_msg_sub_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(CONFIG_LE_AUDIO_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(le_audio_msg_sub_thread_id, "LE_AUDIO_MSG_SUB");
	if (ret) {
		return ret;
	}

	bt_mgmt_msg_sub_thread_id = k_thread_create(
		&bt_mgmt_msg_sub_thread_data, bt_mgmt_msg_sub_thread_stack,
		CONFIG_BT_MGMT_MSG_SUB_STACK_SIZE, (k_thread_entry_t)bt_mgmt_msg_sub_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(CONFIG_BT_MGMT_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
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

	ret = zbus_chan_add_obs(&button_chan, &button_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		return ret;
	}

	ret = zbus_chan_add_obs(&le_audio_chan, &le_audio_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_BOARD_NRF5340_AUDIO_DK_NRF5340_CPUAPP)) {
		ret = zbus_chan_add_obs(&volume_chan, &volume_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
		if (ret) {
			return ret;
		}
	}

	ret = zbus_chan_add_obs(&bt_mgmt_chan, &bt_mgmt_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
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

	bt_le_per_adv_sync_cb_register(&pa_data_sync_cbs);

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

	LOG_INF("Auracast Receiver ready, waiting for iOS pairing...");

	return 0;
}
