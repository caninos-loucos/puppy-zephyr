#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include "soc.h"

typedef struct __packed {
	uint32_t cfg_lo;
	uint32_t cfg_hi;
	uint32_t cnt_lo;
	uint32_t cnt_hi;
	uint32_t cmp_lo;
	uint32_t cmp_hi;
	uint32_t start_lo;
	uint32_t start_hi;
	uint32_t reset_lo;
	uint32_t reset_hi;
} puppy_timer_t;

#define CYC_PER_TICK (sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)

static struct k_spinlock lock;

static volatile uint64_t accumulated_cycle_count = 0ULL;

static volatile puppy_timer_t *timer = (volatile puppy_timer_t *)(PULP_APB_TIMER_BASE_ADDR);

uint64_t sys_clock_cycle_get_64(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint64_t ret = timer->cnt_lo;
	ret += accumulated_cycle_count;
	k_spin_unlock(&lock, key);
	return ret;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return (uint32_t)sys_clock_cycle_get_64();
}

uint32_t sys_clock_elapsed(void)
{
	return 0;
}

static void puppy_timer_irq_handler(void *unused)
{
	ARG_UNUSED(unused);
	accumulated_cycle_count += CYC_PER_TICK;
	sys_clock_announce(1);
}

static int sys_clock_driver_init(void)
{
	IRQ_CONNECT(PULP_TIMER_LO_IRQ, 0, puppy_timer_irq_handler, NULL, 0);

	timer->start_lo = 0x0;
	timer->reset_lo = 0x1;

	while (timer->cfg_lo & BIT(1))
		;

	timer->cfg_lo = 0x14;
	timer->cmp_lo = CYC_PER_TICK - 1UL;
	timer->cnt_lo = 0x0;

	timer->start_lo = 0x1;
	irq_enable(PULP_TIMER_LO_IRQ);

	return 0;
}

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
