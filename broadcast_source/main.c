/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "streamctrl.h"

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/byteorder.h>

#include "broadcast_source.h"
#include "zbus_common.h"
#include "peripherals.h"
#include "macros_common.h"
#include "audio_system.h"
#include "bt_mgmt.h"
#include "fw_info_app.h"

/* PA service (hexagonal architecture) */
#include "pa_service.h"
#include "port_led.h"
#include "port_button.h"
#include "zephyr_uart_adapter.h"
#include "zephyr_ble_pa_adapter.h"
#include "zephyr_timer_adapter.h"
#include "zephyr_led_adapter.h"
#include "zephyr_button_adapter.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_BT_AUDIO_CONCURRENT_TX_STREAMS_MAX <= CONFIG_AUDIO_ENCODE_CHANNELS_MAX);

ZBUS_MSG_SUBSCRIBER_DEFINE(le_audio_evt_sub);

ZBUS_CHAN_DECLARE(le_audio_chan);
ZBUS_CHAN_DECLARE(bt_mgmt_chan);
ZBUS_CHAN_DECLARE(sdu_ref_chan);

ZBUS_OBS_DECLARE(sdu_ref_msg_listen);

static struct k_thread le_audio_msg_sub_thread_data;
static k_tid_t le_audio_msg_sub_thread_id;

struct bt_le_ext_adv *ext_adv;

K_THREAD_STACK_DEFINE(le_audio_msg_sub_thread_stack, CONFIG_LE_AUDIO_MSG_SUB_STACK_SIZE);

static enum stream_state strm_state = STATE_PAUSED;

/* Buffer for the UUIDs. */
#define EXT_ADV_UUID_BUF_SIZE (128)
NET_BUF_SIMPLE_DEFINE_STATIC(uuid_data, EXT_ADV_UUID_BUF_SIZE);
NET_BUF_SIMPLE_DEFINE_STATIC(uuid_data2, EXT_ADV_UUID_BUF_SIZE);

/* Buffer for periodic advertising BASE data. */
NET_BUF_SIMPLE_DEFINE_STATIC(base_data, 128);
NET_BUF_SIMPLE_DEFINE_STATIC(base_data2, 128);

/* Extended advertising buffer. */
static struct bt_data ext_adv_buf[CONFIG_BT_ISO_MAX_BIG][CONFIG_EXT_ADV_BUF_MAX];

/* Periodic advertising buffer. */
static struct bt_data per_adv_buf[CONFIG_BT_ISO_MAX_BIG];

#if (CONFIG_AURACAST)
#define BROADCAST_SRC_PBA_BUF_SIZE                                                                 \
	(BROADCAST_SOURCE_PBA_HEADER_SIZE + CONFIG_BT_AUDIO_BROADCAST_PBA_METADATA_SIZE)

#define BROADCAST_SOURCE_PBA_METADATA_VACANT                                                       \
	(CONFIG_BT_AUDIO_BROADCAST_PBA_METADATA_SIZE / (sizeof(struct bt_data)))

uint8_t pba_data[CONFIG_BT_ISO_MAX_BIG][BROADCAST_SRC_PBA_BUF_SIZE];

static struct broadcast_source_ext_adv_data ext_adv_data[] = {
	{.uuid_buf = &uuid_data,
	 .pba_metadata_vacant_cnt = BROADCAST_SOURCE_PBA_METADATA_VACANT,
	 .pba_buf = pba_data[0]},
	{.uuid_buf = &uuid_data2,
	 .pba_metadata_vacant_cnt = BROADCAST_SOURCE_PBA_METADATA_VACANT,
	 .pba_buf = pba_data[1]}};
#else
static struct broadcast_source_ext_adv_data ext_adv_data[] = {{.uuid_buf = &uuid_data},
							      {.uuid_buf = &uuid_data2}};
#endif /* (CONFIG_AURACAST) */

static struct broadcast_source_per_adv_data per_adv_data[] = {{.base_buf = &base_data},
							      {.base_buf = &base_data2}};

/* ===== PA Service instances (hexagonal architecture) ===== */
static struct zephyr_uart_adapter_state uart_adapter_state;
static struct zephyr_ble_pa_adapter_state ble_pa_adapter_state;
static struct zephyr_timer_adapter_state timer_adapter_state;
static struct zephyr_button_adapter_state button_adapter_state;

static struct pa_port_uart uart_port;
static struct pa_port_ble_pa ble_pa_port;
static struct pa_port_timer timer_port;
static struct pa_port_led led_port;
static struct pa_port_button button_port;

