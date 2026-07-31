/*
 * Copyright (c) 2026 Edgar Bernardi Righi
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_PUPPY_INTC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_PUPPY_INTC_H_

/* Core Interrupt Controller (intc) */

#define PUPPY_INTC_UDMA_IRQ		 8 /* uDMA event */
#define PUPPY_INTC_TIMER_LO_IRQ		10 /* Timer LO event */
#define PUPPY_INTC_TIMER_HI_IRQ		11 /* Timer HI event */
#define PUPPY_INTC_GPIO_EVENT_IRQ	15 /* GPIO event */
#define PUPPY_INTC_EVT_FIFO_VALID_IRQ	26 /* SOC event generator */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_PUPPY_INTC_H_ */
