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

/* No need for __packed because all fields are 32b */
struct puppy_udma_filter_chan {
	uint32_t addr;
	uint32_t cfg;
	uint32_t len_x;
	uint32_t len_y;
	uint32_t len_z;
};

/* No need for __packed because all fields are 32b */
struct puppy_udma_filter {
	struct puppy_udma_filter_chan tx0;
	struct puppy_udma_filter_chan tx1;
	struct puppy_udma_filter_chan rx;
	volatile uint32_t au_cfg;
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
	bool use_shift;
	bool use_regmult;
	bool use_regsum;
	uint32_t current_shift;
	uint32_t current_au_mode;
	volatile struct puppy_udma_filter *filt;
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
			break;
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

	bool regmult_set = false;
	bool regsum_set = false;
	bool shift_set = false;
	bool b_buffer_set = false;
	uint32_t temp_au;

	temp_au = sys_read32((uint32_t)data->filt + REG_AU_CFG);
	temp_au &= ~(0xFF << AU_SHIFT_OFFSET);

	if (out_buf->fmt != in_bufs[0]->fmt) {
		LOG_ERR("First buffer and Out Buffer formats must match! (A: %d, OUT: %d)",
			in_bufs[0]->fmt, out_buf->fmt);
		return -EINVAL;
	}

	LOG_DBG("Buffers:");
	for (int i = 0; i < nbufs; i++) {
		LOG_DBG("\t%d 0x%lx dim: %d", i, (mem_addr_t)in_bufs[i]->buf, in_bufs[i]->dim);
	}

	LOG_DBG("\tOUT: 0x%lx dim: %d", (mem_addr_t)out_buf->buf, out_buf->dim);

	calc_bytes(&bytes, &block_size_bytes, in_bufs[0], &data->filt->tx0);

	switch (in_bufs[0]->fmt) {
	case (FMT_UINT32):
	case (FMT_UINT16):
	case (FMT_UINT8):
		temp_au &= ~(BIT(AU_SIGNED_OFFSET));
		break;
	case (FMT_INT32):
	case (FMT_INT16):
	case (FMT_INT8):
		temp_au |= BIT(AU_SIGNED_OFFSET);
		break;
	default:
		break;
	}

	LOG_DBG("Setting IN buffers");

	LOG_DBG("IN buffer 0");
	data->filt->tx0.cfg = in_bufs[0]->dim == 2 ? TX_CFG(TX_MODE_SLIDING, block_size_bytes >> 1)
						   : TX_CFG(TX_MODE_LINEAR,  block_size_bytes >> 1);
	data->filt->tx0.addr = (uint32_t)in_bufs[0]->buf;

	for (int i = 1; i < nbufs; i++) {
		LOG_DBG("IN buffer %d", i);

		if (in_bufs[i]->fmt != in_bufs[0]->fmt) {
			LOG_ERR("Buffer formats must match! (A: %d, B: %d)",
				in_bufs[0]->fmt, in_bufs[i]->fmt);
			return -EINVAL;
		}

		if (in_bufs[i]->dim == 0) {
			if (data->use_regmult && !regmult_set) {
				data->filt->au_regmult = *(uint32_t*)in_bufs[i]->buf;
				regmult_set = true;
			} else if (data->use_regsum && !regsum_set) {
				data->filt->au_regsum = *(uint32_t*)in_bufs[i]->buf;
				regsum_set = true;
			} else if (data->use_shift && !shift_set) {
				temp_au |= *(uint32_t*)in_bufs[i]->buf << AU_SHIFT_OFFSET;
				shift_set = true;
			} else {
				LOG_ERR("Invalid dimension/operation");
				return -EINVAL;
			}

		} else if (!b_buffer_set) {
			calc_bytes(&bytes, &block_size_bytes, in_bufs[i], &data->filt->tx1);
			data->filt->tx1.cfg =
				in_bufs[i]->dim == 2
					? TX_CFG(TX_MODE_SLIDING, block_size_bytes >> 1)
					: TX_CFG(TX_MODE_LINEAR,  block_size_bytes >> 1);
			data->filt->tx1.addr = (uint32_t)in_bufs[i]->buf;
			b_buffer_set = true;
			
		} else {
			/* Might have to check before setting the buffers instead of during the
			 * loop*/
			LOG_ERR("Invalid buffers, choose only up to two vectors (A and B) and up "
				"to two scalars.");
			return -EINVAL;
		}
	}

	LOG_DBG("Setting OUT buffer");

	calc_bytes(&bytes, &block_size_bytes, out_buf, &data->filt->rx);
	data->filt->rx.cfg = out_buf->dim == 2 ? RX_CFG(RX_MODE_2D_ROW, block_size_bytes >> 1)
					       : RX_CFG(RX_MODE_LINEAR, block_size_bytes >> 1);
	data->filt->rx.addr = (uint32_t)out_buf->buf;

	sys_write32(temp_au, (uint32_t)data->filt + REG_AU_CFG);
	LOG_DBG("Config AU with AU_CFG = %x", temp_au);

	return 0;
}