static struct pa_service pa_svc;

/* Overhead from BASE AD element in periodic advertising */
#define PA_BAAS_OVERHEAD 0

static void stream_state_set(enum stream_state stream_state_new)
{
	strm_state = stream_state_new;
}

/**
 * @brief	Handle button events via port abstraction.
 */
static void button_event_handler(enum pa_button_id button, enum pa_button_action action,
				 void *user_data)
{
	int ret;

	(void)user_data;

	switch (button) {
	case PA_BUTTON_PLAY_PAUSE:
	case PA_BUTTON_VOLUME_DOWN:
		if (strm_state == STATE_STREAMING) {
			ret = broadcast_source_stop(0);
			if (ret) {
				LOG_WRN("Failed to stop broadcaster: %d", ret);
			}
		} else if (strm_state == STATE_PAUSED) {
			ret = broadcast_source_start(0, ext_adv);
			if (ret) {
				LOG_WRN("Failed to start broadcaster: %d", ret);
			}
		} else {
			LOG_WRN("In invalid state: %d", strm_state);
		}
		break;

	case PA_BUTTON_ACTION:
	case PA_BUTTON_VOLUME_UP:
		if (IS_ENABLED(CONFIG_AUDIO_TEST_TONE)) {
			if (strm_state != STATE_STREAMING) {
				LOG_WRN("Not in streaming state");
				break;
			}

			ret = audio_system_encode_test_tone_step();
			if (ret) {
				LOG_WRN("Failed to play test tone, ret: %d", ret);
			}
		}
		break;

	default:
		LOG_WRN("Unexpected button: %d", button);
		break;
	}
}

/**
 * @brief	Handle Bluetooth LE audio events.
 */
static void le_audio_msg_sub_thread(void)
{
	int ret;
	const struct zbus_channel *chan;

	while (1) {
		struct le_audio_msg msg;

		ret = zbus_sub_wait_msg(&le_audio_evt_sub, &chan, &msg, K_FOREVER);
		ERR_CHK(ret);

		LOG_DBG("Received event = %d, current state = %d", msg.event, strm_state);

		switch (msg.event) {
		case LE_AUDIO_EVT_STREAMING:
			LOG_DBG("LE audio evt streaming");

			audio_system_encoder_start();

			if (strm_state == STATE_STREAMING) {
				LOG_DBG("Got streaming event in streaming state");
				break;
			}

			audio_system_start();
			stream_state_set(STATE_STREAMING);
			led_port.blink(led_port.ctx, PA_LED_CONN_STATUS);
			break;

		case LE_AUDIO_EVT_NOT_STREAMING:
			LOG_DBG("LE audio evt not_streaming");

			audio_system_encoder_stop();

			if (strm_state == STATE_PAUSED) {
				LOG_DBG("Got not_streaming event in paused state");
				break;
			}

			stream_state_set(STATE_PAUSED);
			audio_system_stop();
			led_port.on(led_port.ctx, PA_LED_CONN_STATUS);
			break;

		case LE_AUDIO_EVT_STREAM_SENT:
			break;

		default:
			LOG_WRN("Unexpected/unhandled le_audio event: %d", msg.event);
			break;
		}

		STACK_USAGE_PRINT("le_audio_msg_thread", &le_audio_msg_sub_thread_data);
	}
}

static int zbus_subscribers_create(void)
{
	int ret;

	le_audio_msg_sub_thread_id = k_thread_create(
		&le_audio_msg_sub_thread_data, le_audio_msg_sub_thread_stack,
		CONFIG_LE_AUDIO_MSG_SUB_STACK_SIZE, (k_thread_entry_t)le_audio_msg_sub_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(CONFIG_LE_AUDIO_MSG_SUB_THREAD_PRIO), 0, K_NO_WAIT);
	ret = k_thread_name_set(le_audio_msg_sub_thread_id, "LE_AUDIO_MSG_SUB");
	if (ret) {
		LOG_ERR("Failed to create le_audio_msg thread");
		return ret;
	}

	ret = zbus_chan_add_obs(&sdu_ref_chan, &sdu_ref_msg_listen, ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("Failed to add timestamp listener");
		return ret;
	}

	return 0;
}

