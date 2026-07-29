/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_sdhc

#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <soc.h>

#include "sdhc_puppy.h"

LOG_MODULE_REGISTER(sdhc, CONFIG_SDHC_LOG_LEVEL);

struct sdhc_puppy_config {
	uint32_t base;
	struct gpio_dt_spec pwr_gpio;
	struct gpio_dt_spec dat0_gpio;
};

struct sdhc_puppy_data {
	volatile struct sdhc_puppy_t* sdhc;
	struct sdhc_io io;
	struct sdhc_host_props props;
};

struct sdhc_puppy_t {
	uint32_t SDIO_RX_SADDR;     // 0x0
	uint32_t SDIO_RX_SIZE;      // 0x4
	uint32_t SDIO_RX_CFG;       // 0x8
	uint32_t SDIO_RX_INITCFG;   // 0xC
	uint32_t SDIO_TX_SADDR;     // 0x10
	uint32_t SDIO_TX_SIZE;      // 0x14
	uint32_t SDIO_TX_CFG;       // 0x18
	uint32_t SDIO_TX_INITCFG;   // 0x1C
	uint32_t SDIO_CMD_OP;       // 0x20
	uint32_t SDIO_CMD_ARG;      // 0x24
	uint32_t SDIO_DATA_SETUP;   // 0x28
	uint32_t SDIO_START;        // 0x2c
	uint32_t SDIO_RSP0;         // 0x30
	uint32_t SDIO_RSP1;         // 0x34
	uint32_t SDIO_RSP2;         // 0x38
	uint32_t SDIO_RSP3;         // 0x3c
	uint32_t SDIO_CLK_DIV;      // 0x40
	uint32_t SDIO_STATUS;       // 0x44
	uint32_t SDIO_STOPCMD_OP;   // 0x48
	uint32_t SDIO_STOPCMD_ARG;  // 0x4c
	uint32_t SDIO_DATA_TIMEOUT; // 0x50
	uint32_t SDIO_BLOCKS_LEFT;  // 0x54
} __packed;

#define PUPPY_SDHC(x) ((volatile struct sdhc_puppy_t*)(DT_INST_REG_ADDR(x)))

void set_clk_div(const struct sdhc_puppy_data *data, int freq)
{
	uint32_t periph_freq = sys_clock_hw_cycles_per_sec();
	uint32_t div = periph_freq / (freq * 2);

	data->sdhc->SDIO_CLK_DIV = (1 << 8) | (div & 0xff);
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
	const struct sdhc_puppy_data *data = dev->data;

	pull_padpull();
	
	k_usleep(100);

	release_padpull();

	// Clear Status bits
	data->sdhc->SDIO_STATUS = EOT_MASK | ERROR_MASK;

	// translate opcode to reg layout
	data->sdhc->SDIO_CMD_OP = SD_GO_IDLE_STATE << CMD_OP_OFST | SD_RSP_TYPE_NONE;
	data->sdhc->SDIO_CMD_ARG = 0;
	data->sdhc->SDIO_START = 1;

	return 0;
}

void send_data(struct sdhc_puppy_data *dev_data, struct sdhc_command *cmd, struct sdhc_data *data)
{
	uint32_t response;
	int direction;

	switch(cmd->response_type) {
		case(SD_RSP_TYPE_NONE):
			response = NO_RSP;
			break;
		case(SD_RSP_TYPE_R5b):
		case(SD_RSP_TYPE_R1b):
			response = RSP_48_BUSY_CHK;
			break;
		case(SD_RSP_TYPE_R1):
		case(SD_RSP_TYPE_R5):
		case(SD_RSP_TYPE_R6):
		case(SD_RSP_TYPE_R7):
			response = RSP_48_CRC;
			break;
		case(SD_RSP_TYPE_R3):
		case(SD_RSP_TYPE_R4):
			response = RSP_48_NO_CRC;
			break;
		case(SD_RSP_TYPE_R2):
			response = RSP_136;
			break;
		default:
			response = NO_RSP;
			break;
	};

	dev_data->sdhc->SDIO_CMD_OP = cmd->opcode << CMD_OP_OFST | response;
	dev_data->sdhc->SDIO_CMD_ARG = cmd->arg;
	dev_data->sdhc->SDIO_START = 1;

	if (data != NULL) {
		// direction 1 is read, 0 is write
		direction = (cmd->opcode == SD_READ_SINGLE_BLOCK ||
			     cmd->opcode == SD_READ_MULTIPLE_BLOCK)
				    ? 1
				    : 0;

		plp_udma_enqueue(direction ? dev_data->sdhc->SDIO_RX_SADDR : dev_data->sdhc->SDIO_TX_SADDR,
				 (uint32_t)data->data, data->block_size * data->blocks,
				 UDMA_CHANNEL_CFG_EN | UDMA_CHANNEL_CFG_SIZE_32);

		dev_data->sdhc->SDIO_DATA_SETUP = 
			    (BLOCK_SIZE(data->block_size) | BLOCK_NUM((data->blocks - 1)) |
			     DATA_QUAD((dev_data->io.bus_width == 4) ? 1 : 0) |
			     DATA_RWN(direction ? 1 : 0) | DATA_EN(1));

		// Set data timeout in sdio clock cycles
		dev_data->sdhc->SDIO_DATA_TIMEOUT = data->timeout_ms * dev_data->io.clock / 1000;
	}
}

