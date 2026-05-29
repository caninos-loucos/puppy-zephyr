/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __PUPPY_SOC_UDMA_H
#define __PUPPY_SOC_UDMA_H

#ifndef _ASMLANGUAGE

#include <zephyr/arch/riscv/sys_io.h>

/* UDMA Peripheral IDs */

#ifdef CONFIG_SOC_PUPPY_V2
typedef enum {
	UDMA_UART0_ID,
	UDMA_SPI0_ID,
	UDMA_SPI1_ID,
	UDMA_I2C0_ID,
	UDMA_I2C1_ID,
	UDMA_SDIO_ID,
	UDMA_I2S_ID,
	UDMA_CAM_ID,
	UDMA_UART1_ID,
	UDMA_FILTER_ID,
} pulp_udma_periph_t;
#else
typedef enum {
	UDMA_UART0_ID,
	UDMA_SPI0_ID,
	UDMA_I2C0_ID,
	UDMA_I2C1_ID,
	UDMA_SDIO_ID,
	UDMA_I2S_ID,
	UDMA_CAM_ID,
	UDMA_FILTER_ID,
} pulp_udma_periph_t;
#endif /* CONFIG_PUPPY_V2 */

/*
 * Global register map
 */

#define PULP_UDMA_BASE 0x1A102000

// The UDMA register map is made of several channels, each channel area size is defined just below

// Periph area size in log2
#define UDMA_PERIPH_AREA_SIZE_LOG2 7
// Periph area size
#define UDMA_PERIPH_AREA_SIZE      (1 << UDMA_PERIPH_AREA_SIZE_LOG2)
// Channel area size in log2
#define UDMA_CHANNEL_SIZE_LOG2     4
// Channel area size
#define UDMA_CHANNEL_SIZE          (1 << UDMA_CHANNEL_SIZE_LOG2)
#define UDMA_FIRST_CHANNEL_OFFSET  0x80

// Each channel area is itself made of 3 areas
// The offsets are given relative to the offset of the channel

// Offset for RX part
#define UDMA_CHANNEL_RX_OFFSET  0x00
// Offset for TX part
#define UDMA_CHANNEL_TX_OFFSET  0x10
// Offset for peripheral specific part
#define UDMA_CHANNEL_CMD_OFFSET 0x20

// For each channel, the RX and TX part have the following registers
// The offsets are given relative to the offset of the RX or TX part

// Start address register
#define UDMA_CHANNEL_SADDR_OFFSET  0x0
// Size register
#define UDMA_CHANNEL_SIZE_OFFSET   0x4
// Configuration register
#define UDMA_CHANNEL_CFG_OFFSET    0x8
// Int configuration register
#define UDMA_CHANNEL_INTCFG_OFFSET 0xC

// The UDMA also has a global configuration are defined here

// Configuration area offset
#define UDMA_CONF_OFFSET 0x0
// Configuration area size
#define UDMA_CONF_SIZE   0x040

// This area contains the following registers

// Clock-gating control register
#define UDMA_CONF_CG_OFFSET    0x00
// Input event control register
#define UDMA_CONF_EVTIN_OFFSET 0x04

/*
 * Register bitfields
 */

// The configuration register of the RX and TX parts for each channel can be accessed using the
// following bits
#define UDMA_CHANNEL_CFG_SHADOW_BIT (5)
#define UDMA_CHANNEL_CFG_CLEAR_BIT  (6)
#define UDMA_CHANNEL_CFG_EN_BIT     (4)
#define UDMA_CHANNEL_CFG_SIZE_BIT   (1)
#define UDMA_CHANNEL_CFG_CONT_BIT   (0)
// Indicates if a shadow transfer is there
#define UDMA_CHANNEL_CFG_SHADOW     (1 << UDMA_CHANNEL_CFG_SHADOW_BIT)
// Stop and clear all pending transfers
#define UDMA_CHANNEL_CFG_CLEAR      (1 << UDMA_CHANNEL_CFG_CLEAR_BIT)
#define UDMA_CHANNEL_CFG_EN         (1 << UDMA_CHANNEL_CFG_EN_BIT)   // Start a transfer
#define UDMA_CHANNEL_CFG_SIZE_8     (0 << UDMA_CHANNEL_CFG_SIZE_BIT) // Configure for 8-bits transfer
#define UDMA_CHANNEL_CFG_SIZE_16    (1 << UDMA_CHANNEL_CFG_SIZE_BIT) // Configure for 16-bits transfer
#define UDMA_CHANNEL_CFG_SIZE_32    (2 << UDMA_CHANNEL_CFG_SIZE_BIT) // Configure for 32-bits transfer
#define UDMA_CHANNEL_CFG_CONT       (1 << UDMA_CHANNEL_CFG_CONT_BIT) // Configure for continuous mode

