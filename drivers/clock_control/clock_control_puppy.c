/*
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_udma_clock

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control);

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#define CLK_GATE_REG *((volatile uint32_t*)DT_INST_REG_ADDR(0))

static int puppy_clk_on(const struct device *dev,
			clock_control_subsys_t subsys)
{
	uintptr_t id = POINTER_TO_UINT(subsys);
	uint32_t key = irq_lock();
	int ret = -EINVAL;
	ARG_UNUSED(dev);

	if (id < 32) {
		CLK_GATE_REG |= BIT(id);
		ret = 0;
	}
	irq_unlock(key);
	return ret;
}

static int puppy_clk_off(const struct device *dev,
			 clock_control_subsys_t subsys)
{
	uintptr_t id = POINTER_TO_UINT(subsys);
	uint32_t key = irq_lock();
	int ret = -EINVAL;
	ARG_UNUSED(dev);

	if (id < 32) {
		CLK_GATE_REG &= ~BIT(id);
		ret = 0;
	}
	irq_unlock(key);
	return ret;
}

static enum clock_control_status 
puppy_clk_get_status(const struct device *dev, clock_control_subsys_t subsys)
{
	enum clock_control_status ret = CLOCK_CONTROL_STATUS_OFF;
	uintptr_t id = POINTER_TO_UINT(subsys);
	uint32_t key = irq_lock();
	ARG_UNUSED(dev);

	if (id < 32 && (CLK_GATE_REG & BIT(id)) == BIT(id)) {
		ret = CLOCK_CONTROL_STATUS_ON;
	}
	irq_unlock(key);
	return ret;
}

static int puppy_clk_get_rate(const struct device *dev,
			      clock_control_subsys_t subsys, uint32_t *rate)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(subsys);

	if (rate != NULL) {
		*rate = sys_clock_hw_cycles_per_sec();
		return 0;
	}
	return -EINVAL;
}

static DEVICE_API(clock_control, clock_control_puppy_api) = {
	.on = puppy_clk_on,
	.off = puppy_clk_off,
	.get_status = puppy_clk_get_status,
	.get_rate = puppy_clk_get_rate,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY,
		      &clock_control_puppy_api);

