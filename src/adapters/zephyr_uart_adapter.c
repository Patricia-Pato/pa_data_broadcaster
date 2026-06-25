#include "zephyr_uart_adapter.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_adapter, CONFIG_MAIN_LOG_LEVEL);

static struct zephyr_uart_adapter_state *adapter_state;

static void rx_work_handler(struct k_work *work)
{
	struct zephyr_uart_adapter_state *s =
		CONTAINER_OF(work, struct zephyr_uart_adapter_state, rx_work);

	if (s->rx_cb != NULL) {
		s->rx_cb(s->rx_frame, s->rx_frame_len, s->rx_cb_user_data);
	}
}

static void uart_isr_callback(const struct device *dev, void *user_data)
{
	struct zephyr_uart_adapter_state *s = user_data;

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		uint8_t byte;
		int ret = uart_fifo_read(dev, &byte, 1);

		if (ret != 1) {
			break;
		}

		if (!s->rx_header_received) {
			/* First byte: Command_Length */
			s->rx_buf[0] = byte;
			s->rx_pos = 1;
			s->rx_expected = byte + 1; /* total = Command_Length + remaining */
			s->rx_header_received = true;

			if (s->rx_expected <= 1 || s->rx_expected > UART_RX_BUF_SIZE) {
				/* Invalid length, reset */
				s->rx_header_received = false;
				s->rx_pos = 0;
			}
		} else {
			if (s->rx_pos < UART_RX_BUF_SIZE) {
				s->rx_buf[s->rx_pos++] = byte;
			}

			if (s->rx_pos >= s->rx_expected) {
				/* Complete frame received */
				memcpy(s->rx_frame, s->rx_buf, s->rx_pos);
				s->rx_frame_len = s->rx_pos;

				s->rx_header_received = false;
				s->rx_pos = 0;

				k_work_submit(&s->rx_work);
			}
		}
	}
}

static int adapter_init(void *ctx, pa_uart_rx_cb_t rx_cb, void *user_data)
{
	struct zephyr_uart_adapter_state *s = ctx;

	s->dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
	if (!device_is_ready(s->dev)) {
		LOG_ERR("UART0 device not ready");
		return -ENODEV;
	}

	s->rx_cb = rx_cb;
	s->rx_cb_user_data = user_data;
	s->rx_pos = 0;
	s->rx_header_received = false;

	k_work_init(&s->rx_work, rx_work_handler);

	uart_irq_callback_user_data_set(s->dev, uart_isr_callback, s);
	uart_irq_rx_enable(s->dev);

	LOG_INF("UART adapter initialized on uart0");
	return 0;
}

static int adapter_send(void *ctx, const uint8_t *data, size_t len)
{
	struct zephyr_uart_adapter_state *s = ctx;

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(s->dev, data[i]);
	}

	return 0;
}

struct pa_port_uart zephyr_uart_adapter_create(struct zephyr_uart_adapter_state *state)
{
	adapter_state = state;
	memset(state, 0, sizeof(*state));

	struct pa_port_uart port = {
		.init = adapter_init,
		.send = adapter_send,
		.ctx = state,
	};

	return port;
}
