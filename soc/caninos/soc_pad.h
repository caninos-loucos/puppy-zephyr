#ifndef __PUPPY_SOC_PAD_H__
#define __PUPPY_SOC_PAD_H__

#define PULP_PAD_BASE    0x1A104010
#define PULP_PADCFG_BASE 0x1A104020

#ifndef _ASMLANGUAGE
#include <zephyr/arch/riscv/sys_io.h>
#include <stdint.h>

static inline void config_pad_func(int pad, int func)
{
	uint32_t padfunId = pad >> 4;          // pad register id
	uint32_t padfunBit = (pad & 0xf) << 1; // pad bit offset

	uint32_t val = sys_read32(PULP_PAD_BASE + padfunId * 0x4) & ~(0x3 << padfunBit);
	val |= (func & 0x3) << padfunBit;
	sys_write32(val, PULP_PAD_BASE + padfunId * 0x4);
}

static inline void config_pad_cfg(int pad, int cfg)
{
	uint32_t padfunId = pad >> 2;          // pad register id
	uint32_t padfunBit = (pad & 0x3) << 3; // pad bit offset

	uint32_t val = sys_read32(PULP_PADCFG_BASE + padfunId * 0x4) & ~(0xff << padfunBit);
	val |= (cfg & 0xff) << padfunBit;
	sys_write32(val, PULP_PADCFG_BASE + padfunId * 0x4);
}
#endif // _ASMLANGUAGE
#endif // __PUPPY_SOC_PAD_H__