static int sdhc_puppy_request(const struct device *dev, struct sdhc_command *cmd,
			      struct sdhc_data *data)
{
	struct sdhc_puppy_data *dev_data = dev->data;
	int retries = cmd->retries;

retry:
	send_data(dev_data, cmd, data);
	while ((dev_data->sdhc->SDIO_STATUS & (ERROR_MASK & EOT_MASK)) == 0);

	dev_data->sdhc->SDIO_DATA_SETUP = 0;

	if (dev_data->sdhc->SDIO_STATUS & ERROR_MASK) {
		retries--;
		if (retries >= 0) {
			goto retry;
		}

		switch (dev_data->sdhc->SDIO_STATUS & ERR_STATUS_MASK) {
		// nao verifica crc com o tipo de resposta r3 E R4, acho que o controlador ja faz isso
		case CMD_ERR_WRONG_CRC:
			LOG_ERR("Wrong CRC");
			return -EIO;
		case CMD_ERR_WRONG_DIR:
			LOG_ERR("Wrong Direction");
			return -EIO;
		// verifica busy se o comando pede busy, mas acho que o controlador ja pede isso né
		case CMD_ERR_BUSY_TIMEOUT:
		case CMD_ERR_TIMEOUT:
		case DATA_ERR_TIMEOUT:
			LOG_ERR("Timed Out");
			return -ETIMEDOUT;
		default:
			LOG_ERR("WHAT");
			return -ETIMEDOUT;
		}
	}

	// resp 136 ver a ordem em que da as informações 
	switch(cmd->response_type){
		case(SD_RSP_TYPE_NONE):
			break;
		case(SD_RSP_TYPE_R1):
		case(SD_RSP_TYPE_R1b):
		case(SD_RSP_TYPE_R3):
		case(SD_RSP_TYPE_R4):
		case(SD_RSP_TYPE_R5):
		case(SD_RSP_TYPE_R5b):
		case(SD_RSP_TYPE_R6):
		case(SD_RSP_TYPE_R7):
		default:
			cmd->response[0] = dev_data->sdhc->SDIO_RSP0; //ver se ja tá shiftado
			cmd->response[1] = dev_data->sdhc->SDIO_RSP1;
			cmd->response[2] = dev_data->sdhc->SDIO_RSP2;
			cmd->response[3] = dev_data->sdhc->SDIO_RSP3;
			break;
		case(SD_RSP_TYPE_R2):
			cmd->response[0] = dev_data->sdhc->SDIO_RSP0; //precisa mesmo trocar a endianness nesse caso?
			cmd->response[1] = dev_data->sdhc->SDIO_RSP1;
			cmd->response[2] = dev_data->sdhc->SDIO_RSP2;
			cmd->response[3] = dev_data->sdhc->SDIO_RSP3;
			break;
	};

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

	// ve oq acontece com clock 0
	if (ios->clock > 0) {
		if (ios->clock > data->props.f_max || ios->clock < data->props.f_min) {
			LOG_ERR("Proposed clock outside supported host range");
			ret = -EINVAL;
		} else {
			set_clk_div(data, ios->clock);
			data->io.clock = (uint32_t)ios->clock;			
		}
	} else if (ios->clock == 0) {
		data->sdhc->SDIO_CLK_DIV = 0;
		data->io.clock = (uint32_t)ios->clock;			
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

	if ((data->io.power_mode != ios->power_mode) && gpio_is_ready_dt(&config->pwr_gpio)) {
		if (ios->power_mode == SDHC_POWER_OFF) {
			ret = gpio_pin_set_dt(&config->pwr_gpio, 0);
		} else if (ios->power_mode == SDHC_POWER_ON) {
			ret = gpio_pin_set_dt(&config->pwr_gpio, 1);
		}
		if (ret)
		{
			LOG_ERR("Could not set PWR GPIO, error %d", ret);
			return ret;
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

static int sdhc_puppy_card_busy(const struct device* dev)
{
	const struct sdhc_puppy_config *config = dev->config;
	int busy, ret;

	if(!gpio_is_ready_dt(&config->dat0_gpio)) {
		LOG_ERR("dat0 gpio not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->dat0_gpio, GPIO_INPUT | GPIO_ACTIVE_LOW | GPIO_PULL_UP);

	if(ret) {
		LOG_ERR("Could not set dat0 as gpio, %d", ret);
		return ret;
	}

	busy = gpio_pin_get_dt(&config->dat0_gpio);
	config_pad_func(22, 0x0); // Return to SDHC pad

	return busy;
}

static DEVICE_API(sdhc, sdhc_puppy_driver_api) = {
	.reset = sdhc_puppy_reset,
	.request = sdhc_puppy_request,
	.set_io = sdhc_puppy_set_io,
	.get_host_props = sdhc_puppy_get_host_props,
	.card_busy = sdhc_puppy_card_busy,
};

static int sdhc_puppy_init(const struct device *dev)
{
	const struct sdhc_puppy_config *config = dev->config;
	const struct sdhc_puppy_data *data = dev->data;
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

	// Clear Status bits
	data->sdhc->SDIO_STATUS = EOT_MASK | ERROR_MASK;

	return 0;
}

#define PUPPY_SDHC_INIT(idx)                                                                       \
                                                                                                   \
	const struct sdhc_puppy_config sdhc_puppy_##idx##_config = {                               \
		.pwr_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, pwr_gpios, {NULL}),                        \
		.dat0_gpio = GPIO_DT_SPEC_INST_GET(idx, dat0_gpios)};                        \
                                                                                                   \
	struct sdhc_puppy_data sdhc_puppy_##idx##_data = {                                         \
		.sdhc = PUPPY_SDHC(idx),															\
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
