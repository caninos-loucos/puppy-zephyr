#define DT_DRV_COMPAT caninos_puppy_gpio

#include <errno.h>
#include <soc.h>
#include <zephyr/arch/common/sys_bitops.h>
#include <zephyr/drivers/gpio.h>
#include "zephyr/drivers/gpio/gpio_utils.h"
#include "gpio_puppy.h"

struct gpio_puppy_config {
	uint32_t base;
};

struct gpio_puppy_data {
	sys_slist_t callbacks;
};

/**
 * @brief Configure pin
 *
 * @param dev Device structure
 * @param pin The pin number
 * @param flags Flags of pin or port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_puppy_config *config = dev->config;

	int pad_bit;
	int group;

	if (pin > 31) {
		return -EINVAL;
	}

	group = pin / 16;
	pad_bit = (pin % 16) * 2;

	/* Configure pin as gpio */
	/* GPIO pin cannot be input and output simultaneously */
	if ((flags & GPIO_INPUT) && (flags & GPIO_OUTPUT)) {
		PULP_PADMUX(group) &= ~(PULP_PAD_GPIO << pad_bit);
		sys_write32(sys_read32(config->base + GPIO_REG_GPIOEN) & ~BIT(pin),
			    config->base + GPIO_REG_GPIOEN);
	} else if ((flags & GPIO_INPUT) || (flags & GPIO_OUTPUT)) {
		PULP_PADMUX(group) |= (PULP_PAD_GPIO << pad_bit);
		sys_write32(sys_read32(config->base + GPIO_REG_GPIOEN) | BIT(pin),
			    config->base + GPIO_REG_GPIOEN);
	}

	/* Configure gpio direction */
	if (flags & GPIO_INPUT) {

		sys_write32(sys_read32(config->base + GPIO_REG_PADDIR) & ~BIT(pin),
			    config->base + GPIO_REG_PADDIR);
	}

	else if (flags & GPIO_OUTPUT) {
		sys_write32(sys_read32(config->base + GPIO_REG_PADDIR) | BIT(pin),
			    config->base + GPIO_REG_PADDIR);

		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			sys_write32(config->base + GPIO_REG_PADOUTSET, BIT(pin));
		} else {
			sys_write32(config->base + GPIO_REG_PADOUTCLR, BIT(pin));
		}
	}

	/*
	 * Configure interrupt if GPIO_INT is set.
	 * Here, we just configure the gpio interrupt behavior,
	 * we do not enable/disable interrupt for a particular
	 * gpio.
	 * Interrupt for a gpio is:
	 * 1) enabled only via a call to gpio_puppy_enable_callback.
	 * 2) disabled only via a call to gpio_puppy_disabled_callback.
	 */
	if (!(flags & GPIO_INT_ENABLE)) {
		return 0;
	}

	/* Edge or Level triggered ? */
	if (!(flags & GPIO_INT_EDGE)) {
		return -ENOTSUP;
	}

	return 0;
}

