/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#define REG_TX_CH0_ADDR  0x00
#define REG_TX_CH0_CFG   0x04
#define REG_TX_CH0_LEN_0 0x08
#define REG_TX_CH0_LEN_1 0x0C
#define REG_TX_CH0_LEN_2 0x10

#define REG_TX_CH1_ADDR  0x14
#define REG_TX_CH1_CFG   0x18
#define REG_TX_CH1_LEN_0 0x1C
#define REG_TX_CH1_LEN_1 0x20
#define REG_TX_CH1_LEN_2 0x24

#define REG_RX_CH_ADDR  0x28
#define REG_RX_CH_CFG   0x2C
#define REG_RX_CH_LEN_0 0x30
#define REG_TX_CH_LEN_1 0x34
#define REG_TX_CH_LEN_2 0x38

#define REG_AU_CFG     0x3C
#define REG_AU_REGSUM  0x40
#define REG_AU_REGMULT 0x44

#define REG_BINCU_TH    0x48
#define REG_BINCU_CNT   0x4C
#define REG_BINCU_SETUP 0x50
#define REG_BINCU_VAL   0x54

#define REG_FILT_MODE  0x58
#define REG_FILT_START 0x5C

#define REG_STATUS 0x60

/* TX Channels Configuration */

#define TX_MODE_LINEAR   0x0
#define TX_MODE_SLIDING  0x1
#define TX_MODE_CIRCULAR 0x2
#define TX_MODE_2D       0x3

#define TX_DATA_SIZE_8  0x0
#define TX_DATA_SIZE_16 0x1
#define TX_DATA_SIZE_32 0x2

#define TX_MODE_OFFSET 8

#define TX_CFG(mode, size) (mode << TX_MODE_OFFSET | size)

/* RX Channel Configuration */

#define RX_MODE_LINEAR 0x0
#define RX_MODE_2D_ROW 0x1
#define RX_MODE_2D_COL 0x2

#define RX_DATA_SIZE_8  0x0
#define RX_DATA_SIZE_16 0x1
#define RX_DATA_SIZE_32 0x2

#define RX_MODE_OFFSET 8

#define RX_CFG(mode, size) (mode << RX_MODE_OFFSET | size)

/* Arithmetic Unit Config Register (REG_AU_CFG) */

#define AU_MODE_AxB                   0x0
#define AU_MODE_AxB_PLUS_REGSUM       0x1
#define AU_MODE_AxB_ACC               0x2
#define AU_MODE_AxA                   0x3
#define AU_MODE_AxA_PLUS_B            0x4
#define AU_MODE_AxA_MINUS_B           0x5
#define AU_MODE_AxA_ACC               0x6
#define AU_MODE_AxA_PLUS_REGSUM       0x7
#define AU_MODE_AxREGMULT             0x8
#define AU_MODE_AxREGMULT_PLUS_B      0x9
#define AU_MODE_AxREGMULT_MINUS_B     0xA
#define AU_MODE_AxREGMULT_PLUS_REGSUM 0xB
#define AU_MODE_AxREGMULT_ACC         0xC
#define AU_MODE_A_PLUS_B              0xD
#define AU_MODE_A_MINUS_B             0xE
#define AU_MODE_A_PLUS_REGSUM         0xF

#define AU_SIGNED_OFFSET 0
#define AU_BYPASS_OFFSET 1
#define AU_MODE_OFFSET 8
#define AU_SHIFT_OFFSET 16