/*
 * Macros
 */

// Returns the configuration of an input event. Several values can be ORed together to form the full
// configuration
#define UDMA_CONF_EVTIN_EVT(udmaId, globalId) ((globalId) << (udmaId * 8))

// Return the offset of a peripheral from its identifier
#define UDMA_PERIPH_OFFSET(id) (((id) << UDMA_PERIPH_AREA_SIZE_LOG2) + UDMA_FIRST_CHANNEL_OFFSET)

// Returns the identifier of a peripheral from its offset
#define UDMA_PERIPH_GET(offset) ((offset) >> UDMA_PERIPH_AREA_SIZE_LOG2)

// Return the offset of a channel from its identifier
#define UDMA_CHANNEL_OFFSET(id) ((id) << UDMA_CHANNEL_SIZE_LOG2)

// Returns the identifier of a channel from its offset
#define UDMA_CHANNEL_GET(offset) ((offset) >> UDMA_CHANNEL_SIZE_LOG2)

// Return the id of a channel from the peripheral id
#define UDMA_CHANNEL_ID(id) ((id) * 2)

// Return the number of events per peripheral
#define UDMA_NB_PERIPH_EVENTS_LOG2 2
#define UDMA_NB_PERIPH_EVENTS      (1 << UDMA_NB_PERIPH_EVENTS_LOG2)

// Return the periph id from the channel
#define UDMA_PERIPH_ID(id) ((id) / 2)

// Return the event id of a channel from the peripheral id
#define UDMA_EVENT_ID(id) ((id) * UDMA_NB_PERIPH_EVENTS)

#define ARCHI_SOC_EVENT_UDMA_RX(periph) ((periph) * 2)
#define ARCHI_SOC_EVENT_UDMA_TX(periph) ((periph) * 2 + 1)

/*** UDMA EVENT IDS ***/
// id : number relative to number of same peripheral type (except ) (NOT UDMA ID)
#define ARCHI_UDMA_UART0_RX_EVT(id)     ((UDMA_UART0_ID + id) * 4)
#define ARCHI_UDMA_UART0_TX_EVT(id)     (ARCHI_UDMA_UART0_RX_EVT(id) + 1)
#define ARCHI_UDMA_UART0_EOT_EVT(id)    (ARCHI_UDMA_UART0_RX_EVT(id) + 2)
#define ARCHI_UDMA_UART0_RX_DAT_EVT(id) (ARCHI_UDMA_UART0_RX_EVT(id) + 3)

#define ARCHI_UDMA_SPIM_RX_EVT(id)  ((UDMA_SPI0_ID + id) * 4)
#define ARCHI_UDMA_SPIM_TX_EVT(id)  (ARCHI_UDMA_SPIM_RX_EVT(id) + 1)
#define ARCHI_UDMA_SPIM_CMD_EVT(id) (ARCHI_UDMA_SPIM_RX_EVT(id) + 2)
#define ARCHI_UDMA_SPIM_EOT_EVT(id) (ARCHI_UDMA_SPIM_RX_EVT(id) + 3)

#define ARCHI_UDMA_I2C_RX_EVT(id)  ((UDMA_I2C0_ID + id) * 4)
#define ARCHI_UDMA_I2C_TX_EVT(id)  (ARCHI_UDMA_I2C_RX_EVT(id) + 1)
#define ARCHI_UDMA_I2C_CMD_EVT(id) (ARCHI_UDMA_I2C_RX_EVT(id) + 2)
#define ARCHI_UDMA_I2C_EOT_EVT(id) (ARCHI_UDMA_I2C_RX_EVT(id) + 3)

#define ARCHI_UDMA_SDIO_RX_EVT(id)  ((UDMA_SDIO_ID + id) * 4)
#define ARCHI_UDMA_SDIO_TX_EVT(id)  (ARCHI_UDMA_SDIO_RX_EVT(id) + 1)
#define ARCHI_UDMA_SDIO_EOT_EVT(id) (ARCHI_UDMA_SDIO_RX_EVT(id) + 2)
#define ARCHI_UDMA_SDIO_ERR_EVT(id) (ARCHI_UDMA_SDIO_RX_EVT(id) + 3)

#define ARCHI_UDMA_I2S_RX_EVT(id) ((UDMA_I2S_ID + id) * 4)
#define ARCHI_UDMA_I2S_TX_EVT(id) (ARCHI_UDMA_I2S_RX_EVT(id) + 1)

