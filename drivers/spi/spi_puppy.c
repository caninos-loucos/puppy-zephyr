/*
 * Copyright (c) 2026 Ana Clara Forcelli - LSITEC
 * Copyright (c) 2026 Edgar Bernardi Righi - LSITEC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_spi

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_puppy);

#include <zephyr/sys/util.h>
#include <string.h>
#include "spi_puppy.h"
#include "spi_context.h"

#define SPI_PUPPY_MAX_BUFFER_SIZE (512)

struct spi_puppy_data {
    struct spi_context ctx;
    uint32_t base;
    uint32_t id;
    uint32_t slave;

    uint32_t cpol, cpha, clkdiv;
    uint32_t quad, lsb;

    uint8_t txbuf[SPI_PUPPY_MAX_BUFFER_SIZE];
    uint8_t rxbuf[SPI_PUPPY_MAX_BUFFER_SIZE];
    bool first;
    bool hw_cs;
};

static bool spi_puppy_xfer(const struct device *dev)
{
    struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
    struct spi_context *ctx = &data->ctx;
    const uint8_t *tx_buf = ctx->tx_buf;
    size_t chunk_len_bytes, len_bytes;
    uint8_t *rx_buf = ctx->rx_buf;
    uint32_t cmd_buf[8];
    bool last = false;
    int i = 0;

    chunk_len_bytes = spi_context_max_continuous_chunk(ctx);
    len_bytes = MIN(chunk_len_bytes, SPI_PUPPY_MAX_BUFFER_SIZE);

    if (!tx_buf) {
        tx_buf = &data->txbuf[0];
    }
    if (!rx_buf) {
        rx_buf = &data->rxbuf[0];
    }

    if (data->first)
    {
        cmd_buf[i++] = SPI_CMD_CFG(data->clkdiv, data->cpol, data->cpha);
        
        if (data->hw_cs) {
            cmd_buf[i++] = SPI_CMD_SOT(data->slave);
        }
        else {
            spi_context_cs_control(ctx, true);
            cmd_buf[i++] = SPI_CMD_SOT(0);
        }
        data->first = false;
    }

    cmd_buf[i++] = SPI_CMD_FUL(len_bytes, SPI_CMD_1_WORD_PER_TRANSF, 8,
                               data->lsb);

    while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_TX_OFFSET));
    while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_RX_OFFSET));
    while (!plp_udma_canEnqueue(data->base + UDMA_CHANNEL_CMD_OFFSET));

    spi_context_update_tx(ctx, 1, len_bytes);
    spi_context_update_rx(ctx, 1, len_bytes);

    if (!spi_context_tx_on(ctx) && !spi_context_rx_on(ctx))
    {
        cmd_buf[i++] = SPI_CMD_EOT(0, 0);
        last = true;
    }

    plp_udma_enqueue(data->base + UDMA_CHANNEL_TX_OFFSET,
                     (uint32_t)&tx_buf[0], len_bytes, UDMA_CHANNEL_CFG_SIZE_8);
    plp_udma_enqueue(data->base + UDMA_CHANNEL_RX_OFFSET,
                     (uint32_t)&rx_buf[0], len_bytes, UDMA_CHANNEL_CFG_SIZE_8);
    plp_udma_enqueue(data->base + UDMA_CHANNEL_CMD_OFFSET,
                     (uint32_t)&cmd_buf[0], i * 4, UDMA_CHANNEL_CFG_SIZE_32);

    if (last) {
        while (plp_udma_busy(data->base + UDMA_CHANNEL_TX_OFFSET));
        while (plp_udma_busy(data->base + UDMA_CHANNEL_RX_OFFSET));
        while (plp_udma_busy(data->base + UDMA_CHANNEL_CMD_OFFSET));
        
        if (!data->hw_cs) {
            spi_context_cs_control(ctx, false);
        }
    }
    return last;
}

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

static int spi_puppy_config(const struct device *dev,
                            const struct spi_config *config)
{
    struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
    uint32_t lines = config->operation & SPI_LINES_MASK;
    uint32_t mode = SPI_MODE_GET(config->operation);
    
    if (SPI_OP_MODE_GET(config->operation) & SPI_OP_MODE_SLAVE) {
        LOG_ERR("Slave mode not supported");
        return -ENOTSUP;
    }
    if (mode & SPI_MODE_LOOP) {
        LOG_ERR("Loopback mode is not supported");
        return -ENOTSUP;
    }
    if (config->operation & SPI_HALF_DUPLEX) {
        LOG_ERR("Half-duplex not supported");
        return -ENOTSUP;
    }
    if (lines & SPI_LINES_OCTAL) {
        LOG_ERR("Octal lines not supported");
        return -ENOTSUP;
    }

    data->cpol   = (mode & SPI_MODE_CPOL) ? 1U : 0U;
    data->cpha   = (mode & SPI_MODE_CPHA) ? 1U : 0U;
    data->quad   = (lines & SPI_LINES_QUAD) ? 1U : 0U;
    data->lsb    = (config->operation & SPI_TRANSFER_LSB) ? 1U : 0U;
    data->clkdiv = spi_puppy_calc_clkdiv(config->frequency);
    data->first  = true;
    data->hw_cs  = !spi_cs_is_gpio(config);
    data->slave  = config->slave;

    if (data->hw_cs && data->slave >= 4) {
        LOG_ERR("Slave count not supported by hw");
        return -ENOTSUP;
    }

    data->ctx.config = config;
    return 0;
}

static int spi_puppy_transceive(const struct device *dev,
                                const struct spi_config *config,
                                const struct spi_buf_set *tx_bufs,
                                const struct spi_buf_set *rx_bufs)
{
    struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
    int ret;

    spi_context_lock(&data->ctx, false, NULL, NULL, config);

    if ((ret = spi_puppy_config(dev, config)) < 0) {
        spi_context_release(&data->ctx, ret);
        return ret;
    }

    spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

    while (!spi_puppy_xfer(dev));

    spi_context_complete(&data->ctx, dev, 0);
    spi_context_release(&data->ctx, ret);
    return ret;
}

#ifdef CONFIG_SPI_ASYNC
static int spi_puppy_transceive_async(const struct device *dev,
                                      const struct spi_config *spi_cfg,
                                      const struct spi_buf_set *tx_bufs,
                                      const struct spi_buf_set *rx_bufs,
                                      spi_callback_t cb,
                                      void *userdata)
{
    return -ENOTSUP;
}
#endif

static int spi_puppy_release(const struct device *dev,
                             const struct spi_config *config)
{
    struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
    spi_context_unlock_unconditionally(&data->ctx);
    return 0;
}

static DEVICE_API(spi, spi_puppy_driver_api) = {
    .transceive = spi_puppy_transceive,
    .release = spi_puppy_release,
#ifdef CONFIG_SPI_ASYNC
    .transceive_async = spi_puppy_transceive_async,
#endif /* CONFIG_SPI_ASYNC */
};

