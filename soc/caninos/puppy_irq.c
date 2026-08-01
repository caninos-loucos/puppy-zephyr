/*
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>

#if defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
#include <zephyr/irq_nextlevel.h>
#endif

#include <soc.h>

#if defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
#define PUPPY_EVTC_DEV DEVICE_DT_GET(DT_INST(0, caninos_puppy_evtc))
#endif

/*
 * This interrupt controller exposes dedicated SET and CLR registers.
 * Therefore enabling or disabling an interrupt is a single atomic MMIO
 * write and requires no software synchronization.
 */

struct puppy_irq_t {
	/* This register contains the mask of enabled irqs (read only) */
	uint32_t INT_EN_GET;  // BASE_ADDR + 0x00
	/* This register enables irqs (write only) */
	uint32_t INT_EN_SET;  // BASE_ADDR + 0x04
	/* This register disables irqs (write only) */
	uint32_t INT_EN_CLR;  // BASE_ADDR + 0x08
	/* */
	uint32_t INT_PD_GET;  // BASE_ADDR + 0x0C
	uint32_t INT_PD_SET;  // BASE_ADDR + 0x10
	uint32_t INT_PD_CLR;  // BASE_ADDR + 0x14
	/* */
	uint32_t INT_ACK_GET; // BASE_ADDR + 0x18
	uint32_t INT_ACK_SET; // BASE_ADDR + 0x1C
	uint32_t INT_ACK_CLR; // BASE_ADDR + 0x20
};

static volatile struct puppy_irq_t * const PUPPY_IRQ = 
	(volatile struct puppy_irq_t *) PUPPY_IRQ_BASE_ADDR;

void arch_irq_enable(unsigned int irq)
{
#ifdef CONFIG_MULTI_LEVEL_INTERRUPTS
	if (irq_get_level(irq) == 2) {
		irq_enable_next_level(PUPPY_EVTC_DEV, irq);
		return;
	}
#endif
	PUPPY_IRQ->INT_EN_SET = BIT(irq);
}

void arch_irq_disable(unsigned int irq)
{
#ifdef CONFIG_MULTI_LEVEL_INTERRUPTS
	if (irq_get_level(irq) == 2) {
		irq_disable_next_level(PUPPY_EVTC_DEV, irq);
		return;
	}
#endif
	PUPPY_IRQ->INT_EN_CLR = BIT(irq);
}

int arch_irq_is_enabled(unsigned int irq)
{
#ifdef CONFIG_MULTI_LEVEL_INTERRUPTS
	if (irq_get_level(irq) == 2) {
		return irq_line_is_enabled_next_level(PUPPY_EVTC_DEV, irq);
	}
#endif
	return (PUPPY_IRQ->INT_EN_GET & BIT(irq)) != 0U;
}

