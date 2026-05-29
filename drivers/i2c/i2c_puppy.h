#ifndef __I2C_PUPPY_H__
#define __I2C_PUPPY_H__

#include <zephyr/drivers/i2c.h>

// RW bit in address/control byte
#define I2C_R_FLAG (1 << 0)
#define I2C_W_FLAG (0 << 0)

#define I2C_CMD_BUF_SIZE 16

// I2C uDMA commands
#define I2C_CMD_ID_OFFSET 28

#define I2C_CMD_START_ID   0x0
#define I2C_CMD_STOP_ID    0x2
#define I2C_CMD_RD_ACK_ID  0x4
#define I2C_CMD_RD_NACK_ID 0x6
#define I2C_CMD_WR_ID      0x8
#define I2C_CMD_WAIT_ID    0xA
#define I2C_CMD_RPT_ID     0xC
#define I2C_CMD_CFG_ID     0xE
#define I2C_CMD_WAIT_EV_ID 0x1
#define I2C_CMD_EOT_ID     0x9

#define I2C_CMD_START   (uint32_t)(I2C_CMD_START_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_STOP    (uint32_t)(I2C_CMD_STOP_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_RD_ACK  (uint32_t)(I2C_CMD_RD_ACK_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_RD_NACK (uint32_t)(I2C_CMD_RD_NACK_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_WR      (uint32_t)(I2C_CMD_WR_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_WAIT    (uint32_t)(I2C_CMD_WAIT_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_RPT     (uint32_t)(I2C_CMD_RPT_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_CFG     (uint32_t)(I2C_CMD_CFG_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_WAIT_EV (uint32_t)(I2C_CMD_WAIT_EV_ID << I2C_CMD_ID_OFFSET)
#define I2C_CMD_EOT     (uint32_t)(I2C_CMD_EOT_ID << I2C_CMD_ID_OFFSET)

#endif // __I2C_PUPPY_H__
