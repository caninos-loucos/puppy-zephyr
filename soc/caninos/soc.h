/**
 * @file SoC configuration macros for puppy core
 */

#ifndef __PUPPY_SOC_H_
#define __PUPPY_SOC_H_

#include <zephyr/devicetree.h>

#define PUPPY_IRQ_BASE_ADDR DT_REG_ADDR(DT_NODELABEL(intc))

/* PAD configuration */
#include "soc_pad.h"

/* uDMA configuration */
#include "soc_udma.h"

#ifndef DT_IRQN_BY_NAME
#define DT_IRQN_BY_NAME(node_id, name)                                \
	COND_CODE_1(IS_ENABLED(CONFIG_MULTI_LEVEL_INTERRUPTS),        \
		((IRQ_TO_L2(DT_IRQ_BY_NAME(node_id, name, irq)) |     \
		  DT_IRQ(DT_IRQ_INTC_BY_NAME(node_id, name), irq))),  \
		(DT_IRQ_BY_NAME(node_id, name, irq)))
#endif

#ifndef DT_INST_IRQN_BY_NAME
#define DT_INST_IRQN_BY_NAME(inst, name) \
	DT_IRQN_BY_NAME(DT_DRV_INST(inst), name)
#endif

#endif /* __PUPPY_SOC_H_ */
