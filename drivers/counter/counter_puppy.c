/*
 * Copyright (c) 2026 Ana Clara Forcelli
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/device.h>

#define DT_DRV_COMPAT caninos_counter_puppy

#define CFG_PRESCALER_SHIFT 8U

#define CFG_SRC_32K BIT(7)
#define CFG_PRESCALER_EN BIT(6)
#define CFG_ONESHOT BIT(5)
#define CFG_RESTART_MATCH BIT(4)
#define CFG_IRQ_EN BIT(2)
#define CFG_RESET BIT(1)
#define CFG_START BIT(0)

/* Uses same timer as the sys timer */
struct puppy_timer_t {
	uint32_t CFG_LO;
	uint32_t CFG_HI;
	uint32_t CNT_LO;
	uint32_t CNT_HI;
	uint32_t CMP_LO;
	uint32_t CMP_HI;
	uint32_t START_LO;
	uint32_t START_HI;
	uint32_t RESET_LO;
	uint32_t RESET_HI;
};

struct counter_puppy_top_data {
	counter_top_callback_t callback;
	uint32_t ticks;
	void *user_data;
	bool auto_reload;
	uint32_t guard_period;
};

struct counter_puppy_data {        
	struct counter_alarm_cfg alarm_cfg;
	struct counter_puppy_top_data top_data;
};

struct counter_puppy_config {
        struct counter_config_info info;
};


static volatile struct puppy_timer_t * const PUPPY_TIMER =
	(volatile struct puppy_timer_t *)DT_INST_REG_ADDR(0);


static int counter_puppy_start(const struct device* dev)
{
        PUPPY_TIMER->START_HI = 0x1;
        return 0;
}

/* it actually doesnt stop */
static int counter_puppy_stop(const struct device* dev)
{
        return 0;
}

static int counter_puppy_get_value(const struct device *dev, uint32_t *ticks)
{
        *ticks = (uint32_t)PUPPY_TIMER->CNT_HI;
        return 0;
}

static int counter_puppy_reset(const struct device* dev)
{
        PUPPY_TIMER->RESET_HI = 0x1;
	while (PUPPY_TIMER->CFG_HI & BIT(1));

        return 0;
}

static int counter_puppy_get_value_64(const struct device *dev, uint64_t *ticks)
{
        *ticks = PUPPY_TIMER->CNT_HI & 0xFFFFUL;
        return 0;
}

static int counter_puppy_set_alarm(const struct device *dev, uint8_t chan_id, const struct counter_alarm_cfg *alarm_cfg)
{	
        ARG_UNUSED(chan_id);
	struct counter_puppy_data *data = dev->data;
	bool absolute = alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE;
	uint32_t ticks = alarm_cfg->ticks;
	uint32_t now;
	uint64_t target;
	int err = 0;

	if (ticks > data->top_data.ticks) {
		return -EINVAL;
	}

	if (data->alarm_cfg.callback) {
		return -EBUSY;
	}

	data->alarm_cfg.callback = alarm_cfg->callback;
	data->alarm_cfg.user_data = alarm_cfg->user_data;

	counter_puppy_get_value(dev, &now);

	if (absolute == 0) 
        {
		target = now + ticks;
                if(target > data->top_data.ticks)
                target = (now & ~(0xFFFFUL)) | ticks;
        }
        else
                target = ticks;

        PUPPY_TIMER->CMP_HI = target;

	return err;
}

static int counter_puppy_cancel_alarm(const struct device *dev, uint8_t id)
{
	struct counter_puppy_data *data = dev->data;

	data->alarm_cfg.callback  = NULL;
	data->alarm_cfg.user_data = NULL;

	return 0;
}

