#ifndef ZEPHYR_UART_ADAPTER_H
#define ZEPHYR_UART_ADAPTER_H

#include <zephyr/kernel.h>
#include "port_uart.h"

#define UART_RX_BUF_SIZE 260

struct zephyr_uart_adapter_state {
	const struct device *dev;
	pa_uart_rx_cb_t rx_cb;
	void *rx_cb_user_data;
	uint8_t rx_buf[UART_RX_BUF_SIZE];
	uint8_t rx_pos;
	uint8_t rx_expected;
	bool rx_header_received;
	struct k_work rx_work;
	uint8_t rx_frame[UART_RX_BUF_SIZE];
	size_t rx_frame_len;
};

struct pa_port_uart zephyr_uart_adapter_create(struct zephyr_uart_adapter_state *state);

#endif /* ZEPHYR_UART_ADAPTER_H */
