#include <string.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#define UART_NODE DT_NODELABEL(uart1)

#define RING_BUF_SIZE 1024
#define UART_IRQ_THREAD_STACK_SIZE 1024
#define UART_IRQ_THREAD_PRIORITY 10

LOG_MODULE_REGISTER(uart_irq, CONFIG_UART_IRQ_LOG_LEVEL);

uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;

const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

static void interrupt_handler(const struct device *dev, void *user_data)
{
	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {

		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];

			int recv_len = uart_fifo_read(dev, buf, sizeof(buf));
			if (recv_len <= 0) {
				continue;
			}

			ring_buf_put(&ringbuf, buf, recv_len);
		}
	}
}

/* kite uart irq <start|stop> - interrupt-driven background RX */
static void uart_irq_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return;
	}

	ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);


	uart_irq_callback_set(uart_dev, interrupt_handler);
	uart_irq_rx_enable(uart_dev);

	uint8_t buf[64];

	while (1) {
		int len = ring_buf_get(&ringbuf, buf, sizeof(buf));

		char tx_buf[65];
		memcpy(tx_buf, buf, len);
		tx_buf[len] = '\0';

		if (len > 0) {
            printk("Received message: %s\n", tx_buf);
			// Put data onto message queue
            // k_msgq_put(&servo_queue, &rx_buf, K_NO_WAIT);
		} else {
			k_sleep(K_MSEC(10));
		}
		

        // k_msgq_get(&status_queue, &status, K_NO_WAIT);
	}

	return;
}

K_THREAD_DEFINE(uart_irq, UART_IRQ_THREAD_STACK_SIZE,
		uart_irq_thread, NULL, NULL, NULL,
		UART_IRQ_THREAD_PRIORITY, 0, 0);