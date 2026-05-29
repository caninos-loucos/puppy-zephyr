#define DT_DRV_COMPAT caninos_puppy_uart

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <soc.h>
#include "uart_puppy.h"

struct uart_puppy_data {
	uint32_t base;
	struct uart_config uart_config;
	int id;

	uart_irq_callback_user_data_t callback;
	void *user_data;

	uint8_t txbuf[UART_PUPPY_MAX_BUFFER_SIZE];
	uint8_t rxbuf[UART_PUPPY_MAX_BUFFER_SIZE];

	bool rx_byte_available;
	bool tx;
};

static int uart_puppy_configure(const struct device *dev, const struct uart_config *cfg)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t baudrate, clk_div, polling_en, stop_bits, bit_length, parity_en;
	uint32_t setup = 0;

	if (cfg == NULL) {
		return -EINVAL;
	}

	baudrate = cfg->baudrate;
	if (baudrate > sys_clock_hw_cycles_per_sec()) {
		return -EINVAL;
	} else {
		clk_div = (sys_clock_hw_cycles_per_sec() + baudrate / 2) / baudrate - 1;
	}

#if CONFIG_UART_INTERRUPT_DRIVEN
	polling_en = 0;
#else
	polling_en = 1;
#endif

	if (cfg->parity == UART_CFG_PARITY_NONE) {
		parity_en = 0;
	} else if (cfg->parity == UART_CFG_PARITY_EVEN) {
		parity_en = 1;
	} else {
		return -EINVAL;
	}

	if (cfg->stop_bits == UART_CFG_STOP_BITS_1) {
		stop_bits = 0;
	} else if (cfg->stop_bits == UART_CFG_STOP_BITS_2) {
		stop_bits = 1;
	} else {
		return -EINVAL;
	}

	if (cfg->data_bits == UART_CFG_DATA_BITS_9) {
		return -EINVAL;
	} else {
		bit_length = cfg->data_bits & 0x3;
	}

	setup = (clk_div << UART_CLKDIV_SHIFT) | (polling_en & UART_POLL_EN) | UART_TXEN |
		UART_RXEN | (stop_bits & UART_STOP_BITS) | (bit_length << UART_BIT_LEN_SHIFT) |
		(parity_en & UART_PARITY_EN);

	sys_write32(setup, data->base + UART_SETUP);

	return 0;
}

static int uart_puppy_poll_in(const struct device *dev, unsigned char *c)
{
	struct uart_puppy_data *data = dev->data;

	if (!sys_read32(data->base + UART_VALID)) {
		return -1;
	}

	*c = sys_read32(data->base + UART_DATA);
	return 0;
}

static void uart_puppy_poll_out(const struct device *dev, unsigned char c)
{
	struct uart_puppy_data *data = dev->data;
	while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_TX_OFFSET))
		;

	plp_udma_enqueue(data->base + UDMA_CHANNEL_TX_OFFSET, (uint32_t)&c, 1,
			 UDMA_CHANNEL_CFG_SIZE_8);

	while (plp_udma_busy(data->base + UDMA_CHANNEL_TX_OFFSET))
		;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

// RX Funcs

static int uart_puppy_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	struct uart_puppy_data *data = dev->data;

	if (!size || !rx_data) {
		return -EINVAL;
	}
	if (!data->rx_byte_available) {
		return 0;
	}

	rx_data[0] = sys_read32(data->base + UART_DATA);
	data->rx_byte_available = false;

	sys_write32(sys_read32(data->base + UART_STATUS) | UART_CLEAN_FIFO,
		    data->base + UART_STATUS);
	sys_write32(sys_read32(data->base + UART_STATUS) & ~UART_CLEAN_FIFO,
		    data->base + UART_STATUS);

	plp_udma_enqueue(data->base + UDMA_CHANNEL_RX_OFFSET, (uint32_t)data->rxbuf, 1,
			 UDMA_CHANNEL_CFG_SIZE_8);

	return 1;
}

