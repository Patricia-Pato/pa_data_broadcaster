#ifndef PA_CMD_PARSER_H
#define PA_CMD_PARSER_H

#include "pa_protocol.h"

/**
 * Parse a raw UART frame into opcode and payload.
 *
 * Frame format: Command_Length(1) + OpCode(1) + [payload...]
 * Command_Length = total bytes after Command_Length field itself.
 *
 * @param buf     Raw bytes starting with Command_Length
 * @param len     Number of bytes in buf
 * @param opcode  [out] Parsed opcode
 * @param payload [out] Parsed payload (caller provides storage)
 * @return 0 on success, negative on error (-1 = too short, -2 = length mismatch)
 */
int pa_cmd_parse(const uint8_t *buf, size_t len,
		 uint8_t *opcode, union pa_cmd_payload *payload);

/**
 * Serialize PA Status Get Response.
 * @return Number of bytes written, or negative on error
 */
int pa_cmd_serialize_status_rsp(const struct pa_status_get_rsp *rsp,
				uint8_t *buf, size_t buf_size);

/**
 * Serialize PA Data Send Response.
 * @return Number of bytes written, or negative on error
 */
int pa_cmd_serialize_data_send_rsp(uint8_t result,
				   uint8_t *buf, size_t buf_size);

/**
 * Serialize PA Data Send Complete notification.
 * @return Number of bytes written, or negative on error
 */
int pa_cmd_serialize_data_send_complete(uint8_t *buf, size_t buf_size);

#endif /* PA_CMD_PARSER_H */
