#ifndef PA_PROTOCOL_H
#define PA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* OpCodes */
#define PA_OPCODE_STATUS_GET_REQ     0x00
#define PA_OPCODE_STATUS_GET_RSP     0x01
#define PA_OPCODE_DATA_SEND_REQ      0x10
#define PA_OPCODE_DATA_SEND_RSP      0x11
#define PA_OPCODE_DATA_SEND_COMPLETE 0x12

/* Result codes for Data Send Response (0x11) */
#define PA_RESULT_SUCCESS        0x00
#define PA_RESULT_BUSY           0x01
#define PA_RESULT_INVALID_SID    0x02
#define PA_RESULT_DATA_TOO_LARGE 0x03
#define PA_RESULT_NOT_READY      0x04

/* Platform-independent limits */
#define PA_MAX_ADV_SETS  4
#define PA_MAX_DATA_SIZE 252

/* Parsed command structures */
struct pa_status_get_rsp {
	uint8_t num_advertising;
	uint8_t adv_sid[PA_MAX_ADV_SETS];
	uint8_t pa_available_bytes[PA_MAX_ADV_SETS];
};

struct pa_data_send_req {
	uint8_t advertising_sid;
	uint8_t data_length;
	uint8_t data[PA_MAX_DATA_SIZE];
};

/* Union for parsed command output */
union pa_cmd_payload {
	struct pa_data_send_req data_send_req;
};

#endif /* PA_PROTOCOL_H */