static void uart_puppy_irq_rx_enable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t key;
	key = irq_lock();

	sys_write32(sys_read32(data->base + UART_SETUP) | UART_RXEN, data->base + UART_SETUP);

	sys_write32(sys_read32(data->base + UART_IRQEN) | UART_RXIRQEN, data->base + UART_IRQEN);

	sys_write32(sys_read32(data->base + UART_STATUS) | UART_CLEAN_FIFO,
		    data->base + UART_STATUS);
	sys_write32(sys_read32(data->base + UART_STATUS) & ~UART_CLEAN_FIFO,
		    data->base + UART_STATUS);

	if (data->id == 0) {
		puppy_event_enable(ARCHI_UDMA_UART0_RX_EVT(0));
	}

#ifdef CONFIG_SOC_PUPPY_V2
	else if (data->id == 1) {
		puppy_event_enable(ARCHI_UDMA_UART1_RX_EVT(0));
	}
#endif
	data->rx_byte_available = false;

	// UART only sends RX interrupt when a set bytes of data arrive.
	plp_udma_enqueue(data->base + UDMA_CHANNEL_RX_OFFSET, (uint32_t)data->rxbuf, 1,
			 UDMA_CHANNEL_CFG_SIZE_8);

	irq_unlock(key);
}

static void uart_puppy_irq_rx_disable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t key;

	key = irq_lock();

	sys_write32(sys_read32(data->base + UART_IRQEN) & ~UART_RXIRQEN, data->base + UART_IRQEN);

	if (data->id == 0) {
		puppy_event_disable(ARCHI_UDMA_UART0_RX_EVT(0));
	}

#ifdef CONFIG_SOC_PUPPY_V2
	else if (data->id == 1) {
		puppy_event_disable(ARCHI_UDMA_UART1_RX_EVT(0));
	}
#endif
	data->rx_byte_available = false;

	sys_write32(sys_read32(data->base + UART_SETUP) & ~UART_RXEN, data->base + UART_SETUP);

	irq_unlock(key);
}

static int uart_puppy_irq_rx_ready(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return data->rx_byte_available ? 1 : 0;
}

// TX Functions

static int uart_puppy_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	struct uart_puppy_data *data = dev->data;

	if (data->tx) {
		return -EBUSY;
	}

	if (size > UART_PUPPY_MAX_BUFFER_SIZE) {
		size = UART_PUPPY_MAX_BUFFER_SIZE;
	}

	memcpy(data->txbuf, tx_data, size);

	data->tx = true;

	plp_udma_enqueue(data->base + UDMA_CHANNEL_TX_OFFSET, (uint32_t)data->txbuf, size,
			 UDMA_CHANNEL_CFG_SIZE_8);

	return size;
}

static void uart_puppy_irq_tx_enable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t key;
	key = irq_lock();

	if (data->id == 0) {
		puppy_event_enable(ARCHI_UDMA_UART0_TX_EVT(0));
	}

#ifdef CONFIG_SOC_PUPPY_V2
	else if (data->id == 1) {
		puppy_event_enable(ARCHI_UDMA_UART1_TX_EVT(0));
	}
#endif
	data->tx = false;

	sys_write32(sys_read32(data->base + UART_SETUP) | UART_TXEN, data->base + UART_SETUP);

	irq_unlock(key);
}

static void uart_puppy_irq_tx_disable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t key;

	key = irq_lock();

	if (data->id == 0) {
		puppy_event_disable(ARCHI_UDMA_UART0_TX_EVT(0));
	}

#ifdef CONFIG_SOC_PUPPY_V2
	else if (data->id == 1) {
		puppy_event_disable(ARCHI_UDMA_UART1_TX_EVT(0));
	}
#endif
	data->tx = false;

	sys_write32(sys_read32(data->base + UART_SETUP) & ~UART_TXEN, data->base + UART_SETUP);

	irq_unlock(key);
}

static int uart_puppy_irq_tx_ready(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return plp_udma_canEnqueue(data->base + UDMA_CHANNEL_TX_OFFSET);
}

static int uart_puppy_irq_tx_complete(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return sys_read32(data->base + UART_STATUS) & UART_TX_BUSY;
}