static int puppy_udma_filter_configure_ops(const struct device *dev, uint32_t ops_mask)
{
	struct puppy_udma_filter_data *data = (struct puppy_udma_filter_data *)dev->data;

	data->use_shift = false;

	if ((ops_mask & BIT(HW_OP_MULT_SCALAR)) > 0) {
		data->use_regmult = true;
	} else {
		data->use_regmult = false;
	}

	if ((ops_mask & BIT(HW_OP_SUM_SCALAR)) > 0) {
		data->use_regsum = true;
	} else {
		data->use_regsum = false;
	}

	switch (ops_mask) {
	case BIT(HW_OP_SUM):
		data->current_au_mode = AU_MODE_A_PLUS_B;
		data->filt->filt_mode = 0;
		break;

	case BIT(HW_OP_SUB):
		data->current_au_mode = AU_MODE_A_MINUS_B;
		data->filt->filt_mode = 0;
		break;

	case BIT(HW_OP_MULT):
		data->current_au_mode = AU_MODE_AxB;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_MULT) | BIT(HW_OP_ACCUMULATE)):
		data->current_au_mode = AU_MODE_AxB_ACC;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_MULT) | BIT(HW_OP_SUM_SCALAR)):
		data->current_au_mode = AU_MODE_AxB_PLUS_REGSUM;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_SQUARE) | BIT(HW_OP_SUM)):
		data->current_au_mode = AU_MODE_AxA_PLUS_B;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_SQUARE) | BIT(HW_OP_SUB)):
		data->current_au_mode = AU_MODE_AxA_MINUS_B;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_MULT_SCALAR) | BIT(HW_OP_SUM)):
		data->current_au_mode = AU_MODE_AxREGMULT_PLUS_B;
		data->filt->filt_mode = 0;
		break;

	case (BIT(HW_OP_MULT_SCALAR) | BIT(HW_OP_SUB)):
		data->current_au_mode = AU_MODE_AxREGMULT_MINUS_B;
		data->filt->filt_mode = 0;
		break;

	case BIT(HW_OP_SHIFT):
		data->use_shift = true;
		data->current_au_mode = AU_MODE_A_PLUS_REGSUM;
		data->filt->filt_mode = 2;
		break;

	case BIT(HW_OP_SQUARE):
		data->current_au_mode = AU_MODE_AxA;
		data->filt->filt_mode = 2;
		break;

	case BIT(HW_OP_SUM_SCALAR):
		data->current_au_mode = AU_MODE_A_PLUS_REGSUM;
		data->filt->filt_mode = 2;
		break;

	case BIT(HW_OP_MULT_SCALAR):
		data->current_au_mode = AU_MODE_AxREGMULT;
		data->filt->filt_mode = 2;
		break;

	case (BIT(HW_OP_MULT_SCALAR) | BIT(HW_OP_ACCUMULATE)):
		data->current_au_mode = AU_MODE_AxREGMULT_ACC;
		data->filt->filt_mode = 2;
		break;

	case (BIT(HW_OP_SUM_SCALAR) | BIT(HW_OP_MULT_SCALAR)):
		data->current_au_mode = AU_MODE_AxREGMULT_PLUS_REGSUM;
		data->filt->filt_mode = 2;
		break;

	case (BIT(HW_OP_SQUARE) | BIT(HW_OP_ACCUMULATE)):
		data->current_au_mode = AU_MODE_AxA_ACC;
		data->filt->filt_mode = 2;
		break;

	case (BIT(HW_OP_SQUARE) | BIT(HW_OP_SUM_SCALAR)):
		data->current_au_mode = AU_MODE_AxA_PLUS_REGSUM;
		data->filt->filt_mode = 2;
		break;

	default:
		LOG_ERR("OP not supported");
		return -EINVAL;
	}

	uint32_t temp_au = sys_read32((uint32_t)data->filt + REG_AU_CFG);
	temp_au &= ~(0xFF << AU_MODE_OFFSET);
	temp_au |= data->current_au_mode << AU_MODE_OFFSET;

	sys_write32(temp_au, (uint32_t)data->filt + REG_AU_CFG);

	LOG_DBG("Config AU with OPS %d, AU_CFG = %x, current_au_mode %x", ops_mask, data->filt->au_cfg, temp_au);

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

	data->hw_data.cb = cb;
	data->hw_data.user_data = user_data;

	return 0;
}

static void puppy_udma_filter_irq(void *userdata)
{
	const struct device *dev = (const struct device *)userdata;
	struct puppy_udma_filter_data *data = dev->data;

	if (data->hw_data.cb != NULL) {
		data->hw_data.cb(dev, data->hw_data.user_data);
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
		.hw_data =                                                                         \
			{                                                                          \
				.cb = NULL,                                                        \
				.user_data = NULL,                                                 \
			},                                                                         \
		.use_shift = false,                                                                \
		.use_regmult = false,                                                              \
		.use_regsum = false,                                                               \
		.current_shift = 0,                                                                \
		.current_au_mode = 0,                                                              \
		.filt = PUPPY_FILTER(idx),                                                         \
	};                                                                                         \
	const static struct puppy_udma_filter_config puppy_udma_filter_##idx##_config = {          \
		.hw_config =                                                                       \
			{                                                                          \
				.caps =                                                            \
					{                                                          \
						.op_caps = BIT(HW_OP_SUM) | BIT(HW_OP_SUB) |       \
							   BIT(HW_OP_MULT) |                       \
							   BIT(HW_OP_SUM_SCALAR) |                 \
							   BIT(HW_OP_SUB_SCALAR) |                 \
							   BIT(HW_OP_MULT_SCALAR) |                \
							   BIT(HW_OP_SQUARE) | BIT(HW_OP_SHIFT) |  \
							   BIT(HW_OP_ACCUMULATE),                  \
						.fmt_caps = BIT(FMT_UINT8) | BIT(FMT_UINT16) |     \
							    BIT(FMT_UINT32) | BIT(FMT_INT8) |      \
							    BIT(FMT_INT16) | BIT(FMT_INT32),       \
						.max_input_buffers = 3,                            \
						.max_chan_dimension = 2,                           \
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
