/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_udma_filter

#include <zephyr/drivers/hwaccel.h>
#include <zephyr/drivers/clock_control.h>

#define LOG_LEVEL CONFIG_HWACCEL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(puppy_udma_filter);

#include "puppy_udma_filter.h"

struct puppy_udma_filter_chan {
	uint32_t addr;
	uint32_t cfg;
	uint32_t len_x;
	uint32_t len_y;
	uint32_t len_z;
};

struct puppy_udma_filter {
	struct puppy_udma_filter_chan tx0;
	struct puppy_udma_filter_chan tx1;
	struct puppy_udma_filter_chan rx;
	uint32_t au_cfg;
	uint32_t au_regsum;
	uint32_t au_regmult;
	uint32_t bincu_th;
	uint32_t bincu_cnt;
	uint32_t bincu_setup;
	uint32_t bincu_val;
	uint32_t filt_mode;
	uint32_t filt_start;
	uint32_t status;
};

struct puppy_udma_filter_config {
	struct accel_driver_config hw_config;
	const struct device *clk_dev;
	clock_control_subsys_t clk_bits;
};

struct puppy_udma_filter_data {
	struct accel_driver_data hw_data;
	hwaccel_irq_callback_user_data_t callback;
	void *user_data;
	volatile struct puppy_udma_filter *filt;
	uint32_t current_shift;
	bool use_shift;
	bool use_regmult;
	bool use_regsum;
};

#define PUPPY_FILTER(x) (volatile struct puppy_udma_filter *)(DT_INST_REG_ADDR(x))

static int puppy_udma_filter_query_hw_caps(const struct device *dev, accel_hw_caps_t *caps)
{
	const struct puppy_udma_filter_config *config =
		(const struct puppy_udma_filter_config *)dev->config;

	LOG_DBG("Caps mask: \n\tOP: 0x%x\n\tFMT: 0x%x\n\tCHAN_DIM: %d, N_CHANS: "
		"%d",
		config->hw_config.caps.op_caps, config->hw_config.caps.fmt_caps,
		config->hw_config.caps.max_chan_dimension,
		config->hw_config.caps.max_input_buffers);

	*caps = config->hw_config.caps;
	return 0;
}

static int puppy_udma_filter_start(const struct device *dev)
{
	struct puppy_udma_filter_data *data = (struct puppy_udma_filter_data *)dev->data;
	LOG_DBG("Filter @0x%lx Start", (mem_addr_t)data->filt);
	data->filt->filt_start = 1;
	return 0;
}

static int puppy_udma_filter_abort(const struct device *dev)
{
	struct puppy_udma_filter_data *data = (struct puppy_udma_filter_data *)dev->data;
	LOG_DBG("Filter @0x%lx Abort", (mem_addr_t)data->filt);
	data->filt->status = 0;
	return 0;
}

void calc_bytes(int *bytes, int *block_size_bytes, accel_buffer_t *buf,
		volatile struct puppy_udma_filter_chan *chan)
{
	*(bytes) = 0;

	switch (buf->fmt) {
	case (FMT_UINT8):
	case (FMT_INT8):
	default:
		*(block_size_bytes) = 1;
		break;
	case (FMT_UINT16):
	case (FMT_INT16):
		*(block_size_bytes) = 2;
		break;
	case (FMT_UINT32):
	case (FMT_INT32):
		*(block_size_bytes) = 4;
		break;
	}

	LOG_DBG("Block Size Bytes = %d", *(block_size_bytes));

	for (int i = 0; i < buf->dim; i++) {
		switch (i) {
		case (0):
			chan->len_x = buf->len[i];
			LOG_DBG("Chan X len is %d blocks", buf->len[i]);
			break; // does this break leave the for?
		case (1):
			chan->len_y = buf->len[i];
			LOG_DBG("Chan Y len is %d blocks", buf->len[i]);
			break;
		case (2):
			chan->len_z = buf->len[i];
			LOG_DBG("Chan Z len is %d blocks", buf->len[i]);
			break;
		default:
			break;
		}
		*(bytes) += buf->len[i] * *(block_size_bytes);
	}

	LOG_DBG("Transfer size is %d bytes", *(bytes));
}

