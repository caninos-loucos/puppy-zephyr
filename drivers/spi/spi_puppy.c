#define DT_DRV_COMPAT caninos_puppy_spi

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "spi_puppy.h"

LOG_MODULE_REGISTER(spi_puppy, CONFIG_SPI_LOG_LEVEL);

#include "spi_context.h"

#define SPI_PUPPY_MAX_BUFFER_SIZE (512)

struct spi_puppy_data {
	struct spi_context ctx;
	struct spi_config *config;
	uint32_t cpol, cpha, clkdiv;
	uint32_t quad, lsb, holdcs;
	uint32_t base;

	uint8_t txbuf[SPI_PUPPY_MAX_BUFFER_SIZE];
	uint8_t rxbuf[SPI_PUPPY_MAX_BUFFER_SIZE];
	bool first;

	int id;
};

static uint32_t spi_puppy_calc_clkdiv(uint32_t frequency)
{
	uint32_t clkdiv = 0UL;

	if (frequency > 0UL && frequency < sys_clock_hw_cycles_per_sec()) {
		// Round-up the divider to obtain an SPI frequency which is
		// below the maximum
		clkdiv = (sys_clock_hw_cycles_per_sec() + frequency - 1UL) / frequency;

		// The SPIM always divide by 2 once we activate the divider,
		// thus increase by 1
		// in case it is even to not go above the max frequency.
		if (clkdiv & 1UL) {
			clkdiv++;
		}
		clkdiv >>= 1;
	}
	return clkdiv;
}

static int spi_puppy_cfg(const struct device *dev, const struct spi_config *config)
{
	struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
	uint16_t operation = config->operation;

	if (operation & SPI_OP_MODE_SLAVE) {
		LOG_ERR("Slave mode not supported");
		return -ENOTSUP;
	}
	if (operation & SPI_HALF_DUPLEX) {
		LOG_ERR("Half-duplex not supported");
		return -ENOTSUP;
	}
	if (operation & SPI_MODE_LOOP) {
		LOG_ERR("Loopback mode is not supported");
		return -ENOTSUP;
	}
	data->cpol = (operation & SPI_MODE_CPOL) ? 1U : 0U;
	data->cpha = (operation & SPI_MODE_CPHA) ? 1U : 0U;
	data->quad = (operation & SPI_LINES_QUAD) ? 1U : 0U;
	data->lsb = (operation & SPI_TRANSFER_LSB) ? 1U : 0U;
	data->holdcs = (operation & SPI_HOLD_ON_CS) ? 1U : 0U;
	data->clkdiv = spi_puppy_calc_clkdiv(config->frequency);
	data->first = true;
	data->ctx.config = config;
	return 0;
}

static bool spi_puppy_xfer(const struct device *dev)
{
	struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
	struct spi_context *ctx = &data->ctx;

	size_t chunk_len_bytes = spi_context_max_continuous_chunk(ctx);
	size_t len_bytes = MIN(chunk_len_bytes, SPI_PUPPY_MAX_BUFFER_SIZE);

	const uint8_t *tx_buf = (ctx->tx_buf != NULL) ? ctx->tx_buf : &data->txbuf[0];

	uint8_t *rx_buf = (ctx->rx_buf != NULL) ? ctx->rx_buf : &data->rxbuf[0];

	uint32_t cmd_buf[8];
	bool last = false;
	int i = 0;

	if (data->first) {
		cmd_buf[i++] = SPI_CMD_CFG(data->clkdiv, data->cpol, data->cpha);
		cmd_buf[i++] = SPI_CMD_SOT(0);
		data->first = false;
	}

	cmd_buf[i++] = SPI_CMD_FUL(len_bytes, SPI_CMD_1_WORD_PER_TRANSF, 8, data->lsb);

	while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_TX_OFFSET))
		;
	while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_RX_OFFSET))
		;
	while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_CMD_OFFSET))
		;

	spi_context_update_tx(ctx, 1, len_bytes);
	spi_context_update_rx(ctx, 1, len_bytes);

	if (!spi_context_tx_on(ctx) && !spi_context_rx_on(ctx)) {
		cmd_buf[i++] = SPI_CMD_EOT(1, data->holdcs);
		last = true;
	}

	plp_udma_enqueue(data->base + UDMA_CHANNEL_TX_OFFSET, (uint32_t)&tx_buf[0], len_bytes,
			 UDMA_CHANNEL_CFG_SIZE_8);

	plp_udma_enqueue(data->base + UDMA_CHANNEL_RX_OFFSET, (uint32_t)&rx_buf[0], len_bytes,
			 UDMA_CHANNEL_CFG_SIZE_8);

	plp_udma_enqueue(data->base + UDMA_CHANNEL_CMD_OFFSET, (uint32_t)&cmd_buf[0], i * 4,
			 UDMA_CHANNEL_CFG_SIZE_32);

	if (last) {
		while (plp_udma_busy(data->base + UDMA_CHANNEL_TX_OFFSET))
			;
		while (plp_udma_busy(data->base + UDMA_CHANNEL_RX_OFFSET))
			;
		while (plp_udma_busy(data->base + UDMA_CHANNEL_CMD_OFFSET))
			;
	}
	return last;
}

static int spi_puppy_transceive(const struct device *dev, const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
	int ret;

	spi_context_lock(&data->ctx, false, NULL, NULL, config);

	ret = spi_puppy_cfg(dev, config);

	if (ret < 0) {
		goto done;
	}

	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

	if (data->ctx.tx_buf == NULL && data->ctx.rx_buf == NULL) {
		goto done;
	}

	while (!spi_puppy_xfer(dev))
		;

	spi_context_complete(&data->ctx, dev, 0);

done:
	spi_context_release(&data->ctx, ret);
	return ret;
}

static DEVICE_API(spi, spi_puppy_driver_api) = {
	.transceive = spi_puppy_transceive,
};

static int spi_puppy_init(const struct device *dev)
{
	struct spi_puppy_data *data = dev->data;
	int ret;

	memset(&data->txbuf[0], 0, SPI_PUPPY_MAX_BUFFER_SIZE);

	// Set clock-gating config
	uint32_t cg_conf = plp_udma_cg_get();
	plp_udma_cg_set(cg_conf | BIT(UDMA_SPI0_ID + data->id));

	uint32_t int_conf = plp_udma_evtin_get();
	plp_udma_evtin_set(int_conf | ARCHI_UDMA_SPIM_RX_EVT(0) << 16);

	ret = spi_context_cs_configure_all(&data->ctx);

	if (ret < 0) {
		return ret;
	}

	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

#define PUPPY_SPI_INIT(idx)                                                                        \
	static struct spi_puppy_data spi_puppy_##idx##_data = {                                    \
		SPI_CONTEXT_INIT_LOCK(spi_puppy_##idx##_data, ctx),                                \
		SPI_CONTEXT_INIT_SYNC(spi_puppy_##idx##_data, ctx),                                \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(idx, ctx).base = DT_INST_REG_ADDR(idx),            \
		.id = DT_INST_PROP(idx, spi_id),                                                   \
	};                                                                                         \
	SPI_DEVICE_DT_INST_DEFINE(idx, spi_puppy_init, NULL, &spi_puppy_##idx##_data, NULL,        \
				  PRE_KERNEL_1, CONFIG_SPI_INIT_PRIORITY, &spi_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_SPI_INIT);