// Aux IRQ Funcs

static int uart_puppy_irq_is_pending(const struct device *dev)
{
	return uart_puppy_irq_tx_ready(dev) | uart_puppy_irq_rx_ready(dev);
}

static int uart_puppy_irq_update(const struct device *dev)
{
	return 1;
}

static void uart_puppy_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
					void *user_data)
{
	struct uart_puppy_data *data = dev->data;

	data->callback = cb;
	data->user_data = user_data;
}

static void uart_puppy_irq_handler(int event_num, const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;

	switch (event_num) {
	case ARCHI_UDMA_UART0_TX_EVT(0):
#ifdef CONFIG_SOC_PUPPY_V2
	case ARCHI_UDMA_UART1_TX_EVT(0):
#endif
		data->tx = false;
		goto callback;
	case ARCHI_UDMA_UART0_RX_EVT(0):
#ifdef CONFIG_SOC_PUPPY_V2
	case ARCHI_UDMA_UART1_RX_EVT(0):
#endif
		data->rx_byte_available = true;
		goto callback;
	default:
		break;
	}

	return;

callback:
	if (data->callback != NULL) {
		data->callback(dev, data->user_data);
	}
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static int uart_puppy_init(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	const struct uart_config *config = &(data->uart_config);

	uint32_t cg_conf = plp_udma_cg_get();

	if (data->id == 0) {
		plp_udma_cg_set(cg_conf | BIT(UDMA_UART0_ID));
	}

#ifdef CONFIG_SOC_PUPPY_V2
	else if (data->id == 1) {
		plp_udma_cg_set(cg_conf | BIT(UDMA_UART1_ID));
	}
#endif /* CONFIG_SOC_PUPPY_V2 */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	puppy_event_register_callback((event_callback_t)&uart_puppy_irq_handler, (void *)dev);
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

	return uart_puppy_configure(dev, config);
}

static DEVICE_API(uart, uart_puppy_driver_api) = {
	.configure = uart_puppy_configure,
	.poll_in = uart_puppy_poll_in,
	.poll_out = uart_puppy_poll_out,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_puppy_fifo_fill,
	.fifo_read = uart_puppy_fifo_read,
	.irq_tx_enable = uart_puppy_irq_tx_enable,
	.irq_tx_disable = uart_puppy_irq_tx_disable,
	.irq_tx_ready = uart_puppy_irq_tx_ready,
	.irq_tx_complete = uart_puppy_irq_tx_complete,
	.irq_rx_enable = uart_puppy_irq_rx_enable,
	.irq_rx_disable = uart_puppy_irq_rx_disable,
	.irq_rx_ready = uart_puppy_irq_rx_ready,
	.irq_is_pending = uart_puppy_irq_is_pending,
	.irq_update = uart_puppy_irq_update,
	.irq_callback_set = uart_puppy_irq_callback_set,
#endif
};

#define PUPPY_UART_INIT(idx)                                                                       \
                                                                                                   \
	static struct uart_puppy_data uart_puppy_##idx##_data = {                                  \
		.base = DT_INST_REG_ADDR(idx),                                                     \
		.id = DT_INST_PROP(idx, uart_id),                                                  \
		.callback = NULL,                                                                  \
		.user_data = NULL,                                                                 \
		.tx = false,                                                                       \
		.rx_byte_available = false,                                                        \
		.uart_config = {                                                                   \
			.baudrate = DT_INST_PROP(idx, current_speed),                              \
			.stop_bits = DT_INST_ENUM_IDX_OR(idx, stop_bits, UART_CFG_STOP_BITS_1),    \
			.data_bits = DT_INST_ENUM_IDX_OR(idx, data_bits, UART_CFG_DATA_BITS_8),    \
			.parity = DT_INST_ENUM_IDX_OR(idx, parity, UART_CFG_PARITY_NONE),          \
		}};                                                                                \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, uart_puppy_init, NULL, &uart_puppy_##idx##_data, NULL,          \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_UART_INIT);
