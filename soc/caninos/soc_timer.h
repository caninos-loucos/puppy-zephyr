/*
 * Copyright (c) 2026 Edgar Bernardi Righi - LSITEC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PUPPY_SOC_TIMER_H__
#define __PUPPY_SOC_TIMER_H__

#ifndef _ASMLANGUAGE
#include <stdint.h>

struct puppy_timer_t {
	uint32_t CFG_LO;
	uint32_t CFG_HI;
	uint32_t CNT_LO;
	uint32_t CNT_HI;
	uint32_t CMP_LO;
	uint32_t CMP_HI;
	uint32_t START_LO;
	uint32_t START_HI;
	uint32_t RESET_LO;
	uint32_t RESET_HI;
};

typedef volatile struct puppy_timer_t PUPPY_TIMER_Type;

#endif // _ASMLANGUAGE
#endif // __PUPPY_SOC_TIMER_H__
