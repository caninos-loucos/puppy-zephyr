#include <zephyr/kernel_structs.h>
#include <offsets.h>
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <soc.h>

/* exports */
GTEXT(__soc_save_context)
GTEXT(__soc_restore_context)
GTEXT(__soc_handle_irq)

/* Use ABI name of registers for the sake of simplicity */

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE
/*
 * The Pulpino core has ISA extensions for faster loop performance
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
 *    PULP_IRQ_ACK_CLR_REG
 *
 * Exception number is given as parameter via register a0.
 */
SECTION_FUNC(exception.other, __soc_handle_irq)
	/* Clear exception number from the Interrupt pending register */
	li t1, PULP_IRQ_ACK_CLR_REG
	li t2, 1
	sll t3, t2, a0
	sw t3, 0x00(t1)
	ret

