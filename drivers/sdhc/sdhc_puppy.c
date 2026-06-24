
/* Copyright (c) 2026 Caninos Loucos
 *SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_sdhc

#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "sdhc_puppy.h"

LOG_MODULE_REGISTER(sdhc, CONFIG_SDHC_LOG_LEVEL);

struct sdhc_puppy_config {
	uint32_t base;
	struct gpio_dt_spec pwr_gpio;
};

struct sdhc_puppy_data {
	struct sdhc_io io;
	struct sdhc_host_props props;
};

void set_clk_div(const struct sdhc_puppy_config *config, int freq)
{
	uint32_t base = config->base;
	uint32_t periph_freq = sys_clock_hw_cycles_per_sec();

	uint32_t div = periph_freq / (freq * 2);
	sys_write32(base + SDIO_CLK_DIV, (1 << 8) | (div & 0xff));
}

void release_padpull()
{
	// Release PAD PULL-DOWN from pads 21 to 25
	config_pad_cfg(21, 0x1);
	config_pad_cfg(22, 0x1);
	config_pad_cfg(23, 0x1);
	config_pad_cfg(24, 0x1);
	config_pad_cfg(25, 0x1);
}

void pull_padpull()
{
	// Pull PAD PULL-DOWN from pads 21 to 25
	config_pad_cfg(21, 0x0);
	config_pad_cfg(22, 0x0);
	config_pad_cfg(23, 0x0);
	config_pad_cfg(24, 0x0);
	config_pad_cfg(25, 0x0);
}

static int sdhc_puppy_reset(const struct device *dev)
{
	const struct sdhc_puppy_config *config = dev->config;
	uint32_t base = config->base;

	pull_padpull();
	
	k_usleep(100);

	release_padpull();

	// Clear Status bits
	sys_write32(0x3, base + SDIO_STATUS);

	// translate opcode to reg layout
	sys_write32(SD_GO_IDLE_STATE << CMD_OP_OFST | SD_RSP_TYPE_NONE, base + SDIO_CMD_OP);
	sys_write32(0x0, base + SDIO_CMD_ARG);
	sys_write32(0x1, base + SDIO_START);

	return 0;
}

static int sdhc_puppy_request(const struct device *dev, struct sdhc_command *cmd,
			      struct sdhc_data *data)
{
	const struct sdhc_puppy_config *config = dev->config;
	struct sdhc_puppy_data *dev_data = dev->data;
	uint32_t status;
	uint32_t base = config->base;
	int direction;
	int retries = cmd->retries;

retry:
	sys_write32(cmd->opcode << CMD_OP_OFST | cmd->response_type, base + SDIO_CMD_OP);
	sys_write32(cmd->arg, base + SDIO_CMD_ARG);
	sys_write32(0x1, base + SDIO_START);

	if (data != NULL) {

		// direction 1 is read, 0 is write
		direction = (cmd->opcode == SD_READ_SINGLE_BLOCK ||
			     cmd->opcode == SD_READ_MULTIPLE_BLOCK)
				    ? 1
				    : 0;

		plp_udma_enqueue(base + (direction) ? SDIO_RX_SADDR : SDIO_TX_SADDR,
				 (uint32_t)data->data, data->block_size * data->blocks,
				 UDMA_CHANNEL_CFG_EN | UDMA_CHANNEL_CFG_SIZE_32);

		sys_write32(SDIO_DATA_SETUP,
			    (BLOCK_SIZE(data->block_size) | BLOCK_NUM(data->blocks) |
			     DATA_QUAD((dev_data->io.bus_width == 4) ? 1 : 0) |
			     DATA_RWN(direction ? 1 : 0) | DATA_EN(1)));

		// Set data timeout in sdio clock cycles
		sys_write32(data->timeout_ms / (dev_data->io.clock * 1000),
			    base + SDIO_DATA_TIMEOUT);
	}

	do {
		status = sys_read32(base + SDIO_STATUS);
	} while ((status & (ERROR_MASK & EOT_MASK)) == 0);

	if (status & ERROR_MASK) {

		retries--;
		if (retries <= 0) {
			goto retry;
		}

		switch (status & ERR_STATUS_MASK) {
		case CMD_ERR_WRONG_CRC:
			LOG_ERR("Wrong CRC");
			return -EIO;
		case CMD_ERR_WRONG_DIR:
			LOG_ERR("Wrong Direction");
			return -EIO;
		case CMD_ERR_BUSY_TIMEOUT:
		case CMD_ERR_TIMEOUT:
		case DATA_ERR_TIMEOUT:
			LOG_ERR("Timed Out");
		default:
			return -ETIMEDOUT;
		}
	}

	if (cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) {
		cmd->response[0] = sys_read32(base + SDIO_RSP0);
		cmd->response[1] = sys_read32(base + SDIO_RSP1);
		cmd->response[2] = sys_read32(base + SDIO_RSP2);
		cmd->response[3] = sys_read32(base + SDIO_RSP3);
	}

	return 0;
}

static int sdhc_puppy_set_io(const struct device *dev, struct sdhc_io *ios)
{

	const struct sdhc_puppy_config *config = dev->config;
	struct sdhc_puppy_data *data = dev->data;
	uint8_t bus_width;
	int ret = 0;

	LOG_INF("SDHC I/O: bus width %d, clock %dHz, card power %s", ios->bus_width, ios->clock,
		ios->power_mode == SDHC_POWER_ON ? "ON" : "OFF");

	if (ios->clock > 0) {
		if (ios->clock > data->props.f_max || ios->clock < data->props.f_min) {
			LOG_ERR("Proposed clock outside supported host range");
			ret = -EINVAL;
		} else {
			set_clk_div(config, ios->clock);
			data->io.clock = (uint32_t)ios->clock;			
		}
	}

	if (ios->bus_width > 0) {
		switch (ios->bus_width) {
		case SDHC_BUS_WIDTH1BIT:
			bus_width = 1;
			break;
		case SDHC_BUS_WIDTH4BIT:
			bus_width = 4;
			break;
		case SDHC_BUS_WIDTH8BIT:
		default:
			bus_width = 4;
			ret = -ENOTSUP;
		}

		data->io.bus_width = bus_width;
	}

	if ((data->io.power_mode != ios->power_mode) && (config->pwr_gpio.port)) {
		if (ios->power_mode == SDHC_POWER_OFF) {
			ret = gpio_pin_set_dt(&config->pwr_gpio, 0);
		} else if (ios->power_mode == SDHC_POWER_ON) {
			ret = gpio_pin_set_dt(&config->pwr_gpio, 1);
		}
		data->io.power_mode = ios->power_mode;
	}

	if (ios->timing > 0) {
		if (data->io.timing != ios->timing) {
			switch (ios->timing) {
			case SDHC_TIMING_LEGACY:
			case SDHC_TIMING_HS:
				break;
			case SDHC_TIMING_DDR50:
			case SDHC_TIMING_DDR52:
			case SDHC_TIMING_SDR12:
			case SDHC_TIMING_SDR25:
			case SDHC_TIMING_SDR50:
			case SDHC_TIMING_HS400:
			case SDHC_TIMING_SDR104:
			case SDHC_TIMING_HS200:
			default:
				LOG_ERR("Timing mode not supported for this device");
				ret = -ENOTSUP;
				break;
			}

			LOG_INF("Bus timing successfully changed to %d", ios->timing);
			data->io.timing = ios->timing;
		}
	}

	return ret;
}

static int sdhc_puppy_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	struct sdhc_puppy_data *data = dev->data;
	props = &data->props;
	return 0;
}

static DEVICE_API(sdhc, sdhc_puppy_driver_api) = {
	.reset = sdhc_puppy_reset,
	.request = sdhc_puppy_request,
	.set_io = sdhc_puppy_set_io,
	.get_host_props = sdhc_puppy_get_host_props,
};

static int sdhc_puppy_init(const struct device *dev)
{
	const struct sdhc_puppy_config *config = dev->config;
	int ret = 0;
	uint32_t cg_conf = plp_udma_cg_get();

	plp_udma_cg_set(cg_conf | BIT(UDMA_SDIO_ID));

	if (gpio_is_ready_dt(&config->pwr_gpio)) {
		ret = gpio_pin_configure_dt(&config->pwr_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret) {
			LOG_ERR("Could not configure SDIO PWR GPIO");
			return ret;
		}
	} else {
		LOG_WRN("Not using SDIO PWR GPIO");
	}

	sys_write32(0x3,config->base + SDIO_STATUS);

	return 0;
}

#define PUPPY_SDHC_INIT(idx)                                                                       \
                                                                                                   \
	const struct sdhc_puppy_config sdhc_puppy_##idx##_config = {                               \
		.base = DT_INST_REG_ADDR(idx),                                                     \
		.pwr_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, pwr_gpios, {0})};                        \
                                                                                                   \
	struct sdhc_puppy_data sdhc_puppy_##idx##_data = {                                         \
		.io = {.clock = SD_CLOCK_25MHZ,                                                    \
		       .timing = SDHC_TIMING_LEGACY,                                               \
		       .power_mode = SDHC_POWER_ON,                                                \
		       .bus_width = SDHC_BUS_WIDTH1BIT,                                            \
		       .bus_mode = SDHC_BUSMODE_PUSHPULL,                                          \
		       .driver_type = SD_DRIVER_TYPE_A,                                            \
		       .signal_voltage = SD_VOL_3_3_V},                                            \
		.props = {.f_max = sys_clock_hw_cycles_per_sec(),                                  \
			  .f_min = sys_clock_hw_cycles_per_sec() / 255,                            \
			  .is_spi = false,                                                         \
			  .power_delay = 1,                                                        \
			  .host_caps = {.vol_180_support = false,                                  \
					.vol_300_support = false,                                  \
					.vol_330_support = true,                                   \
					.suspend_res_support = false,                              \
					.sdma_support = true,                                      \
					.high_spd_support = true,                                  \
					.adma_2_support = false,                                   \
					.max_blk_len = 0,                                          \
					.ddr50_support = false,                                    \
					.sdr104_support = false,                                   \
					.sdr50_support = false,                                    \
					.bus_8_bit_support = false,                                \
					.bus_4_bit_support = true,                                 \
					.hs200_support = false,                                    \
					.hs400_support = false}}};                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, sdhc_puppy_init, &sdhc_puppy_##idx##_config,                    \
			      &sdhc_puppy_##idx##_data, NULL, POST_KERNEL,                         \
			      CONFIG_SDHC_INIT_PRIORITY, &sdhc_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_SDHC_INIT);
