#ifndef SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_
#define SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_

#ifdef CONFIG_RISCV_SOC_OFFSETS

#define GEN_SOC_OFFSET_SYMS()                                                                      \
	GEN_OFFSET_SYM(soc_esf_t, lpstart0);                                                       \
	GEN_OFFSET_SYM(soc_esf_t, lpend0);                                                         \
	GEN_OFFSET_SYM(soc_esf_t, lpcount0);                                                       \
	GEN_OFFSET_SYM(soc_esf_t, lpstart1);                                                       \
	GEN_OFFSET_SYM(soc_esf_t, lpend1);                                                         \
	GEN_OFFSET_SYM(soc_esf_t, lpcount1)

#endif /* CONFIG_RISCV_SOC_OFFSETS */

#endif /* SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_ */