static void bt_mgmt_evt_handler(const struct zbus_channel *chan)
{
	int ret;
	const struct bt_mgmt_msg *msg;

	msg = zbus_chan_const_msg(chan);

	switch (msg->event) {
	case BT_MGMT_EXT_ADV_WITH_PA_READY:
		LOG_INF("Ext adv ready");

		ext_adv = msg->ext_adv;

		/* Notify PA adapter that ext_adv is available */
		zephyr_ble_pa_adapter_set_ext_adv(&ble_pa_adapter_state, ext_adv);

		ret = broadcast_source_start(msg->index, ext_adv);
		if (ret) {
			LOG_ERR("Failed to start broadcaster: %d", ret);
		}

		break;

	default:
		LOG_WRN("Unexpected/unhandled bt_mgmt event: %d", msg->event);
		break;
	}
}

ZBUS_LISTENER_DEFINE(bt_mgmt_evt_listen, bt_mgmt_evt_handler);

static int zbus_link_producers_observers(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_ZBUS)) {
		return -ENOTSUP;
	}

	ret = zbus_chan_add_obs(&le_audio_chan, &le_audio_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("Failed to add le_audio sub");
		return ret;
	}

	ret = zbus_chan_add_obs(&bt_mgmt_chan, &bt_mgmt_evt_listen, ZBUS_ADD_OBS_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("Failed to add bt_mgmt listener");
		return ret;
	}

	return 0;
}

static int ext_adv_populate(uint8_t big_index, struct broadcast_source_ext_adv_data *ext_adv_data,
			    struct bt_data *ext_adv_buf, size_t ext_adv_buf_size,
			    size_t *ext_adv_count)
{
	int ret;
	size_t ext_adv_buf_cnt = 0;

	if (IS_ENABLED(CONFIG_BT_AUDIO_USE_BROADCAST_NAME_ALT)) {
		if (sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME_ALT) >
		    ARRAY_SIZE(ext_adv_data->brdcst_name_buf)) {
			LOG_ERR("CONFIG_BT_AUDIO_BROADCAST_NAME_ALT is too long");
			return -EINVAL;
		}

		size_t brdcst_name_size = sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME_ALT) - 1;

		memcpy(ext_adv_data->brdcst_name_buf, CONFIG_BT_AUDIO_BROADCAST_NAME_ALT,
		       brdcst_name_size);
	} else {
		if (sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME) >
		    ARRAY_SIZE(ext_adv_data->brdcst_name_buf)) {
			LOG_ERR("CONFIG_BT_AUDIO_BROADCAST_NAME is too long");
			return -EINVAL;
		}

		size_t brdcst_name_size = sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME) - 1;

		memcpy(ext_adv_data->brdcst_name_buf, CONFIG_BT_AUDIO_BROADCAST_NAME,
		       brdcst_name_size);
	}

	ext_adv_buf[ext_adv_buf_cnt].type = BT_DATA_UUID16_ALL;
	ext_adv_buf[ext_adv_buf_cnt].data = ext_adv_data->uuid_buf->data;
	ext_adv_buf_cnt++;

	ret = bt_mgmt_manufacturer_uuid_populate(ext_adv_data->uuid_buf,
						 CONFIG_BT_DEVICE_MANUFACTURER_ID);
	if (ret) {
		LOG_ERR("Failed to add adv data with manufacturer ID: %d", ret);
		return ret;
	}

	uint32_t broadcast_id = 0x000000;
	bool fixed_id = !IS_ENABLED(CONFIG_BT_AUDIO_USE_BROADCAST_ID_RANDOM);

	if (IS_ENABLED(CONFIG_BT_AUDIO_USE_BROADCAST_ID_FICR)) {
		LOG_DBG("Using FICR ID");
		broadcast_id = NRF_FICR->NFC.TAGHEADER0 >> 8;
	} else if (IS_ENABLED(CONFIG_BT_AUDIO_USE_BROADCAST_ID_FIXED)) {
		LOG_DBG("Using fixed ID");
		broadcast_id = CONFIG_BT_AUDIO_BROADCAST_ID_FIXED;
	} else {
		LOG_DBG("Using random broadcast ID");
	}

	ret = broadcast_source_ext_adv_populate(big_index, fixed_id, broadcast_id, ext_adv_data,
						&ext_adv_buf[ext_adv_buf_cnt],
						ext_adv_buf_size - ext_adv_buf_cnt);
	if (ret < 0) {
		LOG_ERR("Failed to add ext adv data for broadcast source: %d", ret);
		return ret;
	}

	ext_adv_buf_cnt += ret;

	ext_adv_buf[0].data_len = ext_adv_data->uuid_buf->len;

	LOG_DBG("Size of adv data: %d, num_elements: %d", sizeof(struct bt_data) * ext_adv_buf_cnt,
		ext_adv_buf_cnt);

	*ext_adv_count = ext_adv_buf_cnt;

	return 0;
}

