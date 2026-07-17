/**
 * @file SoC configuration macros for puppy core
 */

#ifndef __PUPPY_SOC_H_
#define __PUPPY_SOC_H_

/* CSR Registers */
#define PULP_LPSTART0 0x7B0 /* Hardware Loop 0 Start Register */
#define PULP_LPEND0   0x7B1 /* Hardware Loop 0 End Register */
#define PULP_LPCOUNT0 0x7B2 /* Hardware Loop 0 Count Register */
#define PULP_LPSTART1 0x7B4 /* Hardware Loop 1 Start Register */
#define PULP_LPEND1   0x7B5 /* Hardware Loop 1 End Register */
#define PULP_LPCOUNT1 0x7B6 /* Hardware Loop 1 Count Register */

/* IRQ numbers */
#define uDMA_IRQ             8  /* uDMA event */
#define PUPPY_TIMER_LO_IRQ  10 /* Timer LO event */
#define PUPPY_TIMER_HI_IRQ  11 /* Timer HI event */
#define PUPPY_GPIO_IRQ      15 /* GPIO event */
#define PUPPY_SOC_EVENT_IRQ 26 /* SOC event generator */

#define PULP_APB_TIMER_BASE_ADDR (0x1A10B000)

#define PULP_REG(x) (*((volatile uint32_t *)(x)))

/* APB CTRL registers */
#include "soc_apb_ctrl.h"

/* PAD configuration */
#include "soc_pad.h"

/* uDMA configuration */
#include "soc_udma.h"

/* Fabric Controller Event Configuration */
#include "soc_event.h"

/* Interrupt Registers */
#define PULP_IRQ_BASE_ADDR         (0x1A109000)
/* This register contains the MASK (read only) */
#define PULP_IRQ_MSK_GET_REG       (PULP_IRQ_BASE_ADDR + 0x00)
/* This register sets bits on MASK register (write only) */
#define PULP_IRQ_MSK_SET_REG       (PULP_IRQ_BASE_ADDR + 0x04)
/* This register clears bits on MASK register (write only) */
#define PULP_IRQ_MSK_CLR_REG       (PULP_IRQ_BASE_ADDR + 0x08)
#define PULP_IRQ_STATUS_GET_REG    (PULP_IRQ_BASE_ADDR + 0x0C)
#define PULP_IRQ_STATUS_SET_REG    (PULP_IRQ_BASE_ADDR + 0x10)
#define PULP_IRQ_STATUS_CLR_REG    (PULP_IRQ_BASE_ADDR + 0x14)
#define PULP_IRQ_ACK_GET_REG       (PULP_IRQ_BASE_ADDR + 0x18)
#define PULP_IRQ_ACK_SET_REG       (PULP_IRQ_BASE_ADDR + 0x1C)
#define PULP_IRQ_ACK_CLR_REG       (PULP_IRQ_BASE_ADDR + 0x20)
#define PULP_IRQ_FIFO_DATA_GET_REG (PULP_IRQ_BASE_ADDR + 0x24)

#include <zephyr/irq.h>

#define PULP_IRQ_MASK     PULP_REG(PULP_IRQ_MSK_GET_REG)
/* This register sets bits on MASK register */
#define PULP_IRQ_MASK_SET PULP_REG(PULP_IRQ_MSK_SET_REG)
/* This register clears bits on MASK register */
#define PULP_IRQ_MASK_CLR PULP_REG(PULP_IRQ_MSK_CLR_REG)
#define PULP_IRQ_ACK      PULP_REG(PULP_IRQ_ACK_GET_REG)
#define PULP_IRQ_ACK_SET  PULP_REG(PULP_IRQ_ACK_SET_REG)
#define PULP_IRQ_ACK_CLR  PULP_REG(PULP_IRQ_ACK_CLR_REG)

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
void soc_interrupt_init(void);
#endif

#endif /* __PUPPY_SOC_H_ */
