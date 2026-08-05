#define DT_DRV_COMPAT caninos_puppy_i2c

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/i2c.h>
#include <soc.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2c_puppy);

#include "i2c_puppy.h"

struct i2c_puppy_data {
	uint16_t clock_div;
};

struct i2c_puppy_config {
	int id;
	uint32_t base;
	uint32_t bus_freq;
	int sda_pin;
	int scl_pin;
	const struct device *clk_dev;
	clock_control_subsys_t clk_bits;
};

static uint16_t i2c_puppy_get_div(int bus_freq)
{
	uint16_t div = (sys_clock_hw_cycles_per_sec() + bus_freq - 1) / bus_freq;
	if (div & 1) {
		div += 1;
	}
	return (div >> 2);
}

// First byte to send after an START bit
// Some datasheets might refer to this as control byte
static int i2c_puppy_send_addr(const struct device *dev, uint16_t addr, uint16_t rw_flag)
{
	const struct i2c_puppy_config *config = dev->config;
	struct i2c_puppy_data *data = dev->data;
	uint32_t base = config->base;
	uint32_t cmd_buf[I2C_CMD_BUF_SIZE];
	int index = 0;
	uint8_t i2c_data = (addr << 1) | rw_flag;

	cmd_buf[index++] = I2C_CMD_CFG | data->clock_div;
	cmd_buf[index++] = I2C_CMD_START;
	cmd_buf[index++] = I2C_CMD_WRB | i2c_data;
	cmd_buf[index++] = I2C_CMD_EOT;

	while (!plp_udma_canEnqueue(base + UDMA_CHANNEL_CMD_OFFSET))
		;

	plp_udma_enqueue(base + UDMA_CHANNEL_CMD_OFFSET, (uint32_t)cmd_buf, index * 4,
			 UDMA_CHANNEL_CFG_SIZE_32);

	while (plp_udma_busy(base + UDMA_CHANNEL_CMD_OFFSET))
		;

	return 0;
}

static int i2c_puppy_write_msg(const struct device *dev, struct i2c_msg *msg, uint16_t addr)
{
	const struct i2c_puppy_config *config = dev->config;
	struct i2c_puppy_data *data = dev->data;
	uint32_t base = config->base;
	uint32_t cmd_buf[I2C_CMD_BUF_SIZE];
	int index = 0;
	i2c_puppy_send_addr(dev, addr, I2C_W_FLAG);

	cmd_buf[index++] = I2C_CMD_CFG | data->clock_div;

	for (int i = 0; i < msg->len; i++) {
		cmd_buf[index++] = I2C_CMD_WRB | msg->buf[i];
	}

	if (msg->flags & I2C_MSG_STOP) {
		cmd_buf[index++] = I2C_CMD_STOP;
	}

	cmd_buf[index++] = I2C_CMD_EOT;

	while (!plp_udma_canEnqueue(base + UDMA_CHANNEL_CMD_OFFSET))
		;

	plp_udma_enqueue(base + UDMA_CHANNEL_CMD_OFFSET, (uint32_t)cmd_buf, index * 4,
			 UDMA_CHANNEL_CFG_SIZE_32);
#if CONFIG_I2C_W_BLOCKING
	while (plp_udma_busy(base + UDMA_CHANNEL_CMD_OFFSET))
		;
#endif

	return 0;
}

static int i2c_puppy_read_msg(const struct device *dev, struct i2c_msg *msg, uint16_t addr)
{
	const struct i2c_puppy_config *config = dev->config;

	uint32_t base = config->base;
	uint32_t cmd_buf[I2C_CMD_BUF_SIZE];
	uint32_t index = 0;

	i2c_puppy_send_addr(dev, addr, I2C_R_FLAG);

	if (msg->len > 1) {
		cmd_buf[index++] = I2C_CMD_RPT | (msg->len - 1);
		cmd_buf[index++] = I2C_CMD_RD_ACK;
	}

	cmd_buf[index++] = I2C_CMD_RD_NACK;

	if (msg->flags & I2C_MSG_STOP) {
		cmd_buf[index++] = I2C_CMD_STOP;
	}

	while (!plp_udma_canEnqueue(base + UDMA_CHANNEL_RX_OFFSET))
		;
	while (!plp_udma_canEnqueue(base + UDMA_CHANNEL_CMD_OFFSET))
		;

	plp_udma_enqueue(base + UDMA_CHANNEL_RX_OFFSET, (uint32_t)msg->buf, msg->len,
			 UDMA_CHANNEL_CFG_SIZE_8);
	plp_udma_enqueue(base + UDMA_CHANNEL_CMD_OFFSET, (uint32_t)cmd_buf, index * 4,
			 UDMA_CHANNEL_CFG_SIZE_32);

	while (plp_udma_busy(base + UDMA_CHANNEL_RX_OFFSET) ||
	       plp_udma_busy(base + UDMA_CHANNEL_CMD_OFFSET))
		;

	return 0;
}

