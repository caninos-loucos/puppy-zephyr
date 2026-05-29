#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "soc.h"

typedef struct {
	event_callback_t func;
	void *userdata;
} event_desc_s;

static volatile event_desc_s callbacks[SOC_MAX_NUM_EVENT_CALLBACKS];

static void puppy_event_irq_handler(void *unused)
{
	unsigned int key = irq_lock();
	uint32_t event_num;
	int i;
	ARG_UNUSED(unused);
	event_num = PULP_REG(PULP_IRQ_FIFO_DATA_GET_REG);

	for (i = 0; i < SOC_MAX_NUM_EVENT_CALLBACKS; i++) {
		if (callbacks[i].func != NULL) {
			callbacks[i].func(event_num, callbacks[i].userdata);
		}
	}
	irq_unlock(key);
}

int puppy_event_register_callback(event_callback_t callback, void *userdata)
{
	unsigned int key = irq_lock();
	int ret = -ENOBUFS, i;

	for (i = 0; i < SOC_MAX_NUM_EVENT_CALLBACKS; i++) {
		if (callbacks[i].func == NULL) {
			callbacks[i].func = callback;
			callbacks[i].userdata = userdata;
			ret = 0;
			break;
		}
	}
	irq_unlock(key);
	return ret;
}

int puppy_event_unregister_callback(event_callback_t callback, void *userdata)
{
	unsigned int key = irq_lock();
	int ret = -EINVAL, i;

	for (i = 0; i < SOC_MAX_NUM_EVENT_CALLBACKS; i++) {
		if (callbacks[i].func == callback && callbacks[i].userdata == userdata) {
			callbacks[i].func = NULL;
			callbacks[i].userdata = NULL;
			ret = 0;
		}
	}
	irq_unlock(key);
	return ret;
}

int puppy_event_is_enabled(unsigned int event_num)
{
	unsigned int key = irq_lock();
	unsigned int bit = event_num % 32UL;
	unsigned int off = event_num / 32UL;
	int ret = -EINVAL;

	if (off < SOC_NB_EVENT_REGS) {
		ret = !(PULP_REG(PULP_EU_FC_MASK0_REG + 4UL * off) & BIT(bit)) ? 1 : 0;
	}
	irq_unlock(key);
	return ret;
}

int puppy_event_enable(unsigned int event_num)
{
	unsigned int key = irq_lock();
	unsigned int bit = event_num % 32UL;
	unsigned int off = event_num / 32UL;
	int ret = -EINVAL;

	if (off < SOC_NB_EVENT_REGS) {
		PULP_REG(PULP_EU_FC_MASK0_REG + 4UL * off) &= ~BIT(bit);
		ret = 0;
	}
	irq_unlock(key);
	return ret;
}

int puppy_event_disable(unsigned int event_num)
{
	unsigned int key = irq_lock();
	unsigned int bit = event_num % 32UL;
	unsigned int off = event_num / 32UL;
	int ret = -EINVAL;

	if (off < SOC_NB_EVENT_REGS) {
		PULP_REG(PULP_EU_FC_MASK0_REG + 4UL * off) |= BIT(bit);
		ret = 0;
	}
	irq_unlock(key);
	return ret;
}

/* Initialize SoC events status and enable IRQ. */
int sys_event_init(void)
{
	unsigned int key = irq_lock();
	unsigned int i;

	for (i = 0UL; i < SOC_MAX_NUM_EVENT_CALLBACKS; i++) {
		callbacks[i].func = NULL;
		callbacks[i].userdata = NULL;
	}
	for (i = 0UL; i < SOC_NB_EVENT_REGS; i++) {
		PULP_REG(PULP_EU_FC_MASK0_REG + 4UL * i) = 0xffffffff;
	}

	IRQ_CONNECT(PULP_SOC_EVENT_IRQ, 0, puppy_event_irq_handler, NULL, 0);

	irq_unlock(key);
	irq_enable(PULP_SOC_EVENT_IRQ);
	return 0;
}

SYS_INIT(sys_event_init, PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY);