static int spi_puppy_init(const struct device *dev)
{
    struct spi_puppy_data *data = (struct spi_puppy_data *)dev->data;
    int ret;

    memset(data->txbuf, 0, SPI_PUPPY_MAX_BUFFER_SIZE);

    if (data->id >= UDMA_SPI_PERIPH_COUNT) {
        LOG_ERR("Invalid spi_id: %d", data->id);
        return -EINVAL;
    }

    puppy_udma_clock_enable(PUPPY_SPI_ID_TO_UDMA_ID[data->id]);

    if ((ret = spi_context_cs_configure_all(&data->ctx)) < 0)
    {
        puppy_udma_clock_disable(PUPPY_SPI_ID_TO_UDMA_ID[data->id]);
        LOG_ERR("Failed to configure CS pins: %d", ret);
        return ret;
    }

    spi_context_unlock_unconditionally(&data->ctx);
    return 0;
}

#define SPI_PUPPY_INIT(idx)                                      \
    static struct spi_puppy_data spi_puppy_##idx##_data = {      \
        SPI_CONTEXT_INIT_LOCK(spi_puppy_##idx##_data, ctx),      \
        SPI_CONTEXT_INIT_SYNC(spi_puppy_##idx##_data, ctx),      \
        SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(idx), ctx)   \
        .base = DT_INST_REG_ADDR(idx),                           \
        .id = DT_INST_PROP(idx, spi_id),                         \
    };                                                           \
    SPI_DEVICE_DT_INST_DEFINE(idx,                               \
        spi_puppy_init, NULL, &spi_puppy_##idx##_data, NULL,     \
        POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,                   \
        &spi_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_PUPPY_INIT);
