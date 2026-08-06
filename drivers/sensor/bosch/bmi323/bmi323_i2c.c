/*
 * Copyright (c) 2026 Caninos Loucos
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#include "bmi323.h"
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdlib.h>
#include <errno.h>

static int bosch_bmi323_i2c_read_words(const void *context, uint8_t offset,
				       uint16_t *words, uint16_t words_count)
{
	const struct i2c_dt_spec *i2c = (const struct i2c_dt_spec *)context;
	int len = words_count * 2 + 2; // Add space for first dummy word
	int ret;
	uint8_t *rx = (uint8_t*)calloc(len, sizeof(uint8_t));
	if (rx == NULL) {
		printk("Could not get this fucking shit");
		return -ENOMEM;
	}
	rx[0] = offset;

	// have to send a dummy write
	ret = i2c_write_dt(i2c, rx, 1);

	if (ret < 0) {
		return ret;
	}

	ret = i2c_read_dt(i2c, rx, len); 

	if (ret < 0) {
		return ret;
	}

	memcpy(words, rx + 2, len - 2);

	free(rx);

	return 0;
}

static int bosch_bmi323_i2c_write_words(const void *context, uint8_t offset,
					uint16_t *words, uint16_t words_count)
{
	const struct i2c_dt_spec *i2c = (const struct i2c_dt_spec *)context;

	int ret = i2c_burst_write_dt(i2c, offset, (uint8_t *)words, words_count * 2);

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