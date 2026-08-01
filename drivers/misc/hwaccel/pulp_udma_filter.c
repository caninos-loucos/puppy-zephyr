/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT pulp_udma_filter

#include <zephyr/drivers/hwaccel.h>
#include <zephyr/drivers/clock_control.h>
#include <soc.h>

#include "pulp_udma_filter.h"

struct pulp_udma_filter_chan {
	uint32_t acc;
	uint32_t cfg;
	uint32_t len_x;
	uint32_t len_y;
	uint32_t len_z;
} __packed;

struct pulp_udma_filter {
	struct pulp_udma_filter_chan tx0;
	struct pulp_udma_filter_chan tx1;
	struct pulp_udma_filter_chan rx;
	uint32_t au_cfg;
	uint32_t au_regsum;
	uint32_t au_regmult;
	uint32_t bincu_th;
	uint32_t bincu_cnt;
	uint32_t bincu_setup;
	uint32_t bincu_val;
	uint32_t filt_start;
	uint32_t filt_cmd;
	uint32_t status;
} __packed;

struct pulp_udma_filter_config {
	struct accel_driver_config hw_config;
	const struct device *clk_dev;
	clock_control_subsys_t clk_bits;
};

struct pulp_udma_filter_data {
	struct accel_driver_data hw_data;
	hwaccel_irq_callback_user_data_t callback;
	void *user_data;
	volatile struct pulp_udma_filter *filt;
};

#define PULP_FILTER(x) (volatile struct pulp_udma_filter *)(DT_INST_REG_ADDR(x))
/*
Arithmetic Unit mode:
-4’b0000: AU_MODE_AxB
-4’b0001: AU_MODE_AxB+REG0
-4’b0010: AU_MODE_AxB accumulation
-4’b0011: AU_MODE_AxA
-4’b0100: AU_MODE_AxA+B
-4’b0101: AU_MODE_AxA-B
-4’b0110: AU_MODE_AxA accumulation
-4’b0111: AU_MODE_AxA+REG0
-4’b1000: AU_MODE_AxREG1
-4’b1001: AU_MODE_AxREG1+B
-4’b1010: AU_MODE_AxREG1-B
-4’b1011: AU_MODE_AxREG1+REG0
-4’b1100: AU_MODE_AxREG1 accumulation
-4’b1101: AU_MODE_A+B
-4’b1110: AU_MODE_A-B
-4’b1111: AU_MODE_A+REG0

 FILTER component manages the following features:
- Thresholding
- Binarization
- Memory copy
- Transpose
- Convolution 1D
- Vector operation]

how to do them? idk
*/

static int pulp_udma_filter_query_hw_caps(const struct device *dev, const accel_hw_caps_t *caps)
{
	const struct pulp_udma_filter_config *config =
		(const struct pulp_udma_filter_config *)dev->config;
	caps = &config->hw_config.caps;
	return 0;
}

static int pulp_udma_filter_start(const struct device *dev)
{
	struct pulp_udma_filter_data *data = (struct pulp_udma_filter_data *)dev->data;
	data->filt->filt_start = 1;
	return 0;
}

static int pulp_udma_filter_abort(const struct device *dev)
{
	struct pulp_udma_filter_data *data = (struct pulp_udma_filter_data *)dev->data;
	plp_udma_clr((uint32_t)&data->filt->tx0);
	plp_udma_clr((uint32_t)&data->filt->tx1);
	plp_udma_clr((uint32_t)&data->filt->rx);
	return 0;
}

void calc_bytes(int *bytes, int *block_size_bytes, accel_buffer_t *buf,
		volatile struct pulp_udma_filter_chan *chan)
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

	for (int i = 0; i < buf->dim; i++) {
		*(bytes) += buf->len[i] * *(block_size_bytes);
		switch (i) {
		case (0):
			chan->len_x = buf->len[i];
			break;
		case (1):
			chan->len_y = buf->len[i];
			break;
		case (2):
			chan->len_z = buf->len[i];
			break;
		default:
			break;
		}
	}
}

