/**
 * @file SoC configuration macros for puppy core
 */

#ifndef __PUPPY_SOC_H_
#define __PUPPY_SOC_H_

/* IRQ numbers */
//#define uDMA_IRQ             8  /* uDMA event */
//#define PUPPY_TIMER_LO_IRQ  10 /* Timer LO event */
//#define PUPPY_TIMER_HI_IRQ  11 /* Timer HI event */
//#define PUPPY_GPIO_IRQ      15 /* GPIO event */
//#define PUPPY_SOC_EVENT_IRQ 26 /* SOC event generator */

#include <zephyr/devicetree.h>

#define PUPPY_UDMA_REG_CG 0x1A102000

#define PUPPY_IRQ_BASE_ADDR DT_REG_ADDR(DT_NODELABEL(intc))

/* PAD configuration */
#include "soc_pad.h"

/* uDMA configuration */
#include "soc_udma.h"

#endif /* __PUPPY_SOC_H_ */
