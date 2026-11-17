#include "tic_reader.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_REGISTER(tic, CONFIG_LOG_DEFAULT_LEVEL);

#define TIC_STACK_SIZE 1536
#define TIC_PRIORITY   K_PRIO_PREEMPT(10)

static const struct device *const uart = DEVICE_DT_GET(DT_ALIAS(tic_uart));

RING_BUF_DECLARE(rx_ring, 512);
static K_SEM_DEFINE(rx_sem, 0, 1);

static struct tic_meter meter;
static tic_reader_cb user_cb;
static int64_t last_frame_ms;
static int64_t last_switch_ms;
static uint32_t baudrate = 9600;
static bool locked;

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t buf[32];
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		/* If the reader thread falls behind, the excess is dropped and the frame fails its checks. */
		ring_buf_put(&rx_ring, buf, n);
		k_sem_give(&rx_sem);
	}
}

static int apply_baudrate(uint32_t rate)
{
	const struct uart_config cfg = {
		.baudrate = rate,
		.parity = UART_CFG_PARITY_NONE, /* 7E1 is read as 8N1, parity checked in software */
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	uart_irq_rx_disable(uart);
	int err = uart_configure(uart, &cfg);

	ring_buf_reset(&rx_ring);
	tic_meter_init(&meter, meter.on_reading, NULL);
	uart_irq_rx_enable(uart);
	return err;
}

static void on_reading(const struct tic_reading *reading, void *ctx)
{
	ARG_UNUSED(ctx);

	last_frame_ms = k_uptime_get();
	if (!locked) {
		locked = true;
		LOG_INF("TIC found: %s mode, %u baud",
			reading->mode == TIC_MODE_STANDARD ? "standard" : "historic", baudrate);
	}
	if (user_cb) {
		user_cb(reading);
	}
}

static void tic_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint8_t buf[64];

	last_switch_ms = k_uptime_get();
	for (;;) {
		k_sem_take(&rx_sem, K_MSEC(500));

		uint32_t n;

		do {
			unsigned int key = irq_lock();

			n = ring_buf_get(&rx_ring, buf, sizeof(buf));
			irq_unlock(key);
			if (n) {
				tic_meter_feed(&meter, buf, n);
			}
		} while (n);

		/* No valid frame for a while: try the other speed. */
		int64_t now = k_uptime_get();
		int64_t quiet_since = MAX(last_frame_ms, last_switch_ms);

		if (now - quiet_since > CONFIG_LINKY_TIC_TIMEOUT_MS) {
			if (locked) {
				LOG_WRN("TIC lost at %u baud", baudrate);
				locked = false;
			}
			baudrate = (baudrate == 9600) ? 1200 : 9600;
			last_switch_ms = now;
			if (apply_baudrate(baudrate)) {
				LOG_ERR("Cannot set %u baud", baudrate);
			}
		}
	}
}

K_THREAD_STACK_DEFINE(tic_stack, TIC_STACK_SIZE);
static struct k_thread tic_thread_data;

int tic_reader_start(tic_reader_cb cb)
{
	if (!device_is_ready(uart)) {
		LOG_ERR("TIC UART not ready");
		return -ENODEV;
	}
	user_cb = cb;
	tic_meter_init(&meter, on_reading, NULL);
	uart_irq_callback_user_data_set(uart, uart_isr, NULL);

	int err = apply_baudrate(baudrate);

	if (err) {
		return err;
	}
	k_thread_create(&tic_thread_data, tic_stack, K_THREAD_STACK_SIZEOF(tic_stack), tic_thread, NULL,
			NULL, NULL, TIC_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&tic_thread_data, "tic");
	return 0;
}
