/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_uart

#define LOG_LEVEL CONFIG_UART_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_puppy);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/clock_control.h>
#include <soc.h>

#define UART_PUPPY_FIFO_SIZE 32

/* Setup bits */
#define UART_RXEN          BIT(9)
#define UART_TXEN          BIT(8)
#define UART_CLEAN_FIFO    BIT(5)
#define UART_POLL_EN       BIT(4)
#define UART_STOP_BITS     BIT(3)
#define UART_PARITY_EN     BIT(0)

#define UART_CLKDIV_MASK 0xFFFF
#define UART_CLKDIV_SHIFT  16

#define UART_BIT_LEN_SHIFT 1
#define UART_STOP_BITS_SHIFT 3
#define UART_PARITY_EN_SHIFT 0

#define UART_STATUS_TX_BUSY BIT(0)
#define UART_STATUS_RX_BUSY BIT(1)

/* Error bits */
#define UART_PUPPY_ERROR_OVERFLOW BIT(0)
#define UART_PUPPY_ERROR_PARITY   BIT(1)

struct uart_puppy_regs {
	uint32_t RX_SADDR;  // BASEADDR + 0x00
	uint32_t RX_SIZE;   // BASEADDR + 0x04
	uint32_t RX_CFG;    // BASEADDR + 0x08
	uint32_t RX_INTCFG; // BASEADDR + 0x0C
	uint32_t TX_SADDR;  // BASEADDR + 0x10
	uint32_t TX_SIZE;   // BASEADDR + 0x14
	uint32_t TX_CFG;    // BASEADDR + 0x18
	uint32_t TX_INTCFG; // BASEADDR + 0x1C
	uint32_t STATUS;    // BASEADDR + 0x20
	uint32_t SETUP;     // BASEADDR + 0x24
	uint32_t ERROR;     // BASEADDR + 0x28
	uint32_t IRQEN;     // BASEADDR + 0x2C
	uint32_t VALID;     // BASEADDR + 0x30
	uint32_t DATA;      // BASEADDR + 0x34
};

struct uart_puppy_data {
	volatile struct uart_puppy_regs *regs;
	struct uart_config uart_config;
	int id;

	uart_irq_callback_user_data_t callback;
	void *user_data;
	volatile bool callback_active;
	volatile bool callback_pending;

	volatile bool rx_overrun;
	volatile bool tx_irq_enabled, rx_irq_enabled;

	volatile uint8_t tx_buf[UART_PUPPY_FIFO_SIZE];
	volatile uint8_t rx_buf[UART_PUPPY_FIFO_SIZE];
	volatile uint8_t rx_byte, tx_byte;

	volatile uint8_t tx_tail, tx_head, tx_count;
	volatile uint8_t rx_tail, rx_head, rx_count;
	
	unsigned int tx_irq_num, rx_irq_num;
};

struct uart_puppy_cfg {
	const struct device *clk_dev;
	clock_control_subsys_t clk_bits;
};

static inline void uart_puppy_dma_tx_clear(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	data->regs->TX_CFG = UDMA_CHANNEL_CFG_CLEAR;
	data->regs->TX_CFG = 0U;
}

static inline void uart_puppy_dma_rx_clear(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	data->regs->RX_CFG = UDMA_CHANNEL_CFG_CLEAR;
	data->regs->RX_CFG = 0U;
}

static inline void uart_puppy_dma_tx_enqueue(const struct device *dev,
                                             volatile uint8_t *src)
{
	struct uart_puppy_data *data = dev->data;
	data->regs->TX_SADDR = (uint32_t)src;
	data->regs->TX_SIZE = 1U;
	data->regs->TX_CFG = UDMA_CHANNEL_CFG_SIZE_8 | UDMA_CHANNEL_CFG_EN;
}

static inline void uart_puppy_dma_rx_enqueue(const struct device *dev,
                                             volatile uint8_t *dst)
{
	struct uart_puppy_data *data = dev->data;
	data->regs->RX_SADDR = (uint32_t)dst;
	data->regs->RX_SIZE = 1U;
	data->regs->RX_CFG = UDMA_CHANNEL_CFG_SIZE_8 | UDMA_CHANNEL_CFG_EN;
}

