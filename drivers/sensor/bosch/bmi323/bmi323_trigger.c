/*
 * Copyright (c) 2026 Caninos Loucos
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "bmi323.h"

static void bosch_bmi323_irq_callback(const struct device *port, struct gpio_callback *cb,
				     uint32_t pin)
{
	struct bosch_bmi323_data *data =
		CONTAINER_OF(cb, struct bosch_bmi323_data, gpio_callback);

	k_work_submit(&data->callback_work);
}

static void bosch_bmi323_irq_callback_handler(struct k_work *item)
{
	struct bosch_bmi323_data *data =
		CONTAINER_OF(item, struct bosch_bmi323_data, callback_work);

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->trigger_handler != NULL) {
		data->trigger_handler(data->dev, data->trigger);
	}

	k_mutex_unlock(&data->lock);
}

int bosch_bmi323_driver_api_trigger_set_fifo_full(const struct device *dev)
{
	uint16_t buf[2];

	buf[0] = 0;
	buf[1] = IMU_BOSCH_BMI323_REG_VALUE(INT_MAP2, ACC_DRDY_INT, INT1);

	return bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_INT_MAP1, buf, 2);
}

int bosch_bmi323_driver_api_trigger_set_acc_drdy(const struct device *dev)
{
	uint16_t buf[2];

	buf[0] = 0;
	buf[1] = IMU_BOSCH_BMI323_REG_VALUE(INT_MAP2, ACC_DRDY_INT, INT1);

	return bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_INT_MAP1, buf, 2);
}

int bosch_bmi323_driver_api_trigger_set_acc_motion(const struct device *dev)
{
	uint16_t buf[2];
	int ret;

	buf[0] = IMU_BOSCH_BMI323_REG_VALUE(INT_MAP1, MOTION_OUT, INT1);
	buf[1] = 0;

	ret = bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_INT_MAP1, buf, 2);

	if (ret < 0) {
		return ret;
	}

	buf[0] = 0;

	ret = bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_FEATURE_IO0, buf, 1);

	if (ret < 0) {
		return ret;
	}

	buf[0] = IMU_BOSCH_BMI323_REG_VALUE(FEATURE_IO0, MOTION_X_EN, EN) |
		 IMU_BOSCH_BMI323_REG_VALUE(FEATURE_IO0, MOTION_Y_EN, EN) |
		 IMU_BOSCH_BMI323_REG_VALUE(FEATURE_IO0, MOTION_Z_EN, EN);

	ret = bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_FEATURE_IO0, buf, 1);

	if (ret < 0) {
		return ret;
	}

	buf[0] = 1;

	return bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_FEATURE_IO_STATUS, buf, 1);
}

int bosch_bmi323_driver_api_trigger_set(const struct device *dev,
					       const struct sensor_trigger *trig,
					       sensor_trigger_handler_t handler)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret = -ENODEV;

	k_mutex_lock(&data->lock, K_FOREVER);

	data->trigger = trig;
	data->trigger_handler = handler;

	switch (trig->chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		switch (trig->type) {
		case SENSOR_TRIG_DATA_READY:
			ret = bosch_bmi323_driver_api_trigger_set_acc_drdy(dev);

			break;

		case SENSOR_TRIG_MOTION:
			ret = bosch_bmi323_driver_api_trigger_set_acc_motion(dev);

			break;
		case SENSOR_TRIG_FIFO_FULL:
			ret = bosch_bmi323_driver_api_trigger_set_fifo_full(dev);

			break;
		default:
			break;
		}

		break;

	default:
		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}


int bosch_bmi323_init_int1(const struct device *dev)
{
	uint16_t buf;

	buf = IMU_BOSCH_BMI323_REG_VALUE(IO_INT_CTRL, INT1_LVL, ACT_HIGH) |
	      IMU_BOSCH_BMI323_REG_VALUE(IO_INT_CTRL, INT1_OD, PUSH_PULL) |
	      IMU_BOSCH_BMI323_REG_VALUE(IO_INT_CTRL, INT1_OUTPUT_EN, EN);

	return bosch_bmi323_bus_write_words(dev, IMU_BOSCH_BMI323_REG_IO_INT_CTRL, &buf, 1);
}

int bosch_bmi323_trigger_init(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	struct bosch_bmi323_config *config = (struct bosch_bmi323_config *)dev->config;
	int ret;

	ret = bosch_bmi323_init_int1(dev);
	if (ret < 0) {
		return ret;
	}

	k_work_init(&data->callback_work, bosch_bmi323_irq_callback_handler);


	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);

	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&data->gpio_callback, bosch_bmi323_irq_callback,
			   BIT(config->int_gpio.pin));

	ret = gpio_add_callback(config->int_gpio.port, &data->gpio_callback);

	if (ret < 0) {
		return ret;
	}

	return gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_DISABLE);
}
