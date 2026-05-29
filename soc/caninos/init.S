#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/offsets.h>
#include "../../../arch/riscv/core/asm_macros.inc"

/* exports */
GTEXT(__start)

/* imports */
GTEXT(z_prep_c)
GTEXT(_isr_wrapper)

.option norvc;

SECTION_FUNC(vectors, __start)
	/* Set mtvec to point at ivt. */
	la t0, ivt
	csrw mtvec, t0
	/* Call into Zephyr initialization. */
#ifdef CONFIG_FPU
	/*
	 * Enable floating-point.
	 */
	li t0, MSTATUS_FS_INIT
	csrs mstatus, t0

	/*
	 * Floating-point rounding mode set to IEEE-754 default, and clear
	 * all exception flags.
	 */
	fscsr zero
#endif

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
#endif

	/*
	 * Jump into C domain. z_prep_c zeroes BSS, copies rw data into RAM,
	 * and then enters kernel z_cstart
	 */
	call z_prep_c
	j .

	.balign 256
ivt:
	.rept 32
	jal zero, _isr_wrapper
	.endr