static inline bool uart_puppy_dma_tx_busy(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return (data->regs->TX_CFG & UDMA_CHANNEL_CFG_EN) != 0U;
}

static inline bool uart_puppy_dma_rx_busy(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return (data->regs->RX_CFG & UDMA_CHANNEL_CFG_EN) != 0U;
}

static bool uart_puppy_try_send_locked(const struct device *dev, uint8_t ch)
{
	struct uart_puppy_data *data = dev->data;

	if (data->tx_count < UART_PUPPY_FIFO_SIZE) {
		data->tx_buf[data->tx_tail] = ch;
		data->tx_tail = (data->tx_tail + 1U) % UART_PUPPY_FIFO_SIZE;
		data->tx_count++;
		return true;
	}
	return false;
}

static bool uart_puppy_try_receive_locked(const struct device *dev,
                                          unsigned char *ch)
{
	struct uart_puppy_data *data = dev->data;

	if (data->rx_count == 0U) {
		return false;
	}
	*ch = data->rx_buf[data->rx_head];
	data->rx_head = (data->rx_head + 1U) % UART_PUPPY_FIFO_SIZE;
	data->rx_count--;

	if (data->rx_count == 0U) {
		data->rx_head = 0U;
		data->rx_tail = 0U;
	}
	return true;
}

static void uart_puppy_tx_advance_locked(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;

	if (uart_puppy_dma_tx_busy(dev)) {
		return;
	}
	if (data->tx_count == 0U) {
		data->tx_tail = 0U;
		data->tx_head = 0U;
		return;
	}
	data->tx_byte = data->tx_buf[data->tx_head];
	data->tx_head = (data->tx_head + 1U) % UART_PUPPY_FIFO_SIZE;
	data->tx_count--;

	uart_puppy_dma_tx_enqueue(dev, &data->tx_byte);
}

static void uart_puppy_rx_advance_locked(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;

	if (uart_puppy_dma_rx_busy(dev)) {
		return;
	}
	if (data->rx_count >= UART_PUPPY_FIFO_SIZE) {
		/* FIFO full: discard the oldest byte (rx overrun) */
		data->rx_head = (data->rx_head + 1U) % UART_PUPPY_FIFO_SIZE;
		data->rx_count--;
		data->rx_overrun = true;
	}
	data->rx_buf[data->rx_tail] = data->rx_byte;
	data->rx_tail = (data->rx_tail + 1U) % UART_PUPPY_FIFO_SIZE;
	data->rx_count++;

	uart_puppy_dma_rx_enqueue(dev, &data->rx_byte);
}

static int uart_puppy_err_check(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t error;
	unsigned int key;
	int ret = 0;

	key = irq_lock();

	error = data->regs->ERROR;

	if ((error & UART_PUPPY_ERROR_OVERFLOW) != 0U ||
	    data->rx_overrun) {
		ret |= UART_ERROR_OVERRUN;
		data->rx_overrun = false;
	}
	if ((error & UART_PUPPY_ERROR_PARITY) != 0U) {
		ret |= UART_ERROR_PARITY;
	}

	irq_unlock(key);
	return ret;
}

static void uart_puppy_poll_out(const struct device *dev, unsigned char ch)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key;
	bool done;

	/* Disable TX event forwarding to the event controller. */
	irq_disable(data->tx_irq_num);
	/*
	 * Manually advance the software FIFO and DMA using polling.
	 * TX events are not generated during this phase.
	 */
	for (done = false; !done;) {
		key = irq_lock();
		uart_puppy_tx_advance_locked(dev);
		
		done = (data->tx_count == 0U) && !uart_puppy_dma_tx_busy(dev);
		irq_unlock(key);
	}

	key = irq_lock();

	/* FIFO is guaranteed to have room after the drain above. */
	(void)uart_puppy_try_send_locked(dev, ch);

	/*
	 * Re-enable event forwarding while interrupts are still locked.
	 * This prevents a pending TX event from invoking the callback
	 * between queueing the polling byte and starting its DMA.
	 */
	irq_enable(data->tx_irq_num);

	uart_puppy_tx_advance_locked(dev);

	irq_unlock(key);
}

