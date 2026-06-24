#ifndef PORT_UART_H
#define PORT_UART_H

#include <stdint.h>
#include <stddef.h>

typedef void (*pa_uart_rx_cb_t)(const uint8_t *data, size_t len, void *user_data);

struct pa_port_uart {
	int (*init)(void *ctx, pa_uart_rx_cb_t rx_cb, void *user_data);
	int (*send)(void *ctx, const uint8_t *data, size_t len);
	void *ctx;
};

#endif /* PORT_UART_H */
