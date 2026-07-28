/*
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/offsets.h>
#include <soc_offsets.h>
#include <soc.h>

/* irq register offset from PUPPY_IRQ_BASE_ADDR */
#define IRQ_MSK_CLR_OFF 0x08
#define IRQ_ACK_CLR_OFF 0x20

/* exports */
GTEXT(__start)
GTEXT(__soc_save_context)
GTEXT(__soc_restore_context)
GTEXT(__soc_handle_irq)

/* imports */
GTEXT(z_prep_c)
GTEXT(_isr_wrapper)

SECTION_FUNC(vectors, __start)
.option push /* store current assembler settings */
.option norelax /* do not relax auipc+addi */
	/* Clear mstatus.MIE (bit 3) to disable global interrupts */
	csrrc t0, mstatus, MSTATUS_IEN
	li t0, 0xFFFFFFFF
	li t1, PUPPY_IRQ_BASE_ADDR
	sw t0, IRQ_MSK_CLR_OFF(t1) /* disable all irq lines */
	sw t0, IRQ_ACK_CLR_OFF(t1) /* clear pending irqs */
	/* Setup mtvec table addresses */
	la t0, .Lmtvec_table
	ori t0, t0, 0x1 /* set mtvec.MODE (bit 0) to enable vectored irqs */
	csrw mtvec, t0
.option pop /* restore original assembler settings */
	/* Call into Zephyr initialization. */
#ifdef CONFIG_FPU
	/* Enable floating-point. */
	li t0, MSTATUS_FS_INIT
	csrs mstatus, t0
	/*
	 * Floating-point rounding mode set to IEEE-754 default, and clear
	 * all exception flags.
	 */
	fscsr zero
#endif /* CONFIG_FPU */
#ifdef CONFIG_INIT_STACKS
	/* Pre-populate all bytes in z_interrupt_stacks with 0xAA */
	la t0, z_interrupt_stacks
	/* Total size of all cores' IRQ stack */
	li t1, __z_interrupt_all_stacks_SIZEOF
	add t1, t1, t0
	/* Populate z_interrupt_stacks with 0xaaaaaaaa */
	li t2, 0xaaaaaaaa
1:	sw t2, 0x00(t0)
	addi t0, t0, 4
	blt t0, t1, 1b
#endif /* CONFIG_INIT_STACKS */
	/*
	 * Initially, setup stack pointer to
	 * z_interrupt_stacks + __z_interrupt_stack_SIZEOF
	 */
	la sp, z_interrupt_stacks
	li t0, __z_interrupt_stack_SIZEOF
	add sp, sp, t0
#ifdef CONFIG_WDOG_INIT
	call _WdogInit
#endif /* CONFIG_WDOG_INIT */
	/*
	 * Jump into C domain. z_prep_c zeroes BSS, copies rw data into RAM,
	 * and then enters kernel z_cstart
	 */
	call z_prep_c
	j .
.balign 256
.Lmtvec_table:
.option push /* store current assembler settings */
.option norvc /* disable compressed instructions */
	.rept 32
	jal zero, _isr_wrapper
	.endr
.option pop /* restore original assembler settings */

/* Use ABI name of registers for the sake of simplicity */

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE
/*
 * The Puppy core has ISA extensions for faster loop performance
 * that use extra registers.
 *
 * If the toolchain generates instructions that use them, they must be saved
 * prior to handling an interrupt/exception. This case is handled using
 * Zephyr's generic RISC-V mechanism for soc-specific context.
 *
 * For details, see the Kconfig help for CONFIG_RISCV_SOC_CONTEXT_SAVE.
 */
SECTION_FUNC(exception.other, __soc_save_context)
	/* Save hardware loop registers to the soc_esf_t passed in a0. */
	csrr t0, PULP_LPSTART0
	csrr t1, PULP_LPEND0
	csrr t2, PULP_LPCOUNT0
	sw t0, __soc_esf_t_lpstart0_OFFSET(a0)
	sw t1, __soc_esf_t_lpend0_OFFSET(a0)
	sw t2, __soc_esf_t_lpcount0_OFFSET(a0)
	csrr t0, PULP_LPSTART1
	csrr t1, PULP_LPEND1
	csrr t2, PULP_LPCOUNT1
	sw t0, __soc_esf_t_lpstart1_OFFSET(a0)
	sw t1, __soc_esf_t_lpend1_OFFSET(a0)
	sw t2, __soc_esf_t_lpcount1_OFFSET(a0)
	/* Return */
	jalr x0, ra

SECTION_FUNC(exception.other, __soc_restore_context)
	/* Restore hardware loop registers from soc_esf_t in a0. */
	lw t0, __soc_esf_t_lpstart0_OFFSET(a0)
	lw t1, __soc_esf_t_lpend0_OFFSET(a0)
	lw t2, __soc_esf_t_lpcount0_OFFSET(a0)
	csrw PULP_LPSTART0, t0
	csrw PULP_LPEND0, t1
	csrw PULP_LPCOUNT0, t2
	lw t0, __soc_esf_t_lpstart1_OFFSET(a0)
	lw t1, __soc_esf_t_lpend1_OFFSET(a0)
	lw t2, __soc_esf_t_lpcount1_OFFSET(a0)
	csrw PULP_LPSTART1, t0
	csrw PULP_LPEND1, t1
	csrw PULP_LPCOUNT1, t2
	/* Return */
	jalr x0, ra
#endif /* CONFIG_RISCV_SOC_CONTEXT_SAVE */

/*
 * SOC-specific function to handle pending IRQ number generating the interrupt.
 *
 * The Puppy core has:
 * 1) an CLEAR register for the pending IRQ number upon an interrupt at address
 *    PUPPY_IRQ_ACK_CLR_REG
 *
 * Exception number is given as parameter via register a0.
 */
SECTION_FUNC(exception.other, __soc_handle_irq)
	/* Clear exception number from the Interrupt pending register */
	li t1, PUPPY_IRQ_BASE_ADDR
	li t2, 0x1
	sll t3, t2, a0
	sw t3, IRQ_ACK_CLR_OFF(t1)
	ret

