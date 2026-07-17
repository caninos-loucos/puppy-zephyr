/*
 * Copyright (c) 2026 Ana Clara Forcelli - LSITEC
 * Copyright (c) 2026 Edgar Bernardi Righi - LSITEC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_gpio

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_puppy);

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <soc.h>

struct gpio_puppy_config {
    struct gpio_driver_config common; /* this MUST be first */
    uint32_t base, port;
};

struct gpio_puppy_data {
    struct gpio_driver_data common; /* this MUST be first */
    sys_slist_t callbacks;          /* list of callbacks */
};

struct gpio_puppy {
    uint32_t PADDIR;        // BASEADDR + 0x00
    uint32_t GPIOEN;        // BASEADDR + 0x04
    uint32_t PADIN;         // BASEADDR + 0x08
    uint32_t PADOUT;        // BASEADDR + 0x0c
    uint32_t PADOUTSET;     // BASEADDR + 0x10
    uint32_t PADOUTCLR;     // BASEADDR + 0x14
    uint32_t INTEN;         // BASEADDR + 0x18
    uint32_t INTTYPE_00_15; // BASEADDR + 0x1c
    uint32_t INTTYPE_16_31; // BASEADDR + 0x20
    uint32_t INTSTATUS;     // BASEADDR + 0x24
    uint32_t PADCFG_00_07;  // BASEADDR + 0x28
    uint32_t PADCFG_08_15;  // BASEADDR + 0x2c
    uint32_t PADCFG_16_23;  // BASEADDR + 0x30
    uint32_t PADCFG_24_31;  // BASEADDR + 0x34
} __packed;

#define PUPPY_MAX_GPIO (32U)

static inline volatile struct gpio_puppy* GET_GPIO(const struct device *dev)
{
    const struct gpio_puppy_config *config = dev->config;
    return (volatile struct gpio_puppy *)(config->base);
}

static inline uint32_t GET_PAD(const struct device *dev, gpio_pin_t pin)
{
    const struct gpio_puppy_config *config = dev->config;
    return (uint32_t)(pin * (gpio_pin_t)(config->port));
}

static int gpio_puppy_configure(const struct device *dev,
                                gpio_pin_t pin, gpio_flags_t flags)
{
    volatile struct gpio_puppy *gpio = GET_GPIO(dev);

    /* Check input parameters: pin number */
    if (pin >= PUPPY_MAX_GPIO) {
        return -ENOTSUP;
    }
    /* Check input parameters: simultaneous in/out mode */
    if ((flags & GPIO_OUTPUT) && (flags & GPIO_INPUT)) {
        return -ENOTSUP;
    }

    /* Configure pin as gpio */
    config_pad_func(GET_PAD(dev, pin), 0x1); /* gpio is always function 0x1 */

    /* Enable the gpio's clock */
    gpio->GPIOEN |= BIT(pin);

    if (flags & GPIO_OUTPUT)
    {
        if (flags & GPIO_OUTPUT_INIT_HIGH) {
            gpio->PADOUTSET = BIT(pin);
        }
        else {
            gpio->PADOUTCLR = BIT(pin);
        }
        gpio->PADDIR |= BIT(pin);
    }
    else {
        gpio->PADDIR &= ~BIT(pin);
    }
    return 0;
}

static int gpio_puppy_port_get_raw(const struct device *dev,
                                   gpio_port_value_t *value)
{
    *value = GET_GPIO(dev)->PADIN;
    return 0;
}

static int gpio_puppy_port_set_masked_raw(const struct device *dev,
                                          gpio_port_pins_t mask,
                                          gpio_port_value_t value)
{
    GET_GPIO(dev)->PADOUT = (GET_GPIO(dev)->PADOUT & ~mask) | (value & mask);
    return 0;
}

static int gpio_puppy_port_set_bits_raw(const struct device *dev,
                                        gpio_port_pins_t mask)
{
    GET_GPIO(dev)->PADOUTSET = mask;
    return 0;
}

static int gpio_puppy_port_clear_bits_raw(const struct device *dev,
                                          gpio_port_pins_t mask)
{
    GET_GPIO(dev)->PADOUTCLR = mask;
    return 0;
}

