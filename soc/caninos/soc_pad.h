/*
 * Copyright (c) 2026 Edgar Bernardi Righi - LSITEC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PUPPY_SOC_PAD_H__
#define __PUPPY_SOC_PAD_H__

#ifndef _ASMLANGUAGE
#include <stdint.h>

static inline void config_pad_func(uint32_t pad, uint32_t func)
{
    volatile uint32_t *padfun = (volatile uint32_t *)(PUPPY_PADFUN0_REG);
    uint32_t off = pad * 2UL;  // 2 bits per function
    uint32_t idx = off / 32UL; // pad register id
    uint32_t bit = off % 32UL; // pad bit offset
    padfun[idx] = ((func & 0x3) << bit) | (padfun[idx] & ~(0x3 << bit));
}

static inline void config_pad_cfg(uint32_t pad, uint32_t cfg)
{
    volatile uint32_t *padcfg = (volatile uint32_t *)(PUPPY_PADCFG0_REG);
    uint32_t off = pad * 8UL;  // 8 bits per config
    uint32_t idx = off / 32UL; // pad register id
    uint32_t bit = off % 32UL; // pad bit offset
    padcfg[idx] = ((cfg & 0xff) << bit) | (padcfg[idx] & ~(0xff << bit));
}

#endif // _ASMLANGUAGE
#endif // __PUPPY_SOC_PAD_H__
