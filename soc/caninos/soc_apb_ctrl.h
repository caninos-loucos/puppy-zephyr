/*
 * Copyright (c) 2026 Edgar Bernardi Righi - LSITEC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PUPPY_SOC_APB_CTRL_H__
#define __PUPPY_SOC_APB_CTRL_H__

/*
 * From /ips/pulp_soc/rtl/components/apb_soc_ctrl.sv
 */
#define PUPPY_APB_SOC_CTRL_BASE_ADDR (0x1A104000)
/* This register contains number of cores [31:16] and clusters [15:0] */
#define PUPPY_SOC_INFO_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x00)
/* This register is not used at the moment */
#define PUPPY_SOC_FCBOOT_REG  (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x04)
/* This register is not used at the moment */
#define PUPPY_SOC_FCFETCH_REG (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x08)
/* This register sets the mux for pins  0 (bits [1:0]) to 15 (bits [31:30]) */
#define PUPPY_PADFUN0_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x10)
/* This register sets the mux for pins 16 (bits [1:0]) to 31 (bits [31:30]) */
#define PUPPY_PADFUN1_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x14)
/* This register sets the mux for pins 32 (bits [1:0]) to 47 (bits [31:30]) */
#define PUPPY_PADFUN2_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x18)
/* This register sets the mux for pins 48 (bits [1:0]) to 63 (bits [31:30]) */
#define PUPPY_PADFUN3_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x1C)
/* 
 * PAD configuration is made of 8bits out of which only the first 6 are used
 * Valid only when the pad is not in gpio mode (function)
 * Not all configurations are available for all pads and/or functions
 * bit0 enable pull UP
 * bit1 enable pull DOWN
 * bit2 enable ST
 * bit3 enable SlewRate Limit
 * bit4..5 Driving Strength
 * bit6..7 not used
 */
/* This register sets config for pin  0(bits [7:0]) to pin  3(bits [31:24]) */
#define PUPPY_PADCFG0_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x20)
/* This register sets config for pin  4(bits [7:0]) to pin  7(bits [31:24]) */
#define PUPPY_PADCFG1_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x24)
/* This register sets config for pin  8(bits [7:0]) to pin 11(bits [31:24]) */
#define PUPPY_PADCFG2_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x28)
/* This register sets config for pin 12(bits [7:0]) to pin 15(bits [31:24]) */
#define PUPPY_PADCFG3_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x2C)
/* This register sets config for pin 16(bits [7:0]) to pin 19(bits [31:24]) */
#define PUPPY_PADCFG4_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x30)
/* This register sets config for pin 20(bits [7:0]) to pin 23(bits [31:24]) */
#define PUPPY_PADCFG5_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x34)
/* This register sets config for pin 24(bits [7:0]) to pin 27(bits [31:24]) */
#define PUPPY_PADCFG6_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x38)
/* This register sets config for pin 28(bits [7:0]) to pin 31(bits [31:24]) */
#define PUPPY_PADCFG7_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x3C)
/* This register sets config for pin 32(bits [7:0]) to pin 35(bits [31:24]) */
#define PUPPY_PADCFG8_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x40)
/* This register sets config for pin 36(bits [7:0]) to pin 39(bits [31:24]) */
#define PUPPY_PADCFG9_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x44)
/* This register sets config for pin 40(bits [7:0]) to pin 43(bits [31:24]) */
#define PUPPY_PADCFG10_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x48)
/* This register sets config for pin 44(bits [7:0]) to pin 47(bits [31:24]) */
#define PUPPY_PADCFG11_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x4C)
/* This register sets config for pin 48(bits [7:0]) to pin 51(bits [31:24]) */
#define PUPPY_PADCFG12_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x50)
/* This register sets config for pin 52(bits [7:0]) to pin 55(bits [31:24]) */
#define PUPPY_PADCFG13_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x54)
/* This register sets config for pin 56(bits [7:0]) to pin 59(bits [31:24]) */
#define PUPPY_PADCFG14_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x58)
/* This register sets config for pin 60(bits [7:0]) to pin 63(bits [31:24]) */
#define PUPPY_PADCFG15_REG    (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x5C)
/* This registe is used for JTAG */
#define PUPPY_JTAG_REG        (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0x74)
/* This 32bit GP register is used during testing to return EOC(bit[31])
 * and status(bit[30:0])
 */
#define PUPPY_CORESTATUS_REG  (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0xA0)
/* This 32bit GP register is used during testing to return EOC(bit[31])
 * and status(bit[30:0]). Read Only mirror
 */
#define PUPPY_CS_RO_REG       (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0xC0)
/* This register contains the value of bootsel */
#define PUPPY_BOOTSEL_REG     (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0xC4)
/* This register contains the value of clocksel */
#define PUPPY_CLKSEL_REG      (PUPPY_APB_SOC_CTRL_BASE_ADDR + 0xC8)

#endif // __PUPPY_APB_CTRL_H__