/**
 * @brief Read GPIO port
 *
 * @param dev Device struct
 * @param value Value to set (0 or 1)
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_puppy_config *config = dev->config;

	*value = sys_read32(config->base + GPIO_REG_PADIN);

	return 0;
}

/**
 * @brief Set port according to mask and value
 *
 * @param dev Device struct
 * @param mask The mask for GPIO port
 * @param value Value of input pin(s)
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	const struct gpio_puppy_config *config = dev->config;

	if (mask & value) {
		sys_write32(mask & value, config->base + GPIO_REG_PADOUTSET);
	}
	if (mask & ~value) {
		sys_write32(mask & ~value, config->base + GPIO_REG_PADOUTCLR);
	}

	return 0;
}

/**
 * @brief Set bits according to mask
 *
 * @param dev Device struct
 * @param mask The mask for GPIO port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_port_set_bits_raw(const struct device *dev, gpio_port_pins_t mask)
{
	const struct gpio_puppy_config *config = dev->config;

	sys_write32(mask, config->base + GPIO_REG_PADOUTSET);

	return 0;
}

/**
 * @brief Clear bits according to mask
 *
 * @param dev Device struct
 * @param mask The mask for GPIO port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t mask)
{
	const struct gpio_puppy_config *config = dev->config;

	sys_write32(mask, config->base + GPIO_REG_PADOUTCLR);

	return 0;
}

/**
 * @brief Toggle bits according to mask
 *
 * @param dev Device struct
 * @param mask The mask for GPIO port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_port_toggle_bits(const struct device *dev, gpio_port_pins_t mask)
{
	const struct gpio_puppy_config *config = dev->config;

	uint32_t value = sys_read32(config->base + GPIO_REG_PADOUT);

	return gpio_puppy_port_set_masked_raw(dev, mask, ~value);

	return 0;
}

/**
 * @brief Configure pin interrupt settings
 *
 * @param dev Device struct
 * @param pin GPIO pin
 * @param flags GPIO Interrupt flags
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					      enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_puppy_config *config = dev->config;
	uint32_t inttype = 0;

	if (mode == GPIO_INT_MODE_DISABLED) {
		sys_write32(sys_read32(config->base + GPIO_REG_INTEN) & ~BIT(pin),
			    config->base + GPIO_REG_INTEN);
		return 0;
	}

	if (mode == GPIO_INT_MODE_LEVEL) {
		return -ENOTSUP;
	}

	switch (trig) {
	case GPIO_INT_TRIG_LOW:
		inttype = 0;
		break;
	case GPIO_INT_TRIG_HIGH:
		inttype = 1;
		break;
	case GPIO_INT_TRIG_BOTH:
		inttype = 2;
		break;
	default:
		return -EINVAL;
	}

	if (pin < 16) {
		sys_write32(sys_read32(config->base + GPIO_REG_INTTYPE_00_15) & ~(0x3 << (pin * 2)),
			    config->base + GPIO_REG_INTTYPE_00_15);
		sys_write32(sys_read32(config->base + GPIO_REG_INTTYPE_00_15) |
				    (inttype << (pin * 2)),
			    config->base + GPIO_REG_INTTYPE_00_15);
	} else {
		sys_write32(sys_read32(config->base + GPIO_REG_INTTYPE_16_31) & ~(0x3 << (pin * 2)),
			    config->base + GPIO_REG_INTTYPE_16_31);
		sys_write32(sys_read32(config->base + GPIO_REG_INTTYPE_16_31) |
				    (inttype << (pin * 2)),
			    config->base + GPIO_REG_INTTYPE_16_31);
	}

	sys_write32(sys_read32(config->base + GPIO_REG_INTEN) | BIT(pin),
		    config->base + GPIO_REG_INTEN);

	return 0;
}

/**
 * @brief Configure callbacks
 *
 * @param dev Device struct
 * @param callback callback handle
 * @param set
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_puppy_manage_callback(const struct device *dev, struct gpio_callback *callback,
				      bool set)
{
	struct gpio_puppy_data *data = dev->data;

	gpio_manage_callback(&data->callbacks, callback, set);

	return 0;
}

/**
 * @brief Handles GPIO ISR
 *
 * Perform basic initialization of a GPIO controller
 *
 * @param dev GPIO device structs
 *
 * @return 0
 */
static void gpio_puppy_isr(const struct device *dev)
{
	const struct gpio_puppy_config *config = dev->config;
	struct gpio_puppy_data *data = dev->data;

	/* Interrupts cleared automatically when intstatus register is read */
	uint32_t int_status = sys_read32(config->base + GPIO_REG_INTSTATUS);

	gpio_fire_callbacks(&data->callbacks, dev, int_status);
}

static DEVICE_API(gpio, gpio_puppy_driver_api) = {
	.pin_configure = gpio_puppy_configure,
	.port_get_raw = gpio_puppy_port_get_raw,
	.port_set_masked_raw = gpio_puppy_port_set_masked_raw,
	.port_set_bits_raw = gpio_puppy_port_set_bits_raw,
	.port_clear_bits_raw = gpio_puppy_port_clear_bits_raw,
	.port_toggle_bits = gpio_puppy_port_toggle_bits,
	.pin_interrupt_configure = gpio_puppy_pin_interrupt_configure,
	.manage_callback = gpio_puppy_manage_callback,
};

#define GPIO_PUPPY_INIT(idx)                                                                       \
	static const struct gpio_puppy_config gpio_puppy_##idx##_config = {                        \
		.base = DT_INST_REG_ADDR(idx),                                                     \
	};                                                                                         \
                                                                                                   \
	static struct gpio_puppy_data gpio_puppy_##idx##_data = {                                  \
		.callbacks = NULL,                                                                 \
	};                                                                                         \
                                                                                                   \
	static int gpio_puppy_##idx##_init(const struct device *dev)                               \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(idx), 0, gpio_puppy_isr, DEVICE_DT_INST_GET(idx), 0);     \
                                                                                                   \
		irq_enable(DT_INST_IRQN(idx));                                                     \
		return 0;                                                                          \
	}                                                                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, gpio_puppy_##idx##_init, NULL, &gpio_puppy_##idx##_data,        \
			      &gpio_puppy_##idx##_config, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, \
			      &gpio_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_PUPPY_INIT)
