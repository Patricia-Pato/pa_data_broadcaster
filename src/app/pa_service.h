#ifndef PA_SERVICE_H
#define PA_SERVICE_H

#include "pa_data_manager.h"
#include "port_uart.h"
#include "port_ble_pa.h"
#include "port_timer.h"

#define PA_SERVICE_TX_BUF_SIZE 260

struct pa_service {
	struct pa_port_uart *uart;
	struct pa_port_ble_pa *ble_pa;
	struct pa_port_timer *timer;
	struct pa_data_context pa_ctx;
	uint8_t tx_buf[PA_SERVICE_TX_BUF_SIZE];
};

int pa_service_init(struct pa_service *svc,
		    struct pa_port_uart *uart,
		    struct pa_port_ble_pa *ble_pa,
		    struct pa_port_timer *timer,
		    uint8_t baas_overhead);

void pa_service_on_command(const uint8_t *data, size_t len, void *user_data);

void pa_service_on_pa_interval(void *user_data);

#endif /* PA_SERVICE_H */
