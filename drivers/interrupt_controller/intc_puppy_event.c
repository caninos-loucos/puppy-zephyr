/*
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_evtc

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/irq_multilevel.h>
#include <zephyr/irq_nextlevel.h>
#include <zephyr/sw_isr_table.h>
#include <soc.h>

#define FC_MASK_COUNT 8

struct puppy_evtc_regs {
	uint32_t SW_EVENT;               // BASEADDR + 0x0
	uint32_t FC_MASK[FC_MASK_COUNT]; // BASEADDR + 0x4 + (n * 0x4)
};

struct puppy_evtc_config {
	volatile struct puppy_evtc_regs *regs;
	volatile uint32_t *fifo;
};

static void puppy_evtc_irq_enable(const struct device *dev, uint32_t irq)
{
	const struct puppy_evtc_config *config = dev->config;
	uint32_t l2 = irq_from_level_2(irq);
	uint32_t bit = l2 & 31U;
	uint32_t off = l2 >> 5;

	if (off < FC_MASK_COUNT) {
		unsigned int key = irq_lock();
		config->regs->FC_MASK[off] &= ~BIT(bit);
		irq_unlock(key);
	}
}

static void puppy_evtc_irq_disable(const struct device *dev, uint32_t irq)
{
	const struct puppy_evtc_config *config = dev->config;
	uint32_t l2 = irq_from_level_2(irq);
	uint32_t bit = l2 & 31U;
	uint32_t off = l2 >> 5;

	if (off < FC_MASK_COUNT) {
		unsigned int key = irq_lock();
		config->regs->FC_MASK[off] |= BIT(bit);
		irq_unlock(key);
	}
}

static uint32_t puppy_evtc_get_state(const struct device *dev)
{
	const struct puppy_evtc_config *config = dev->config;
	uint32_t off;

	for (off = 0; off < FC_MASK_COUNT; off++) {
		if (config->regs->FC_MASK[off] != UINT32_MAX) {
			return 1;
		}
	}
	return 0;
}

static int puppy_evtc_get_line_state(const struct device *dev,
				     unsigned int irq)
{
	const struct puppy_evtc_config *config = dev->config;
	uint32_t l2 = irq_from_level_2(irq);
	uint32_t bit = l2 & 31U;
	uint32_t off = l2 >> 5;

	return (off < FC_MASK_COUNT) && 
	       !(config->regs->FC_MASK[off] & BIT(bit));
}

static void puppy_evtc_isr(const void *arg)
{
	const struct device *dev = (const struct device *)arg;
	const struct puppy_evtc_config *config = dev->config;
	const struct _isr_table_entry *entry;
	uint32_t num = *(config->fifo);

	if (num < CONFIG_MAX_IRQ_PER_AGGREGATOR) {
		entry = &_sw_isr_table[num + CONFIG_2ND_LVL_ISR_TBL_OFFSET];
		entry->isr(entry->arg);
	}
}

static int puppy_evtc_init(const struct device *dev)
{
	const struct puppy_evtc_config *config = dev->config;
	uint32_t off;

	for (off = 0; off < FC_MASK_COUNT; off++) {
		config->regs->FC_MASK[off] = UINT32_MAX;
	}

	IRQ_CONNECT(DT_INST_IRQN(0), 0, puppy_evtc_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
	return 0;
}

static const struct irq_next_level_api puppy_evtc_api = {
	.intr_enable = puppy_evtc_irq_enable,
	.intr_disable = puppy_evtc_irq_disable,
	.intr_get_state = puppy_evtc_get_state,
	.intr_get_line_state = puppy_evtc_get_line_state,
};

static const struct puppy_evtc_config puppy_evtc_config = {
	.regs = (volatile void *) DT_INST_REG_ADDR_BY_NAME(0, base),
	.fifo = (volatile void *) DT_INST_REG_ADDR_BY_NAME(0, fifo),
};

DEVICE_DT_INST_DEFINE(0, puppy_evtc_init, NULL, NULL,
		      &puppy_evtc_config, PRE_KERNEL_1,
		      CONFIG_INTC_INIT_PRIORITY,
		      &puppy_evtc_api);

