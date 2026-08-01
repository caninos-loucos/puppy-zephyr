/*
 * Copyright (c) 2026 Ana Clara Forcelli
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_timer

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include <soc.h>

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

#define CYC_PER_TICK \
	(sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)

static volatile uint64_t cycle_count = 0ULL;

static volatile struct puppy_timer_t * const PUPPY_TIMER =
	(volatile struct puppy_timer_t *)DT_INST_REG_ADDR(0);

uint64_t sys_clock_cycle_get_64(void)
{
	unsigned int key = irq_lock();
	uint64_t cycles = cycle_count + PUPPY_TIMER->CNT_LO;
	irq_unlock(key);
	return cycles;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return (uint32_t)sys_clock_cycle_get_64();
}

uint32_t sys_clock_elapsed(void)
{
	return 0;
}

static void puppy_timer_irq_handler(void *unused)
{
	ARG_UNUSED(unused);
	cycle_count += CYC_PER_TICK;
	sys_clock_announce(1);
}

static int puppy_timer_init(void)
{
	PUPPY_TIMER->START_LO = 0x0;
	PUPPY_TIMER->RESET_LO = 0x1;
	while (PUPPY_TIMER->CFG_LO & BIT(1));

	PUPPY_TIMER->CFG_LO = 0x14;
	PUPPY_TIMER->CMP_LO = CYC_PER_TICK - 1UL;
	PUPPY_TIMER->CNT_LO = 0x0;

	IRQ_CONNECT(DT_INST_IRQN(0), 0, puppy_timer_irq_handler, NULL, 0);
	irq_enable(DT_INST_IRQN(0));
	PUPPY_TIMER->START_LO = 0x1;
	return 0;
}

SYS_INIT(puppy_timer_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);