static int pulp_udma_filter_set_buffers(const struct device *dev, accel_buffer_t **in_bufs,
					int nbufs, accel_buffer_t *out_buf)
{
	struct pulp_udma_filter_data *data = (struct pulp_udma_filter_data *)dev->data;
	int bytes, block_size_bytes;

	calc_bytes(&bytes, &block_size_bytes, in_bufs[0], &data->filt->tx0);
	plp_udma_enqueue((uint32_t)&data->filt->tx0, (uint32_t)in_bufs[0]->buf, bytes,
			 block_size_bytes >> 1);

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

	// add different dim support
	data->filt->tx0.cfg = TX_CFG(TX_MODE_LINEAR, block_size_bytes >> 1);

	if (nbufs == 2) {
		if (in_bufs[1]->fmt != in_bufs[0]->fmt) {
			return -EINVAL;
		}

		calc_bytes(&bytes, &block_size_bytes, in_bufs[1], &data->filt->tx1);
		plp_udma_enqueue((uint32_t)&data->filt->tx1, (uint32_t)in_bufs[1]->buf, bytes,
				 block_size_bytes >> 1);
		data->filt->tx1.cfg = TX_CFG(TX_MODE_LINEAR, block_size_bytes >> 1);
	}

	if (out_buf->fmt != in_bufs[0]->fmt) {
		return -EINVAL;
	}

	calc_bytes(&bytes, &block_size_bytes, out_buf, &data->filt->rx);
	plp_udma_enqueue((uint32_t)&data->filt->rx, (uint32_t)out_buf->buf, bytes,
			 block_size_bytes >> 1);
	data->filt->rx.cfg = RX_CFG(RX_MODE_LINEAR, block_size_bytes >> 1);

	return 0;
}

static int pulp_udma_filter_configure_ops(const struct device *dev, accel_hw_ops_t *ops_series,
					  int nops)
{
	struct pulp_udma_filter_data *data = (struct pulp_udma_filter_data *)dev->data;

	switch (ops_series[0]) {
	case (HW_OP_SUM):
		data->filt->au_cfg |= AU_CFG(0, AU_MODE_A_PLUS_B, 0, 0);
		break;
	case (HW_OP_MULT):
		data->filt->au_cfg |= AU_CFG(0, AU_MODE_AxB, 0, 0);
		break;
	case (HW_OP_SUB):
		data->filt->au_cfg |= AU_CFG(0, AU_MODE_A_MINUS_B, 0, 0);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int pulp_udma_filter_irq_callback_set(const struct device *dev,
					      hwaccel_irq_callback_user_data_t cb, void *user_data)
{
	struct pulp_udma_filter_data *data = dev->data;

	data->callback = cb;
	data->user_data = user_data;

	return 0;
}

static void pulp_udma_filter_irq(void *userdata)
{
	const struct device *dev = (const struct device *)userdata;
	struct pulp_udma_filter_data *data = dev->data;

	if (data->callback != NULL) {
		data->callback(dev, data->user_data);
	}
}

static int pulp_udma_common_filter_init(const struct device *dev)
{
	const struct pulp_udma_filter_config *config = dev->config;
	int ret = clock_control_on(config->clk_dev, config->clk_bits);
	if (ret != 0) {
		return ret;
	}
	return 0;
}

static struct accel_driver_api pulp_udma_filter_accel_api = {
	.query_hw_caps = pulp_udma_filter_query_hw_caps,
	.configure_ops = pulp_udma_filter_configure_ops,
	.set_buffers = pulp_udma_filter_set_buffers,
	.set_callback = pulp_udma_filter_irq_callback_set,
	.start = pulp_udma_filter_start,
	.abort = pulp_udma_filter_abort,
};

#define PULP_FILTER_INIT(idx)                                                                      \
	static int pulp_udma_filter_##idx##_init(const struct device *dev)                         \
	{                                                                                          \
		int ret = pulp_udma_common_filter_init(dev);                                       \
		if (ret != 0) {                                                                    \
			return ret;                                                                \
		}                                                                                  \
		IRQ_CONNECT(DT_INST_IRQN(idx), 0, pulp_udma_filter_irq, DEVICE_DT_INST_GET(idx),   \
			    0);                                                                    \
		irq_enable(DT_INST_IRQN(idx));                                                     \
		return 0;                                                                          \
	}                                                                                          \
	static struct pulp_udma_filter_data pulp_udma_filter_##idx##_data = {                      \
		.hw_data =                                                                         \
			{                                                                          \
				.current_config = 0,                                               \
			},                                                                         \
		.filt = PULP_FILTER(idx),                                                          \
	};                                                                                         \
	const static struct pulp_udma_filter_config pulp_udma_filter_##idx##_config = {            \
		.hw_config =                                                                       \
			{                                                                          \
				.caps =                                                            \
					{                                                          \
						.op_caps = BIT(HW_OP_SUM) | BIT(HW_OP_SUB) |       \
							   BIT(HW_OP_MULT) |                       \
							   BIT(HW_OP_SUM_SCALAR) |                 \
							   BIT(HW_OP_SUB_SCALAR) |                 \
							   BIT(HW_OP_MULT_SCALAR) |                \
							   BIT(HW_OP_ACCUMULATE),                  \
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
	DEVICE_DT_INST_DEFINE(idx, &pulp_udma_filter_##idx##_init, NULL,                           \
			      &pulp_udma_filter_##idx##_data, &pulp_udma_filter_##idx##_config,    \
			      POST_KERNEL, CONFIG_HWACCEL_INIT_PRIORITY,                           \
			      &pulp_udma_filter_accel_api);

DT_INST_FOREACH_STATUS_OKAY(PULP_FILTER_INIT)