static int i2c_puppy_configure(const struct device *dev, uint32_t dev_config)
{
	const struct i2c_puppy_config *config;
	struct i2c_puppy_data *data = dev->data;
	uint32_t i2c_speed;

	/* Check for NULL pointers */
	if (dev == NULL) {
		return -EINVAL;
	}

	config = dev->config;

	if (config == NULL) {
		return -EINVAL;
	}

	/* Configure bus frequency */
	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		i2c_speed = 100000U; /* 100 KHz */
		break;
	case I2C_SPEED_FAST:
		i2c_speed = 400000U; /* 400 KHz */
		break;
	case I2C_SPEED_FAST_PLUS:
	case I2C_SPEED_HIGH:
	case I2C_SPEED_ULTRA:
	default:
		return -ENOTSUP;
	}

	data->clock_div = i2c_puppy_get_div(i2c_speed);

	/* Support I2C Master mode only */
	if (!(dev_config & I2C_MODE_CONTROLLER)) {
		return -ENOTSUP;
	}

	/*
	 * Puppy I2C controller does not support 10-bit addresses.
	 */
	if (dev_config & I2C_ADDR_10_BITS) {
		return -ENOTSUP;
	}

	return 0;
}

static int i2c_puppy_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			      uint16_t addr)
{
	int ret = 0;

	/* Check for NULL pointers */
	if (dev == NULL) {
		return -EINVAL;
	}

	if (dev->config == NULL) {
		return -EINVAL;
	}

	if (msgs == NULL) {
		return -EINVAL;
	}

	for (int i = 0; i < num_msgs; i++) {
		if (msgs[i].flags & I2C_MSG_READ) {
			ret = i2c_puppy_read_msg(dev, &(msgs[i]), addr);
		} else {
			ret = i2c_puppy_write_msg(dev, &(msgs[i]), addr);
		}

		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int i2c_puppy_init(const struct device *dev)
{
	const struct i2c_puppy_config *config = dev->config;
	struct i2c_puppy_data *data = dev->data;
	int ret = clock_control_on(config->clk_dev, config->clk_bits);
	if (ret != 0) {
		return ret;
	}

	data->clock_div = i2c_puppy_get_div(config->bus_freq);

	switch (config->id) {
	case 1:
		config_pad_func(config->sda_pin, 0x2); // function 2 for spi1
		config_pad_func(config->scl_pin, 0x2); // function 2 for spi1
		break;
	case 0:
		config_pad_func(config->sda_pin, 0x0); // function 0 for spi0
		config_pad_func(config->scl_pin, 0x0); // function 0 for spi0
		break;
	default:
		return -EINVAL;
	}

	config_pad_cfg(config->sda_pin, 0x1); // enable pull-up
	config_pad_cfg(config->scl_pin, 0x1); // enable pull-up
	return 0;
}

static DEVICE_API(i2c, i2c_puppy_driver_api) = {
	.configure = i2c_puppy_configure,
	.transfer = i2c_puppy_transfer,
};

#define PUPPY_I2C_INIT(idx)                                                                        \
	static const struct i2c_puppy_config i2c_puppy_##idx##_config = {                          \
		.id = DT_INST_PROP(idx, i2c_id),                                                   \
		.base = DT_INST_REG_ADDR(idx),                                                     \
		.bus_freq = DT_INST_ENUM_IDX_OR(idx, clock_frequency, 100000U),                    \
		.sda_pin = DT_INST_PROP(idx, sda_pin),                                             \
		.scl_pin = DT_INST_PROP(idx, scl_pin),                                             \
		.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                                \
		.clk_bits = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, bits),                \
	};                                                                                         \
                                                                                                   \
	static struct i2c_puppy_data i2c_puppy_##idx##_data = {.clock_div = 0};                    \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, i2c_puppy_init, NULL, &i2c_puppy_##idx##_data,                  \
			      &i2c_puppy_##idx##_config, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,    \
			      &i2c_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_I2C_INIT);