static int uart_puppy_poll_in(const struct device *dev, unsigned char *ch)
{
	unsigned int key;
	int ret;

	key = irq_lock();

	if (uart_puppy_try_receive_locked(dev, ch)) {
		ret = 0;
		/* A slot was freed, so collect a completed DMA if any. */
		uart_puppy_rx_advance_locked(dev);
	} else {
		/* Nothing buffered: try collecting the current DMA. */
		uart_puppy_rx_advance_locked(dev);
		ret = uart_puppy_try_receive_locked(dev, ch) ? 0 : -1;
	}

	irq_unlock(key);
	return ret;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static void uart_puppy_callback(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	uart_irq_callback_user_data_t callback;
	void *user_data;
	unsigned int key;

	key = irq_lock();

	if (data->callback == NULL) {
		irq_unlock(key);
		return;
	}

	if (data->callback_active) {
		data->callback_pending = true;
		irq_unlock(key);
		return;
	}

	data->callback_active = true;

	do {
		/*
		 * Clear pending before running the callback. Any new request
		 * occurring during the callback will set it again.
		 */
		data->callback_pending = false;

		callback = data->callback;
		user_data = data->user_data;

		irq_unlock(key);

		callback(dev, user_data);

		key = irq_lock();

		/*
		 * callback_set() may have disabled/replaced the callback
		 * while it was running.
		 */
		if (data->callback == NULL) {
			data->callback_pending = false;
		}
	} while (data->callback_pending);

	data->callback_active = false;

	irq_unlock(key);
}

static void uart_puppy_irq_callback_set(const struct device *dev,
                                        uart_irq_callback_user_data_t cb,
                                        void *user_data)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	data->callback = cb;
	data->user_data = user_data;
	if (cb == NULL) {
		data->callback_pending = false;
	}
	irq_unlock(key);
}

static int uart_puppy_fifo_fill(const struct device *dev,
                                const uint8_t *tx_data,
                                int len)
{
	unsigned int key;
	int i;

	if (len <= 0) {
		return 0;
	}

	key = irq_lock();
	uart_puppy_tx_advance_locked(dev);

	for (i = 0; i < len; i++) {
		if (!uart_puppy_try_send_locked(dev, tx_data[i])) {
			break;
		}
	}
	uart_puppy_tx_advance_locked(dev);

	irq_unlock(key);
	return i;
}

static int uart_puppy_fifo_read(const struct device *dev,
                                uint8_t *rx_data,
                                const int len)
{
	unsigned int key;
	int i;

	if (len <= 0) {
		return 0;
	}

	key = irq_lock();

	for (i = 0; i < len; i++) {
		/*
		 * Prefer already-buffered data. If the software FIFO is
		 * empty, try collecting a completed RX DMA.
		 */
		if (!uart_puppy_try_receive_locked(dev, &rx_data[i])) {
			uart_puppy_rx_advance_locked(dev);

			if (!uart_puppy_try_receive_locked(dev, &rx_data[i])) {
				break;
			}
		}

		/*
		 * We just freed one FIFO slot. Collect a completed DMA now,
		 * without unnecessarily causing a software FIFO overrun.
		 */
		uart_puppy_rx_advance_locked(dev);
	}

	irq_unlock(key);

	return i;
}

static int uart_puppy_irq_tx_ready(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	return (data->tx_count < UART_PUPPY_FIFO_SIZE);
}

static int uart_puppy_irq_tx_complete(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;

	return (data->tx_count == 0U) &&
	       !uart_puppy_dma_tx_busy(dev) &&
	       !(data->regs->STATUS & UART_STATUS_TX_BUSY);
}

static int uart_puppy_irq_rx_ready(const struct device *dev)
{
	const struct uart_puppy_data *data = dev->data;
	return (data->rx_count > 0U);
}

static int uart_puppy_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}

static void uart_puppy_irq_tx_enable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	bool notify = false;

	if (data->tx_irq_enabled) {
		irq_unlock(key);
		return;
	}
	data->tx_irq_enabled = true;

	uart_puppy_tx_advance_locked(dev);

	notify = uart_puppy_irq_tx_ready(dev);

	irq_unlock(key);

	if (notify) {
		uart_puppy_callback(dev);
	}
}
static void uart_puppy_irq_tx_disable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	data->tx_irq_enabled = false;
	irq_unlock(key);
}

