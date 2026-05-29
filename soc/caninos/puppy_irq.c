#include <zephyr/irq.h>
#include <soc.h>

void arch_irq_enable(unsigned int irq)
{
	unsigned int key = irq_lock();
	PULP_IRQ_MASK_SET = (1 << irq);
	irq_unlock(key);
};

void arch_irq_disable(unsigned int irq)
{
	unsigned int key = irq_lock();
	PULP_IRQ_MASK_CLR = (1 << irq);
	irq_unlock(key);
};

int arch_irq_is_enabled(unsigned int irq)
{
	return !(PULP_IRQ_MASK & (1 << irq)) ? 0 : 1;
}
