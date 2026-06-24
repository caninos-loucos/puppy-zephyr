/* Copyright (c) 2026 Caninos Loucos
 *SPDX-License-Identifier: Apache-2.0
 */


#ifndef __SDHC_PUPPY_H__
#define __SDHC_PUPPY_H__

#include <soc.h>
#include <soc_pad.h>
#include <soc_event.h>

//  SDIO REGISTERS
#define SDIO_RX_SADDR     0x0
#define SDIO_RX_SIZE      0x4
#define SDIO_RX_CFG       0x8
#define SDIO_RX_INITCFG   0xC
#define SDIO_TX_SADDR     0x10
#define SDIO_TX_SIZE      0x14
#define SDIO_TX_CFG       0x18
#define SDIO_TX_INITCFG   0x1C
#define SDIO_CMD_OP       0x20
#define SDIO_CMD_ARG      0x24
#define SDIO_DATA_SETUP   0x28
#define SDIO_START        0x2C
#define SDIO_RSP0         0x30
#define SDIO_RSP1         0x34
#define SDIO_RSP2         0x38
#define SDIO_RSP3         0x3C
#define SDIO_CLK_DIV      0x40
#define SDIO_STATUS       0x44
#define SDIO_STOPCMD_OP   0x48
#define SDIO_STOPCMD_ARG  0x4C
#define SDIO_DATA_TIMEOUT 0x50
#define SDIO_BLOCKS_LEFT  0x54

//  CMD_OP bit position in SDIO_CMD_OP
#define CMD_OP_OFST 8

//  RESPONSE TYPE and bit position
#define RSP_TYPE_OFST   0
#define NO_RSP          0x0
#define RSP_48_CRC      0x1
#define RSP_48_NO_CRC   0x2
#define RSP_136         0x3
#define RSP_48_BUSY_CHK 0x4

//  SDIO_DATA_SETUP bit fields
#define DATA_EN(x)    (x & 0x1)           // 1 bit
#define DATA_RWN(x)   ((x & 0x1) << 1)    // 1 bit  : 1 - host to card; 0 - card to host
#define DATA_QUAD(x)  ((x & 0x1) << 2)    // 1 bit
#define BLOCK_NUM(x)  ((x & 0xff) << 8)   // 8 bit
#define BLOCK_SIZE(x) ((x & 0x3ff) << 16) // 10 bit

// SDIO_STATUS bit masks
#define EOT_MASK             BIT(0)
#define ERROR_MASK           BIT(1)
#define ERR_STATUS_MASK  (0x3f << 16)

// SDIO ERROR STATUS
#define CMD_ERR_TIMEOUT      BIT(16)
#define CMD_ERR_WRONG_DIR    BIT(17)
#define CMD_ERR_BUSY_TIMEOUT BIT(18)
#define CMD_ERR_WRONG_CRC    BIT(19)
#define DATA_ERR_TIMEOUT BIT(24)

// SDIO EVENT ID
#define SDIO_EVT_RX  20
#define SDIO_EVT_TX  21
#define SDIO_EVT_EOT 22
#define SDIO_EVT_ERR 23

#endif // __SDIO_H__
