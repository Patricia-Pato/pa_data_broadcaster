#include "pa_cmd_parser.h"
#include <string.h>

int pa_cmd_parse(const uint8_t *buf, size_t len,
		 uint8_t *opcode, union pa_cmd_payload *payload)
{
	if (buf == NULL || opcode == NULL || len < 2) {
		return -1;
	}

	uint8_t cmd_length = buf[0];

	if ((size_t)(cmd_length + 1) > len) {
		return -2;
	}

	*opcode = buf[1];

	switch (*opcode) {
	case PA_OPCODE_STATUS_GET_REQ:
		/* No payload */
		break;

	case PA_OPCODE_DATA_SEND_REQ:
		if (payload == NULL) {
			return -1;
		}
		/* Minimum: OpCode(1) + SID(1) + Data_Length(1) = 3 bytes in cmd_length */
		if (cmd_length < 3) {
			return -2;
		}
		payload->data_send_req.advertising_sid = buf[2];
		payload->data_send_req.data_length = buf[3];

		if (payload->data_send_req.data_length > PA_MAX_DATA_SIZE) {
			return -2;
		}
		if (cmd_length < (uint8_t)(3 + payload->data_send_req.data_length)) {
			return -2;
		}
		if (payload->data_send_req.data_length > 0) {
			memcpy(payload->data_send_req.data, &buf[4],
			       payload->data_send_req.data_length);
		}
		break;

	default:
		return -3;
	}

	return 0;
}

int pa_cmd_serialize_status_rsp(const struct pa_status_get_rsp *rsp,
				uint8_t *buf, size_t buf_size)
{
	if (rsp == NULL || buf == NULL) {
		return -1;
	}

	/* Command_Length(1) + OpCode(1) + Num_Advertising(1)
	 * + SID(N) + Available(N)
	 */
	size_t total = 1 + 1 + 1 + (rsp->num_advertising * 2);

	if (buf_size < total) {
		return -1;
	}

	size_t pos = 0;

	buf[pos++] = (uint8_t)(total - 1); /* Command_Length excludes itself */
	buf[pos++] = PA_OPCODE_STATUS_GET_RSP;
	buf[pos++] = rsp->num_advertising;

	for (uint8_t i = 0; i < rsp->num_advertising; i++) {
		buf[pos++] = rsp->adv_sid[i];
	}
	for (uint8_t i = 0; i < rsp->num_advertising; i++) {
		buf[pos++] = rsp->pa_available_bytes[i];
	}

	return (int)pos;
}

int pa_cmd_serialize_data_send_rsp(uint8_t result,
				   uint8_t *buf, size_t buf_size)
{
	if (buf == NULL || buf_size < 3) {
		return -1;
	}

	buf[0] = 0x02; /* Command_Length: OpCode(1) + Result(1) */
	buf[1] = PA_OPCODE_DATA_SEND_RSP;
	buf[2] = result;

	return 3;
}

int pa_cmd_serialize_data_send_complete(uint8_t *buf, size_t buf_size)
{
	if (buf == NULL || buf_size < 2) {
		return -1;
	}

	buf[0] = 0x01; /* Command_Length: OpCode(1) */
	buf[1] = PA_OPCODE_DATA_SEND_COMPLETE;

	return 2;
}