static int puppy_udma_filter_set_buffers(const struct device *dev, accel_buffer_t **in_bufs,
					 int nbufs, accel_buffer_t *out_buf)
{
	struct puppy_udma_filter_data *data = (struct puppy_udma_filter_data *)dev->data;
	int bytes, block_size_bytes;

	if (out_buf->fmt != in_bufs[0]->fmt) {
		LOG_ERR("Buffer formats must match! (A: %d, OUT: %d)", in_bufs[0]->fmt,
			out_buf->fmt);
		return -EINVAL;
	}

	LOG_DBG("Buffers: \n\tA: 0x%lx dim: %d", (mem_addr_t)in_bufs[0]->buf, in_bufs[0]->dim);
	if (nbufs == 2) {
		LOG_DBG("\tB: 0x%lx dim: %d", (mem_addr_t)in_bufs[1]->buf, in_bufs[1]->dim);
	}
	LOG_DBG("\tOUT: 0x%lx dim: %d", (mem_addr_t)out_buf->buf, out_buf->dim);

	calc_bytes(&bytes, &block_size_bytes, in_bufs[0], &data->filt->tx0);

	switch (in_bufs[0]->fmt) {
	case (FMT_UINT32):
	case (FMT_UINT16):
	case (FMT_UINT8):
		data->filt->au_cfg &= ~(0x1);
		break;
	case (FMT_INT32):
	case (FMT_INT16):
	case (FMT_INT8):
		data->filt->au_cfg |= 0x1;
		break;
	default:
		break;
	}

	/* TODO: add matrix support */
	data->filt->tx0.cfg = in_bufs[0]->dim == 2 ? TX_CFG(TX_MODE_SLIDING, block_size_bytes >> 1)
						   : TX_CFG(TX_MODE_LINEAR, block_size_bytes >> 1);
	data->filt->tx0.addr = (uint32_t)in_bufs[0]->buf;

	if (nbufs == 2) {
		if (in_bufs[1]->fmt != in_bufs[0]->fmt) {
			LOG_ERR("Buffer formats must match! (A: %d, B: %d)", in_bufs[0]->fmt,
				in_bufs[1]->fmt);
			return -EINVAL;
		}

		if (in_bufs[1]->dim != 0) // if not scalar
		{
			calc_bytes(&bytes, &block_size_bytes, in_bufs[1], &data->filt->tx1);
			data->filt->tx1.cfg =
				in_bufs[1]->dim == 2
					? TX_CFG(TX_MODE_SLIDING, block_size_bytes >> 1)
					: TX_CFG(TX_MODE_LINEAR, block_size_bytes >> 1);
			data->filt->tx1.addr = (uint32_t)in_bufs[1]->buf;
		} else {
			if (data->use_regmult) {
				data->filt->au_regmult = ((uint32_t)in_bufs[1]->buf);
			} else if (data->use_regsum) {
				data->filt->au_regsum = ((uint32_t)in_bufs[1]->buf);
			} else {
				LOG_ERR("Invalid dimension");
				return -EINVAL;
			}
		}
	}

	calc_bytes(&bytes, &block_size_bytes, out_buf, &data->filt->rx);
	data->filt->rx.cfg = out_buf->dim == 2 ? RX_CFG(RX_MODE_2D_ROW, block_size_bytes >> 1)
					       : RX_CFG(RX_MODE_LINEAR, block_size_bytes >> 1);
	data->filt->rx.addr = (uint32_t)out_buf->buf;

	return 0;
}

static int puppy_udma_filter_configure_ops(const struct device *dev, accel_hw_ops_t ops)
{
	struct puppy_udma_filter_data *data = (struct puppy_udma_filter_data *)dev->data;

	data->filt->au_cfg &= ~(0xFF << AU_MODE_OFFSET);

	switch (ops) {
	case HW_OP_SUM:
		data->filt->au_cfg |= AU_MODE_A_PLUS_B << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_MULT:
		data->filt->au_cfg |= AU_MODE_AxB << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_SUB:
		data->filt->au_cfg |= AU_MODE_A_MINUS_B << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_SQUARE:
		data->filt->au_cfg |= AU_MODE_AxA << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 2;
		break;
	case HW_OP_SUM_SCALAR:
		data->filt->au_cfg |= AU_MODE_A_PLUS_REGSUM << AU_MODE_OFFSET;
		data->use_regsum = true;
		data->use_regmult = false;
		data->filt->filt_mode = 2;
		break;
	case HW_OP_MULT_SCALAR:
		data->filt->au_cfg |= AU_MODE_AxREGMULT << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = true;
		data->filt->filt_mode = 2;
		break;
	case HW_OP_MAC:
		data->filt->au_cfg |= AU_MODE_AxB_ACC << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_SQUARE_ACC:
		data->filt->au_cfg |= AU_MODE_AxA_ACC << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_SQUARE_SUM:
		data->filt->au_cfg |= AU_MODE_AxA_PLUS_B << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	case HW_OP_SQUARE_SUB:
		data->filt->au_cfg |= AU_MODE_AxA_MINUS_B << AU_MODE_OFFSET;
		data->use_regsum = false;
		data->use_regmult = false;
		data->filt->filt_mode = 0;
		break;
	default:
		LOG_ERR("OP not supported");
		break;
	}

	LOG_DBG("Config AU with OPS %d", ops);

	return 0;
}

