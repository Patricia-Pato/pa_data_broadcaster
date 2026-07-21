#ifndef AURACAST_GATT_H
#define AURACAST_GATT_H

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

/* OpCodes - Request (from app) */
#define AC_OP_DEVICE_INFO_REQ    0x0001
#define AC_OP_SEARCH_START_REQ   0x0002
#define AC_OP_SEARCH_STOP_REQ    0x0003
#define AC_OP_PA_SYNC_START_REQ  0x0004
#define AC_OP_PA_SYNC_RELEASE_REQ 0x0005
#define AC_OP_BIS_SYNC_START_REQ 0x0006
#define AC_OP_BIS_SYNC_STOP_REQ  0x0007
#define AC_OP_PA_DATA_NTF_START_REQ 0x0008
#define AC_OP_PA_DATA_NTF_STOP_REQ  0x0009
#define AC_OP_VOLUME_SET_REQ     0x000A
#define AC_OP_MUTE_SET_REQ       0x000B
#define AC_OP_VOLUME_GET_REQ     0x000C
#define AC_OP_AUDIO_OUTPUT_REQ   0x000D

/* OpCodes - Response (to app) */
#define AC_OP_DEVICE_INFO_RSP    0x0801
#define AC_OP_SEARCH_START_RSP   0x0802
#define AC_OP_SEARCH_STOP_RSP    0x0803
#define AC_OP_PA_SYNC_START_RSP  0x0804
#define AC_OP_PA_SYNC_RELEASE_RSP 0x0805
#define AC_OP_BIS_SYNC_START_RSP 0x0806
#define AC_OP_BIS_SYNC_STOP_RSP  0x0807
#define AC_OP_PA_DATA_NTF_START_RSP 0x0808
#define AC_OP_PA_DATA_NTF_STOP_RSP  0x0809
#define AC_OP_VOLUME_SET_RSP     0x080A
#define AC_OP_MUTE_SET_RSP       0x080B
#define AC_OP_VOLUME_GET_RSP     0x080C
#define AC_OP_AUDIO_OUTPUT_RSP   0x080D

/* OpCodes - Notification (to app) */
#define AC_OP_SEARCH_RESULT_NTF  0x0C01
#define AC_OP_PA_SYNC_STATE_NTF  0x0C02
#define AC_OP_BIS_SYNC_STATE_NTF 0x0C03
#define AC_OP_PA_DATA_NTF        0x0C04
#define AC_OP_VOLUME_CHANGE_NTF  0x0C05
#define AC_OP_STATE_TRANSITION_NTF 0x0C06

/* Result codes */
#define AC_RESULT_SUCCESS 0x00
#define AC_RESULT_FAIL    0x01

/* State bits for state transition notification */
#define AC_STATE_SEARCHING   BIT(0)
#define AC_STATE_PA_SYNCED   BIT(1)
#define AC_STATE_BIS_SYNCED  BIT(2)

/* Callback: invoked when a command is received from the app */
typedef void (*auracast_gatt_cmd_cb_t)(struct bt_conn *conn, uint16_t opcode,
				       const uint8_t *params, uint16_t param_len);

void auracast_gatt_set_cmd_cb(auracast_gatt_cmd_cb_t cb);

/* Send command response via response/notification characteristic */
int auracast_gatt_send_response(struct bt_conn *conn,
				uint16_t opcode, const uint8_t *params,
				uint16_t param_len);

/* Send PA data notification */
int auracast_gatt_send_pa_data(struct bt_conn *conn,
			       const uint8_t *data, uint16_t len);

#endif /* AURACAST_GATT_H */
