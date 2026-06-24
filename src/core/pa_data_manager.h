#ifndef PA_DATA_MANAGER_H
#define PA_DATA_MANAGER_H

#include "pa_protocol.h"

enum pa_data_state {
	PA_STATE_IDLE,
	PA_STATE_PENDING,
	PA_STATE_COMPLETE,
};

struct pa_data_context {
	enum pa_data_state state;
	uint8_t current_sid;
	uint8_t data[PA_MAX_DATA_SIZE];
	uint8_t data_length;
	uint8_t baas_overhead;
};

void pa_data_mgr_init(struct pa_data_context *ctx, uint8_t baas_overhead);

/**
 * Validate and accept PA data for transmission.
 * Does not touch hardware - only validates state and stores data.
 * @return PA_RESULT_* code
 */
uint8_t pa_data_mgr_submit(struct pa_data_context *ctx,
			    uint8_t sid, const uint8_t *data, uint8_t data_len);

/**
 * Called when PA interval elapses.
 * @return true if state transitioned PENDING -> COMPLETE
 */
bool pa_data_mgr_on_pa_interval(struct pa_data_context *ctx);

void pa_data_mgr_reset(struct pa_data_context *ctx);

uint8_t pa_data_mgr_available_bytes(const struct pa_data_context *ctx);

enum pa_data_state pa_data_mgr_state_get(const struct pa_data_context *ctx);

#endif /* PA_DATA_MANAGER_H */
