#include "pa_data_manager.h"
#include <string.h>

void pa_data_mgr_init(struct pa_data_context *ctx, uint8_t baas_overhead)
{
	if (ctx == NULL) {
		return;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->state = PA_STATE_IDLE;
	ctx->baas_overhead = baas_overhead;
}

uint8_t pa_data_mgr_submit(struct pa_data_context *ctx,
			    uint8_t sid, const uint8_t *data, uint8_t data_len)
{
	if (ctx == NULL || data == NULL) {
		return PA_RESULT_NOT_READY;
	}

	if (ctx->state != PA_STATE_IDLE) {
		return PA_RESULT_BUSY;
	}

	uint8_t available = pa_data_mgr_available_bytes(ctx);

	if (data_len > available) {
		return PA_RESULT_DATA_TOO_LARGE;
	}

	ctx->current_sid = sid;
	ctx->data_length = data_len;
	memcpy(ctx->data, data, data_len);
	ctx->state = PA_STATE_PENDING;

	return PA_RESULT_SUCCESS;
}

bool pa_data_mgr_on_pa_interval(struct pa_data_context *ctx)
{
	if (ctx == NULL || ctx->state != PA_STATE_PENDING) {
		return false;
	}

	ctx->state = PA_STATE_COMPLETE;
	return true;
}

void pa_data_mgr_reset(struct pa_data_context *ctx)
{
	if (ctx == NULL) {
		return;
	}

	ctx->state = PA_STATE_IDLE;
	ctx->data_length = 0;
}

uint8_t pa_data_mgr_available_bytes(const struct pa_data_context *ctx)
{
	if (ctx == NULL) {
		return 0;
	}

	if (ctx->baas_overhead >= PA_MAX_DATA_SIZE) {
		return 0;
	}

	return PA_MAX_DATA_SIZE - ctx->baas_overhead;
}

enum pa_data_state pa_data_mgr_state_get(const struct pa_data_context *ctx)
{
	if (ctx == NULL) {
		return PA_STATE_IDLE;
	}

	return ctx->state;
}
