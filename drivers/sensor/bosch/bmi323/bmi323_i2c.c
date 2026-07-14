/*
 * Copyright (c) 2026 Caninos Loucos
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#include "bmi323.h"
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <errno.h>

static uint8_t rx[2 * 2048];

static int bosch_bmi323_i2c_read_words(const void *context, uint8_t offset,
				       uint16_t *words, uint16_t words_count)
{
	const struct i2c_dt_spec *i2c = (const struct i2c_dt_spec *)context;
	size_t len = (size_t)words_count * 2U + 2U; // words -> bytes and space for first dummy word

	if (!device_is_ready(i2c->bus)) {
		return -ENODEV;
	}

	if (len > sizeof(rx)) {
		printf("abc %d \n\r",len);
		return -EINVAL;
	}
	
	int ret = i2c_burst_read_dt(i2c, offset, rx, len); 

	if (ret < 0) {
		return ret;
	}

	memcpy(words, rx+2, len - 2U); // ignore first dummy word

	k_usleep(2);

	return 0;
}

static int bosch_bmi323_i2c_write_words(const void *context, uint8_t offset,
					uint16_t *words, uint16_t words_count)
{
	const struct i2c_dt_spec *i2c = (const struct i2c_dt_spec *)context;
	uint8_t tx[1 + 2 * 64];
	size_t len = (size_t)words_count * 2U + 1U; // words -> bytes and space for reg address/offset (first word)

	if (!device_is_ready(i2c->bus)) {
		return -ENODEV;
	}

	if (len > sizeof(tx)) {
		return -EINVAL;
	}

	tx[0] = offset;

	memcpy(tx+1, words, len - 1U);

	int ret = i2c_write_dt(i2c, tx, len);

	k_usleep(2);

	return ret;
}

static int bosch_bmi323_i2c_init(const void *context)
{
	const struct i2c_dt_spec *i2c = (const struct i2c_dt_spec *)context;
	if (!device_is_ready(i2c->bus)) {
		return -ENODEV;
	}

	k_usleep(1500);
	return 0;
}

const struct bosch_bmi323_bus_api bosch_bmi323_i2c_bus_api = {
	.read_words  = bosch_bmi323_i2c_read_words,
	.write_words = bosch_bmi323_i2c_write_words,
	.init        = bosch_bmi323_i2c_init,
};