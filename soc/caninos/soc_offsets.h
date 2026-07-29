#ifndef SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_
#define SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE

/* CSR Registers */
#define PULP_LPSTART0 0x7B0 /* Hardware Loop 0 Start Register */
#define PULP_LPEND0   0x7B1 /* Hardware Loop 0 End Register */
#define PULP_LPCOUNT0 0x7B2 /* Hardware Loop 0 Count Register */
#define PULP_LPSTART1 0x7B4 /* Hardware Loop 1 Start Register */
#define PULP_LPEND1   0x7B5 /* Hardware Loop 1 End Register */
#define PULP_LPCOUNT1 0x7B6 /* Hardware Loop 1 Count Register */

#define GEN_SOC_OFFSET_SYMS()                 \
	GEN_OFFSET_SYM(soc_esf_t, lpstart0);  \
	GEN_OFFSET_SYM(soc_esf_t, lpend0);    \
	GEN_OFFSET_SYM(soc_esf_t, lpcount0);  \
	GEN_OFFSET_SYM(soc_esf_t, lpstart1);  \
	GEN_OFFSET_SYM(soc_esf_t, lpend1);    \
	GEN_OFFSET_SYM(soc_esf_t, lpcount1);

#endif /* CONFIG_RISCV_SOC_CONTEXT_SAVE */

#endif /* SOC_RISCV_CANINOS_PUPPY_SOC_OFFSETS_H_ */