static int counter_puppy_set_top_value(const struct device *dev, const struct counter_top_cfg *cfg)
{
	const struct counter_puppy_config *config = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t now;

	if (data->alarm_cfg.callback) {
		return -EBUSY;
	}

	if (cfg->ticks > config->info.max_top_value) {
		return -ENOTSUP;
	}

	counter_puppy_get_value(dev, &now);

	if (!(cfg->flags & COUNTER_TOP_CFG_DONT_RESET)) {
		PUPPY_TIMER->RESET_HI = 0x1;
	} else {
		if (now > cfg->ticks) {
			if (cfg->flags & COUNTER_TOP_CFG_RESET_WHEN_LATE) {
		                PUPPY_TIMER->RESET_HI = 0x1;
			} else {
				return -ETIME;
			}
		}
	}

	data->top_data.ticks = cfg->ticks;
	data->top_data.callback = cfg->callback;
	data->top_data.user_data = cfg->user_data;
	data->top_data.auto_reload = (cfg->callback != NULL);

        PUPPY_TIMER->CMP_HI = cfg->ticks;
        PUPPY_TIMER->CFG_HI |= CFG_IRQ_EN;
	return 0;

}
static uint32_t counter_puppy_get_pending_int(const struct device *dev)
{
        ARG_UNUSED(dev);
        return -ENOTSUP;
}

static uint32_t counter_puppy_get_top_value(const struct device *dev)
{
	struct counter_puppy_data *data = dev->data;

	return data->top_data.ticks;
}

static uint32_t counter_puppy_get_guard_period(const struct device *dev, uint32_t flags)
{
	struct counter_puppy_data *data = dev->data;

	ARG_UNUSED(flags);

	return data->top_data.guard_period;
}

static int counter_puppy_set_guard_period(const struct device *dev, uint32_t ticks, uint32_t flags)
{
	struct counter_puppy_data *data = dev->data;

	ARG_UNUSED(flags);

	if (ticks > data->top_data.ticks) {
		return -EINVAL;
	}

	data->top_data.guard_period = ticks;
	return 0;
}

static uint32_t counter_puppy_get_freq(const struct device *dev)
{
        return sys_clock_hw_cycles_per_sec();
}

static DEVICE_API(counter, counter_api) = {
	.start = counter_puppy_start,
	.stop = counter_puppy_stop,
	.reset = counter_puppy_reset,
	.get_value = counter_puppy_get_value,
	.get_value_64 = counter_puppy_get_value_64,
	.set_alarm = counter_puppy_set_alarm,
	.cancel_alarm = counter_puppy_cancel_alarm,
	.set_top_value = counter_puppy_set_top_value,
	.get_top_value = counter_puppy_get_top_value,
	.get_pending_int = counter_puppy_get_pending_int,
	.get_freq = counter_puppy_get_freq,
	.get_guard_period = counter_puppy_get_guard_period,
	.set_guard_period = counter_puppy_set_guard_period,
};

static void counter_puppy_isr(void* data)
{
        const struct device* dev = (const struct device*)data;
	struct counter_puppy_data *dev_data = dev->data;

	counter_alarm_callback_t cb = dev_data->alarm_cfg.callback;
	void *user_data = dev_data->alarm_cfg.user_data;

	if (cb) {
		dev_data->alarm_cfg.callback = NULL;
		dev_data->alarm_cfg.user_data = NULL;
		cb(dev, 0, PUPPY_TIMER->CNT_HI, user_data);
	}
}

static int counter_puppy_init(const struct device* dev)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;

	IRQ_CONNECT(DT_INST_IRQN(0), 0, counter_puppy_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	PUPPY_TIMER->START_HI = 0x0;
	PUPPY_TIMER->RESET_HI = 0x1;
	while (PUPPY_TIMER->CFG_HI & BIT(1));

	PUPPY_TIMER->CFG_HI = CFG_RESTART_MATCH | CFG_IRQ_EN;

	data->alarm_cfg.callback = NULL;
	data->top_data.callback = NULL;
	data->top_data.user_data = NULL;
	data->top_data.auto_reload = false;
	data->top_data.ticks = cfg->info.max_top_value;

	return 0;
}

#define COUNTER_PUPPY_INIT(idx)                                                                    \
                                                                                                   \
	static struct counter_puppy_data counter_data_##idx;                                       \
                                                                                                   \
	static const struct counter_puppy_config counter_config_##idx = {                          \
		.info = {.max_top_value = UINT32_MAX,                                      \
				 .flags = COUNTER_CONFIG_INFO_COUNT_UP,                            \
				 .channels = 1}};                                                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, counter_puppy_init, NULL, &counter_data_##idx,                  \
			      &counter_config_##idx, PRE_KERNEL_1, CONFIG_COUNTER_INIT_PRIORITY,   \
			      &counter_api);

DT_INST_FOREACH_STATUS_OKAY(COUNTER_PUPPY_INIT);