static int per_adv_populate(uint8_t big_index, struct broadcast_source_per_adv_data *pre_adv_data,
			    struct bt_data *per_adv_buf, size_t per_adv_buf_size,
			    size_t *per_adv_count)
{
	int ret;
	size_t per_adv_buf_cnt = 0;

	ret = broadcast_source_per_adv_populate(big_index, pre_adv_data, per_adv_buf,
						per_adv_buf_size - per_adv_buf_cnt);
	if (ret < 0) {
		LOG_ERR("Failed to add per adv data for broadcast source: %d", ret);
		return ret;
	}

	per_adv_buf_cnt += ret;

	LOG_DBG("Size of per adv data: %d, num_elements: %d",
		sizeof(struct bt_data) * per_adv_buf_cnt, per_adv_buf_cnt);

	*per_adv_count = per_adv_buf_cnt;

	return 0;
}

uint8_t stream_state_get(void)
{
	return strm_state;
}

void streamctrl_send(struct net_buf const *const audio_frame)
{
	int ret;
	static int prev_ret;

	if (strm_state == STATE_STREAMING) {
		ret = broadcast_source_send(audio_frame, 0, 0);

		if (ret != 0 && ret != prev_ret) {
			if (ret == -ECANCELED) {
				LOG_WRN("Sending operation cancelled");
			} else {
				LOG_WRN("Problem with sending LE audio data, ret: %d", ret);
			}
		}

		prev_ret = ret;
	}
}

int main(void)
{
	int ret;
	static struct broadcast_source_big broadcast_param;

	LOG_DBG("Main started");

	size_t ext_adv_buf_cnt = 0;
	size_t per_adv_buf_cnt = 0;

	ret = peripherals_init();
	ERR_CHK(ret);

	ret = fw_info_app_print();
	ERR_CHK(ret);

	ret = bt_mgmt_init();
	ERR_CHK(ret);

	ret = audio_system_init();
	ERR_CHK(ret);

	/* Initialize PA service (hexagonal architecture DI wiring) */
	uart_port = zephyr_uart_adapter_create(&uart_adapter_state);
	ble_pa_port = zephyr_ble_pa_adapter_create(&ble_pa_adapter_state);
	timer_port = zephyr_timer_adapter_create(&timer_adapter_state);
	led_port = zephyr_led_adapter_create();
	button_port = zephyr_button_adapter_create(&button_adapter_state);

	ret = pa_service_init(&pa_svc, &uart_port, &ble_pa_port, &timer_port, PA_BAAS_OVERHEAD);
	ERR_CHK_MSG(ret, "Failed to initialize PA service");

	ret = button_port.init(button_port.ctx, button_event_handler, NULL);
	ERR_CHK_MSG(ret, "Failed to initialize button port");

	ret = zbus_subscribers_create();
	ERR_CHK_MSG(ret, "Failed to create zbus subscriber threads");

	ret = zbus_link_producers_observers();
	ERR_CHK_MSG(ret, "Failed to link zbus producers and observers");

	broadcast_source_default_create(&broadcast_param);

	ret = broadcast_source_enable(&broadcast_param, 0);
	ERR_CHK_MSG(ret, "Failed to enable broadcaster(s)");

	ret = audio_system_config_set(
		bt_audio_codec_cfg_freq_to_freq_hz(CONFIG_BT_AUDIO_PREF_SAMPLE_RATE_VALUE),
		CONFIG_BT_AUDIO_BITRATE_BROADCAST_SRC, VALUE_NOT_SET);
	ERR_CHK_MSG(ret, "Failed to set sample- and bitrate");

	ret = ext_adv_populate(0, &ext_adv_data[0], ext_adv_buf[0], ARRAY_SIZE(ext_adv_buf[0]),
			       &ext_adv_buf_cnt);
	ERR_CHK(ret);

	ret = per_adv_populate(0, &per_adv_data[0], &per_adv_buf[0], 1, &per_adv_buf_cnt);
	ERR_CHK(ret);

	/* Store reference to base PA data for the adapter */
	zephyr_ble_pa_adapter_set_base_per_adv(&ble_pa_adapter_state,
					       &per_adv_buf[0], per_adv_buf_cnt,
					       per_adv_buf[0].data_len + 2);

	ret = bt_mgmt_adv_start(0, ext_adv_buf[0], ext_adv_buf_cnt, &per_adv_buf[0],
				per_adv_buf_cnt, false);
	ERR_CHK_MSG(ret, "Failed to start first advertiser");

	return 0;
}
