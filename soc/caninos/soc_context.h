/*
 * Extra definitions required for CONFIG_RISCV_SOC_CONTEXT_SAVE.
 */

#ifndef PULPINO_SOC_CONTEXT_H_
#define PULPINO_SOC_CONTEXT_H_

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE

/* Extra state for pulpino hardware loop registers. */
#define SOC_ESF_MEMBERS                                                                            \
	uint32_t lpstart0;                                                                         \
	uint32_t lpend0;                                                                           \
	uint32_t lpcount0;                                                                         \
	uint32_t lpstart1;                                                                         \
	uint32_t lpend1;                                                                           \
	uint32_t lpcount1

/* Initial saved state. */
#define SOC_ESF_INIT 0x0, 0x0, 0x0, 0x0, 0x0, 0x0

/* Ensure offset macros are available in <offsets.h> for the above. */

#endif

#endif