#define ARCHI_UDMA_CAM_RX_EVT(id) ((UDMA_CAM_ID + id) * 4)

#ifdef CONFIG_SOC_PUPPY_V2
#define ARCHI_UDMA_UART1_RX_EVT(id)     ((UDMA_UART1_ID + id) * 4)
#define ARCHI_UDMA_UART1_TX_EVT(id)     (ARCHI_UDMA_UART1_RX_EVT(id) + 1)
#define ARCHI_UDMA_UART1_EOT_EVT(id)    (ARCHI_UDMA_UART0_RX_EVT(id) + 2)
#define ARCHI_UDMA_UART1_RX_DAT_EVT(id) (ARCHI_UDMA_UART0_RX_EVT(id) + 3)
#endif

#define ARCHI_UDMA_FILTER_EOT_EVT(id) ((UDMA_FILTER_ID + id) * 4)
#define ARCHI_UDMA_FILTER_ACT_EVT(id) ((UDMA_FILTER_ID + id) * 4 + 1)

/** Tell if a new transfer can be enqueued to a UDMA channel.
 *
	\param   channelOffset   Offset of the channel
	\return         1 if another transfer can be enqueued, 0 otherwise
	*/
static inline uint32_t plp_udma_canEnqueue(uint32_t channelBase)
{
	return !(sys_read32(channelBase + UDMA_CHANNEL_CFG_OFFSET) & UDMA_CHANNEL_CFG_SHADOW);
}

/** Enqueue a new transfer to a UDMA channel.
 *
	\param   channelOffset Offset of the channel
	\param   l2Addr        Address in L2 memory where the data must be accessed (either stored
 or loaded)
	\param   size          Size in bytes of the transfer
	\param   cfg           Configuration of the transfer. Can be UDMA_CHANNEL_CFG_SIZE_8,
 UDMA_CHANNEL_CFG_SIZE_16 or UDMA_CHANNEL_CFG_SIZE_32
	*/
static inline void plp_udma_enqueue(unsigned channelBase, uint32_t l2Addr, unsigned int size,
				    unsigned int cfg)
{
	sys_write32(l2Addr, channelBase + UDMA_CHANNEL_SADDR_OFFSET);
	sys_write32(size, channelBase + UDMA_CHANNEL_SIZE_OFFSET);
	sys_write32(cfg | UDMA_CHANNEL_CFG_EN, channelBase + UDMA_CHANNEL_CFG_OFFSET);
}

/** Tell if a channel is busy, i.e. if it already contains at least one on-going transfer
 *
	\param   channelOffset Offset of the channel
	\return  1 if the channel is busy, 0 otherwise
	*/
static inline uint32_t plp_udma_busy(unsigned channelOffset)
{
	return (sys_read32(channelOffset + UDMA_CHANNEL_CFG_OFFSET) & UDMA_CHANNEL_CFG_EN);
}

/** Configures peripheral clock-gating.
 *
	\param   value    New configuration. There is 1 bit per peripheral, 0 means the peripheral
 is clock-gated. The bit number corresponds to the channel ID of the peripheral.
	*/
static inline void plp_udma_cg_set(uint32_t value)
{
	sys_write32(value, PULP_UDMA_BASE + UDMA_CONF_OFFSET + UDMA_CONF_CG_OFFSET);
}

/** Returns peripheral clock-gating.
 *
	\return The current clock-gating configuration.
	*/
static inline uint32_t plp_udma_cg_get()
{
	return sys_read32(PULP_UDMA_BASE + UDMA_CONF_OFFSET + UDMA_CONF_CG_OFFSET);
}

/** Configures input events
 * 4 input events can be configured. Each input event is made of 8 bits specifying which global
 event this local event refers to. This can then be used in some UDMA commands to refer to the
 global event.
 *
	\param value  The new configuration. Each event is encoded on 8 bits so that it can contain
 a global event ID between 0 and 255.
	*/
static inline void plp_udma_evtin_set(uint32_t value)
{
	sys_write32(value, UDMA_CONF_OFFSET + UDMA_CONF_EVTIN_OFFSET);
}

/** Returns input events configuration.
 *
	\return The current input events configuration.
	*/
static inline uint32_t plp_udma_evtin_get()
{
	return sys_read32(UDMA_CONF_OFFSET + UDMA_CONF_EVTIN_OFFSET);
}

#endif /* _ASMLANGUAGE */
#endif /* __PUPPY_SOC_UDMA_H */