static int puppy_udma_check_status(const struct device *dev)
{
	struct puppy_udma_filter_data *data = dev->data;
	return data->filt->status ? 0 : -EBUSY;
}

static int puppy_udma_filter_irq_callback_set(const struct device *dev,
					      hwaccel_irq_callback_user_data_t cb, void *user_data)
{
	struct puppy_udma_filter_data *data = dev->data;

	data->callback = cb;
	data->user_data = user_data;

	return 0;
}

static void puppy_udma_filter_irq(void *userdata)
{
	const struct device *dev = (const struct device *)userdata;
	struct puppy_udma_filter_data *data = dev->data;

	if (data->callback != NULL) {
		data->callback(dev, data->user_data);
	}
}

static int puppy_udma_common_filter_init(const struct device *dev)
{
	const struct puppy_udma_filter_config *config = dev->config;

	int ret = clock_control_on(config->clk_dev, config->clk_bits);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static struct accel_driver_api puppy_udma_filter_accel_api = {
	.query_hw_caps = puppy_udma_filter_query_hw_caps,
	.configure_ops = puppy_udma_filter_configure_ops,
	.set_buffers = puppy_udma_filter_set_buffers,
	.set_callback = puppy_udma_filter_irq_callback_set,
	.start = puppy_udma_filter_start,
	.abort = puppy_udma_filter_abort,
	.check_status = puppy_udma_check_status};

#define PUPPY_FILTER_INIT(idx)                                                                     \
	static int puppy_udma_filter_##idx##_init(const struct device *dev)                        \
	{                                                                                          \
		int ret = puppy_udma_common_filter_init(dev);                                      \
		if (ret != 0) {                                                                    \
			return ret;                                                                \
		}                                                                                  \
		IRQ_CONNECT(DT_INST_IRQN(idx), 0, puppy_udma_filter_irq, DEVICE_DT_INST_GET(idx),  \
			    0);                                                                    \
		irq_enable(DT_INST_IRQN(idx));                                                     \
		return 0;                                                                          \
	}                                                                                          \
	static struct puppy_udma_filter_data puppy_udma_filter_##idx##_data = {                    \
		.use_shift = 0,                                                                    \
		.current_shift = 0,                                                                \
		.hw_data =                                                                         \
			{                                                                          \
				.cb = NULL,                                                        \
				.user_data = NULL,                                                 \
			},                                                                         \
		.filt = PUPPY_FILTER(idx),                                                         \
	};                                                                                         \
	const static struct puppy_udma_filter_config puppy_udma_filter_##idx##_config = {          \
		.hw_config =                                                                       \
			{                                                                          \
				.caps =                                                            \
					{                                                          \
						.op_caps =                                         \
							BIT(HW_OP_SUM) | BIT(HW_OP_SUB) |          \
							BIT(HW_OP_MULT) | BIT(HW_OP_SUM_SCALAR) |  \
							BIT(HW_OP_SUB_SCALAR) |                    \
							BIT(HW_OP_MULT_SCALAR) |                   \
							BIT(HW_OP_SQUARE) | BIT(HW_OP_MAC) |       \
							BIT(HW_OP_SHIFT) | BIT(HW_OP_SQUARE_SUM) | \
							BIT(HW_OP_SQUARE_SUB) |                    \
							BIT(HW_OP_SQUARE_ACC),                     \
						.fmt_caps = BIT(FMT_UINT8) | BIT(FMT_UINT16) |     \
							    BIT(FMT_UINT32) | BIT(FMT_INT8) |      \
							    BIT(FMT_INT16) | BIT(FMT_INT32),       \
						.max_input_buffers = 2,                            \
						.max_chan_dimension = 3,                           \
					},                                                         \
			},                                                                         \
		.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                                \
		.clk_bits = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, bits),                \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, &puppy_udma_filter_##idx##_init, NULL,                          \
			      &puppy_udma_filter_##idx##_data, &puppy_udma_filter_##idx##_config,  \
			      POST_KERNEL, CONFIG_HWACCEL_INIT_PRIORITY,                           \
			      &puppy_udma_filter_accel_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_FILTER_INIT)