static int gpio_puppy_port_toggle_bits(const struct device *dev,
                                       gpio_port_pins_t mask)
{
    GET_GPIO(dev)->PADOUT ^= mask;
    return 0;
}

static int gpio_puppy_pin_irq_set(const struct device *dev,
                                  gpio_pin_t pin, enum gpio_int_trig trig)
{
    volatile struct gpio_puppy *gpio = GET_GPIO(dev);
    uint32_t bit = 2 * (pin % 16); 
    uint32_t inttype = 0UL;

    switch (trig) {
    case GPIO_INT_TRIG_LOW:
        inttype = 0UL;
        break;
    case GPIO_INT_TRIG_HIGH:
        inttype = 1UL;
        break;
    case GPIO_INT_TRIG_BOTH:
        inttype = 2UL;
        break;
    default:
        return -ENOTSUP;
    }
    
    gpio->INTEN &= ~BIT(pin);

    if (pin < 16) {
        gpio->INTTYPE_00_15 &= ~(0x3 << bit);
        gpio->INTTYPE_00_15 |= (inttype << bit);
    }
    else {
        gpio->INTTYPE_16_31 &= ~(0x3 << bit);
        gpio->INTTYPE_16_31 |= (inttype << bit);
    }

    gpio->INTEN |= BIT(pin);
    return 0;
}

static int gpio_puppy_pin_interrupt_configure(const struct device *dev,
                                              gpio_pin_t pin,
                                              enum gpio_int_mode mode,
                                              enum gpio_int_trig trig)
{
    switch (mode) {
    case GPIO_INT_MODE_DISABLED:
        GET_GPIO(dev)->INTEN &= ~BIT(pin);
        return 0;
    case GPIO_INT_MODE_EDGE:
        return gpio_puppy_pin_irq_set(dev, pin, trig);
    default:
        return -ENOTSUP;
    }
}

static int gpio_puppy_manage_callback(const struct device *dev,
                                      struct gpio_callback *callback,
                                      bool set)
{
    struct gpio_puppy_data *data = dev->data;
    return gpio_manage_callback(&data->callbacks, callback, set);
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

static void gpio_puppy_fire_callbacks(const struct device *dev)
{
    struct gpio_puppy_data *data = dev->data;
    
    /* Interrupts cleared automatically when intstatus register is read */
    uint32_t status = GET_GPIO(dev)->INTSTATUS;
    
    if (status != 0) {
        gpio_fire_callbacks(&data->callbacks, dev, status);
    }
}

static void gpio_puppy_isr(void *param);

static bool gpio_puppy_isr_connected = false;

static int gpio_puppy_init(const struct device *dev)
{
    if (!gpio_puppy_isr_connected)
    {
        IRQ_CONNECT(PUPPY_GPIO_IRQ, 0, gpio_puppy_isr, NULL, 0);
        irq_enable(PUPPY_GPIO_IRQ);
        gpio_puppy_isr_connected = true;
    }
    return 0;
}

/* GPIO driver registration */
#define GPIO_PUPPY_INIT(_id)                                             \
    static const struct gpio_puppy_config gpio_puppy_##_id##_config = {  \
        .common = {                                                      \
            .port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(_id),       \
        },                                                               \
        .base = DT_REG_ADDR(DT_NODELABEL(gpio##_id)),                    \
        .port = _id,                                                     \
    };                                                                   \
    static struct gpio_puppy_data gpio_puppy_##_id##_data;               \
                                                                         \
    DEVICE_DT_DEFINE(DT_NODELABEL(gpio##_id),                            \
        gpio_puppy_init, NULL,                                           \
        &gpio_puppy_##_id##_data, &gpio_puppy_##_id##_config,            \
        PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,                         \
        &gpio_puppy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_PUPPY_INIT)

static void gpio_puppy_isr(void *param)
{
    ARG_UNUSED(param);
    
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio0))
    gpio_puppy_fire_callbacks(DEVICE_DT_INST_GET(0));
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio1))
    gpio_puppy_fire_callbacks(DEVICE_DT_INST_GET(1));
#endif
}