static void uart_puppy_irq_rx_enable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	bool notify = false;

	if (data->rx_irq_enabled) {
		irq_unlock(key);
		return;
	}

	data->rx_irq_enabled = true;

	uart_puppy_rx_advance_locked(dev);

	notify = uart_puppy_irq_rx_ready(dev);

	irq_unlock(key);

	if (notify) {
		uart_puppy_callback(dev);
	}
}

static void uart_puppy_irq_rx_disable(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	data->rx_irq_enabled = false;
	irq_unlock(key);
}

static int uart_puppy_irq_is_pending(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	unsigned int key = irq_lock();
	int ret = (data->tx_irq_enabled && uart_puppy_irq_tx_ready(dev)) ||
	          (data->rx_irq_enabled && uart_puppy_irq_rx_ready(dev));
	irq_unlock(key);
	return ret;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static void uart_puppy_tx_irq(void *userdata)
{
	const struct device *dev = (const struct device *) userdata;
	struct uart_puppy_data *data __unused = dev->data;

	uart_puppy_tx_advance_locked(dev);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	if (data->tx_irq_enabled) {
		uart_puppy_callback(dev);
	}
#endif
}

static void uart_puppy_rx_irq(void *userdata)
{
	const struct device *dev = (const struct device *) userdata;
	struct uart_puppy_data *data __unused = dev->data;

	uart_puppy_rx_advance_locked(dev);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	if (data->rx_irq_enabled) {
		uart_puppy_callback(dev);
	}
#endif
}

static int uart_puppy_configure(const struct device *dev,
                                const struct uart_config *cfg)
{
	struct uart_puppy_data *data = dev->data;
	uint32_t baudrate, clk, clk_div;
	uint32_t setup = 0U;
	unsigned int key;

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		setup |= (0x0 << UART_PARITY_EN_SHIFT);
		break;
	case UART_CFG_PARITY_EVEN:
		setup |= (0x1 << UART_PARITY_EN_SHIFT);
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		setup |= (0x0 << UART_STOP_BITS_SHIFT);
		break;
	case UART_CFG_STOP_BITS_2:
		setup |= (0x1 << UART_STOP_BITS_SHIFT);
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		setup |= (0x0 << UART_BIT_LEN_SHIFT);
		break;
	case UART_CFG_DATA_BITS_6:
		setup |= (0x1 << UART_BIT_LEN_SHIFT);
		break;
	case UART_CFG_DATA_BITS_7:
		setup |= (0x2 << UART_BIT_LEN_SHIFT);
		break;
	case UART_CFG_DATA_BITS_8:
		setup |= (0x3 << UART_BIT_LEN_SHIFT);
		break;
	default:
		return -ENOTSUP;
	}

	if (cfg->flow_ctrl != UART_CFG_FLOW_CTRL_NONE) {
		return -ENOTSUP;
	}

	baudrate = cfg->baudrate;
	clk = sys_clock_hw_cycles_per_sec();

	if (baudrate == 0U || baudrate > clk) {
		return -ENOTSUP;
	}

	clk_div = DIV_ROUND_CLOSEST(clk, baudrate);

	if (clk_div > 0U) {
		clk_div--;
	}
	if (clk_div > UART_CLKDIV_MASK) {
		return -ENOTSUP;
	}

	setup |= (clk_div << UART_CLKDIV_SHIFT) | UART_TXEN | UART_RXEN;

	key = irq_lock();

	if ((data->tx_count > 0U) ||
	    uart_puppy_dma_tx_busy(dev) ||
	    (data->regs->STATUS & UART_STATUS_TX_BUSY)) {
		irq_unlock(key);
		return -EBUSY;
	}

	/* Clear RX and TX DMA */
	uart_puppy_dma_tx_clear(dev);
	uart_puppy_dma_rx_clear(dev);

	/* Start clear RX hardware FIFO operation */
	data->regs->SETUP = UART_CLEAN_FIFO;

	/* Disable TX/RX (also stop clear RX hardware FIFO operation) */
	data->regs->SETUP = 0U;

	/* Clear TX/RX Software FIFO information */
	data->tx_tail = 0U;
	data->tx_head = 0U;
	data->tx_count = 0U;
	data->rx_tail = 0U;
	data->rx_head = 0U;
	data->rx_count = 0U;
	data->rx_overrun = false;
	
	/* Clear any latched hardware RX error. */
	(void)data->regs->ERROR;

	/* Update local config */
	data->uart_config = *cfg;

	/* Program SETUP register and enable TX/RX */
	data->regs->SETUP = setup;

	/* Make sure UART's non-DMA interrupt sources are disabled */
	data->regs->IRQEN = 0U;

	/* Restart one-byte RX DMA */
	uart_puppy_dma_rx_enqueue(dev, &data->rx_byte);

	irq_unlock(key);

	return 0;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE

static int uart_puppy_config_get(const struct device *dev,
                                 struct uart_config *cfg)
{
	struct uart_puppy_data *data = dev->data;
	if (cfg != NULL) {
		*cfg = data->uart_config;
	}
	return 0;
}

#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

static int uart_puppy_common_init(const struct device *dev)
{
	struct uart_puppy_data *data = dev->data;
	const struct uart_puppy_cfg *config = dev->config;
	int ret;

	if (!device_is_ready(config->clk_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clk_dev, config->clk_bits);
	if (ret != 0) {
		return ret;
	}

	ret = uart_puppy_configure(dev, &data->uart_config);
	if (ret != 0) {
		clock_control_off(config->clk_dev, config->clk_bits);
	}
	return ret;
}

static DEVICE_API(uart, uart_puppy_driver_api) = {
	.err_check = uart_puppy_err_check,
	.poll_in = uart_puppy_poll_in,
	.poll_out = uart_puppy_poll_out,

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_puppy_configure,
	.config_get = uart_puppy_config_get,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

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

#define PUPPY_UART_INIT(_id)                                             \
	static int uart_puppy_##_id##_init(const struct device *dev) {   \
		unsigned int key;                                        \
		int ret = uart_puppy_common_init(dev);                   \
		if (ret != 0) {                                          \
			return ret;                                      \
		}                                                        \
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(_id, tx), 0,            \
			    uart_puppy_tx_irq,                           \
			    DEVICE_DT_INST_GET(_id), 0);                 \
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(_id, rx), 0,            \
			    uart_puppy_rx_irq,                           \
			    DEVICE_DT_INST_GET(_id), 0);                 \
		irq_enable(DT_INST_IRQN_BY_NAME(_id, tx));               \
		irq_enable(DT_INST_IRQN_BY_NAME(_id, rx));               \
		key = irq_lock();                                        \
		uart_puppy_rx_advance_locked(dev);                       \
		irq_unlock(key);                                         \
		return 0;                                                \
	}                                                                \
	static const struct uart_puppy_cfg uart_puppy_##_id##_cfg = {    \
		.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(_id)),      \
		.clk_bits = (clock_control_subsys_t)                     \
			    DT_INST_CLOCKS_CELL(_id, bits),              \
	};                                                               \
	static struct uart_puppy_data uart_puppy_##_id##_data = {        \
		.regs = (volatile struct uart_puppy_regs *)              \
			DT_INST_REG_ADDR(_id),                           \
		.id = DT_INST_PROP(_id, uart_id),                        \
		.callback = NULL,                                        \
		.user_data = NULL,                                       \
		.uart_config = {                                         \
			.baudrate = DT_INST_PROP(_id, current_speed),    \
			.stop_bits = DT_INST_ENUM_IDX_OR(_id,            \
				     stop_bits, UART_CFG_STOP_BITS_1),   \
			.data_bits = DT_INST_ENUM_IDX_OR(_id,            \
				     data_bits, UART_CFG_DATA_BITS_8),   \
			.parity = DT_INST_ENUM_IDX_OR(_id,               \
				  parity, UART_CFG_PARITY_NONE),         \
			.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,            \
		},                                                       \
		.tx_irq_num = DT_INST_IRQN_BY_NAME(_id, tx),             \
		.rx_irq_num = DT_INST_IRQN_BY_NAME(_id, rx),             \
	};                                                               \
	DEVICE_DT_INST_DEFINE(_id, uart_puppy_##_id##_init, NULL,        \
			      &uart_puppy_##_id##_data,                  \
			      &uart_puppy_##_id##_cfg,                   \
			      PRE_KERNEL_2, CONFIG_SERIAL_INIT_PRIORITY, \
			      &uart_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_UART_INIT);
