/*
 * Copyright (c) 2026 Caninos Loucos
 *
 * SPDX-License-Identifier:
 */
#define DT_DRV_COMPAT caninos_puppy_pwm

#include <soc_pad.h>
#include <zephyr/devicetree.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <soc.h>

#define LOG_LEVEL CONFIG_PWM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pwm_puppy);

#define TIMER_CHAN0 0
#define TIMER_CHAN1 1
#define TIMER_CHAN2 2
#define TIMER_CHAN3 3

#define CLKDIV_OFFSET    16
#define UPDOWNSEL_OFFSET 12
#define CLKSEL_OFFSET    11
#define MODE_OFFSET      8

#define COUNT_CONTINUOUS 0
#define COUNT_MATCH_RST  1
#define CLK_MAIN         0
#define CLK_32K          1
#define MODE_CLK_CYCLE   0
#define INSEL            0

#define TIMER_CFG(clkdiv, updownsel, clksel, mode, insel)                                          \
	(((clkdiv) << CLKDIV_OFFSET) | ((updownsel) << UPDOWNSEL_OFFSET) |                         \
	 ((clksel) << CLKSEL_OFFSET) | ((mode) << MODE_OFFSET) | (insel))

#define THRESH_HI_OFFSET           16
#define TIMER_THRESHOLD(high, low) (high << THRESH_HI_OFFSET) | low

#define THRESH_MODE_OFFSET         16
#define TIMER_CH_CFG(mode, thresh) (mode << THRESH_MODE_OFFSET) | thresh

#define ARM_TIMER    BIT(4)
#define RESET_TIMER  BIT(3)
#define UPDATE_TIMER BIT(2)
#define STOP_TIMER   BIT(1)
#define START_TIMER  BIT(0)

#define CMD_OFFSET    0x00
#define CFG_OFFSET    0x04
#define THRESH_OFFSET 0x8
#define CH_OFFSET(x)  (0xc + 0x4 * (x))

struct pwm_puppy_config {
	int id;
};

struct pwm_puppy_data {
	uint32_t base;
};

#define ADV_TIMER_OFFSET(t) (t * 0x40)
#define ADV_TIMER_CG        0x1A105104 // hardcoded for now

static inline void timer_cg(int group, int enable)
{
	int adv_tim_reg = sys_read32(ADV_TIMER_CG);
	if (enable) {
		sys_write32(adv_tim_reg | BIT(group), ADV_TIMER_CG);
	} else {
		sys_write32(adv_tim_reg & ~(BIT(group)), ADV_TIMER_CG);
	}
}

static inline void set_pinmux(int id, int channel)
{
	int pad = 4*id + channel + 5; //starts from pin 9, up to 19
	int func;

#ifdef CONFIG_SOC_PUPPY_V2
	if(channel == 0 || (id == 1 && channel == 1))
		func = 0x0;
	else
		func = 0x2;
#else
	func = 0x2;
#endif

	config_pad_func(pad, func);
	config_pad_cfg(pad, 0xfe);
}

static int pwm_puppy_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					uint64_t *cycles)
{
	struct pwm_puppy_data *data = dev->data;
	int clksel = sys_read32(data->base + CFG_OFFSET) >> CLKSEL_OFFSET & 0x1;

	if (clksel) {
		*cycles = 32768 / 2;
	} else {
		*cycles = sys_clock_hw_cycles_per_sec() / 2;
	}

	LOG_DBG("%lld cyc/s", *cycles);

	return 0;
}

static int pwm_puppy_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
				uint32_t pulse, pwm_flags_t flags)
{
	struct pwm_puppy_data *data = dev->data;
	const struct pwm_puppy_config *config = dev->config;
	uint8_t prescaler = 0;

	bool inverted = (flags & 0x1) != 0x0;
	if (inverted) {
		LOG_DBG("invert polarity");
		pulse = period - pulse;
	}

	if (channel > 3) {
		LOG_ERR("Channel %d does not exist", channel);
		return -EINVAL;
	}

	set_pinmux(config->id, channel);

	for (; period > UINT16_MAX; prescaler++) {
		period /= 2;
		pulse /= 2;
	}

	if (period == pulse) {
		period -= 1; // fix so 100% duty cycle does not fallback to 50% in hardware
	}

	timer_cg(config->id, 1);

	sys_write32(STOP_TIMER, data->base + CMD_OFFSET);

	sys_write32((sys_read32(data->base + CFG_OFFSET) & 0xff) | prescaler << 16,
		    data->base + CFG_OFFSET);

	sys_write32(TIMER_THRESHOLD(period, 0), data->base + THRESH_OFFSET);
	sys_write32(TIMER_CH_CFG(0x6, pulse), data->base + CH_OFFSET(channel));

	sys_write32(RESET_TIMER, data->base + CMD_OFFSET);
	sys_write32(UPDATE_TIMER, data->base + CMD_OFFSET);
	sys_write32(START_TIMER, data->base + CMD_OFFSET);

	LOG_DBG("Set pwm %d chan %d period %d pulse %d, prescaler %x", config->id, channel, period,
		pulse, prescaler);

	return 0;
}

static DEVICE_API(pwm, pwm_puppy_driver_api) = {
	.get_cycles_per_sec = pwm_puppy_get_cycles_per_sec,
	.set_cycles = pwm_puppy_set_cycles,
};

static int pwm_puppy_init(const struct device *dev)
{
	struct pwm_puppy_data *data = dev->data;

	sys_write32(RESET_TIMER, data->base + CMD_OFFSET);

	sys_write32(TIMER_CFG(1, COUNT_MATCH_RST, CLK_MAIN, MODE_CLK_CYCLE, INSEL),
		    data->base + CFG_OFFSET);

	return 0;
}

#define PWM_PUPPY_INIT(idx)                                                                        \
	static struct pwm_puppy_data pwm_puppy_data_##idx = {                                      \
		.base = DT_INST_REG_ADDR(idx),                                                     \
	};                                                                                         \
	const static struct pwm_puppy_config pwm_puppy_config_##idx = {                            \
		.id = DT_INST_PROP(idx, pwm_id),                                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, pwm_puppy_init, NULL, &pwm_puppy_data_##idx,                    \
			      &pwm_puppy_config_##idx, POST_KERNEL, CONFIG_PWM_INIT_PRIORITY,      \
			      &pwm_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_PUPPY_INIT);
