#include "pa_service.h"
#include "pa_cmd_parser.h"
#include "port_log.h"

PA_LOG_MODULE_REGISTER(pa_service);

int pa_service_init(struct pa_service *svc,
		    struct pa_port_uart *uart,
		    struct pa_port_ble_pa *ble_pa,
		    struct pa_port_timer *timer,
		    uint8_t baas_overhead)
{
	if (svc == NULL || uart == NULL || ble_pa == NULL || timer == NULL) {
		return -1;
	}

	svc->uart = uart;
	svc->ble_pa = ble_pa;
	svc->timer = timer;

	pa_data_mgr_init(&svc->pa_ctx, baas_overhead);

	int ret = uart->init(uart->ctx, pa_service_on_command, svc);

	if (ret) {
		PA_LOG_ERR("UART init failed: %d", ret);
		return ret;
	}

	PA_LOG_INF("PA service initialized");
	return 0;
}

static void handle_status_get_req(struct pa_service *svc)
{
	struct pa_status_get_rsp rsp = {0};

	if (svc->ble_pa->is_ready(svc->ble_pa->ctx)) {
		struct pa_adv_info info[PA_MAX_ADV_SETS];

		svc->ble_pa->get_status(svc->ble_pa->ctx,
					&rsp.num_advertising,
					info, PA_MAX_ADV_SETS);

		for (uint8_t i = 0; i < rsp.num_advertising && i < PA_MAX_ADV_SETS; i++) {
			rsp.adv_sid[i] = info[i].sid;
			rsp.pa_available_bytes[i] = info[i].available_bytes;
		}
	}

	int len = pa_cmd_serialize_status_rsp(&rsp, svc->tx_buf,
					      PA_SERVICE_TX_BUF_SIZE);
	if (len > 0) {
		svc->uart->send(svc->uart->ctx, svc->tx_buf, len);
	}

	PA_LOG_INF("Status Get: %u adv sets", rsp.num_advertising);
}

static void handle_data_send_req(struct pa_service *svc,
				 const struct pa_data_send_req *req)
{
	uint8_t result;

	if (!svc->ble_pa->is_ready(svc->ble_pa->ctx)) {
		result = PA_RESULT_NOT_READY;
		goto send_rsp;
	}

	result = pa_data_mgr_submit(&svc->pa_ctx, req->advertising_sid,
				    req->data, req->data_length);

	if (result != PA_RESULT_SUCCESS) {
		goto send_rsp;
	}

	int ret = svc->ble_pa->set_data(svc->ble_pa->ctx,
					req->advertising_sid,
					req->data, req->data_length);
	if (ret) {
		PA_LOG_ERR("BLE PA set_data failed: %d", ret);
		pa_data_mgr_reset(&svc->pa_ctx);
		result = PA_RESULT_NOT_READY;
		goto send_rsp;
	}

	/* Wait longer than one PA interval (150-200ms) to ensure data is broadcast */
	svc->timer->start_oneshot(svc->timer->ctx, 250,
				  pa_service_on_pa_interval, svc);

send_rsp:;
	int len = pa_cmd_serialize_data_send_rsp(result, svc->tx_buf,
						 PA_SERVICE_TX_BUF_SIZE);
	if (len > 0) {
		svc->uart->send(svc->uart->ctx, svc->tx_buf, len);
	}

	PA_LOG_INF("Data Send: SID=%u len=%u result=0x%02x",
		req->advertising_sid, req->data_length, result);
}

void pa_service_on_command(const uint8_t *data, size_t len, void *user_data)
{
	struct pa_service *svc = user_data;
	uint8_t opcode;
	union pa_cmd_payload payload;

	int ret = pa_cmd_parse(data, len, &opcode, &payload);

	if (ret) {
		PA_LOG_WRN("Command parse error: %d", ret);
		return;
	}

	switch (opcode) {
	case PA_OPCODE_STATUS_GET_REQ:
		handle_status_get_req(svc);
		break;

	case PA_OPCODE_DATA_SEND_REQ:
		handle_data_send_req(svc, &payload.data_send_req);
		break;

	default:
		PA_LOG_WRN("Unknown opcode: 0x%02x", opcode);
		break;
	}
}

void pa_service_on_pa_interval(void *user_data)
{
	struct pa_service *svc = user_data;

	bool transitioned = pa_data_mgr_on_pa_interval(&svc->pa_ctx);

	if (transitioned) {
		int len = pa_cmd_serialize_data_send_complete(
			svc->tx_buf, PA_SERVICE_TX_BUF_SIZE);

		if (len > 0) {
			svc->uart->send(svc->uart->ctx, svc->tx_buf, len);
		}

		pa_data_mgr_reset(&svc->pa_ctx);

		PA_LOG_INF("PA data send complete notification sent");
	}
}
