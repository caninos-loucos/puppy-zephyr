/*
 * Copyright (c) 2026 Ana Clara Forcelli <ana.forcelli@lsitec.org.br>
 * Copyright (c) 2026 Edgar Bernardi Righi <edgar.righi@lsitec.org.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT caninos_puppy_counter

#define LOG_LEVEL CONFIG_COUNTER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(counter_puppy);

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#define CFG_PRESCALER_EN  BIT(6)
#define CFG_RESTART_MATCH BIT(4)
#define CFG_IRQ_EN        BIT(2)
#define CFG_RESET         BIT(1)
#define CFG_START         BIT(0)

#define CFG_PRESCALER_SHIFT 8U

/*
 * Puppy timer prescaler: f_timer = f_clk / (1 + PVAL)
 *
 * The Puppy timer clock is currently 20 MHz, so DIV=10 (PVAL=9)
 * gives a 2 MHz Zephyr counter clock, i.e. 0.5 us per tick.
 *
 * Keeping the prescaler enabled is also important for the compare/reset
 * algorithm used by this driver. On Puppy the compare IRQ is generated
 * when CNT reaches CMP and CMP_CLR resets the main counter on the following
 * FLL clock. With DIV > 1 that reset happens before the next prescaled timer
 * tick, so a logical segment of N timer ticks is programmed as CMP=N.
 *
 * The behaviour is different when the prescaler is disabled. In that case
 * the main counter advances on every FLL clock, so after CNT reaches CMP
 * one additional timer tick elapses before CMP_CLR resets the counter.
 * Consequently, CMP=N would produce a segment of N+1 timer ticks. An exact
 * N-tick segment would normally require CMP=N-1, and a one-tick segment
 * cannot be represented directly by the compare/reset mechanism because
 * CMP=0 would only match after the counter wraps around.
 *
 * Using a prescaler with DIV > 1 therefore avoids this off-by-one behaviour
 * and allows every segment, including a one-tick segment, to be represented
 * directly with CMP equal to the desired number of timer ticks.
 */
#define COUNTER_PUPPY_PRESCALER_DIV  10U
#define COUNTER_PUPPY_PRESCALER_PVAL (COUNTER_PUPPY_PRESCALER_DIV - 1U)

BUILD_ASSERT(COUNTER_PUPPY_PRESCALER_DIV >= 2U,
	     "Puppy prescaler must divide by at least 2 in this driver");
BUILD_ASSERT(COUNTER_PUPPY_PRESCALER_DIV <= 256U,
	     "Puppy prescaler is 8 bits");

#define CFG_PRESCALER_VALUE \
	((uint32_t)COUNTER_PUPPY_PRESCALER_PVAL << CFG_PRESCALER_SHIFT)

#define CFG_SAFE_BASE \
	(CFG_PRESCALER_EN | CFG_PRESCALER_VALUE | \
	 CFG_RESTART_MATCH | CFG_IRQ_EN)

/*
 * Keep one count below UINT32_MAX so that a programmed compare always
 * happens before the 32-bit counter can overflow.
 */
#define COUNTER_PUPPY_MAX_TOP (UINT32_MAX - 1U)

#define PUPPY_EVENT_TOP   BIT(0)
#define PUPPY_EVENT_ALARM BIT(1)

struct counter_puppy_top_data {
	counter_top_callback_t callback;
	void *user_data;
	uint32_t ticks;
};

struct counter_puppy_data {
	struct counter_alarm_cfg alarm_cfg;
	struct counter_puppy_top_data top_data;

	/* Value passed to the alarm callback. */
	uint32_t alarm_target;
	uint32_t guard_period;
	/*
	 * Logical value corresponding to physical CNT_HI == 0.
	 * The hardware may restart at an intermediate alarm, but the Zephyr
	 * counter must continue from the logical alarm value. counter_base
	 * provides that virtualization.
	 */
	uint32_t counter_base;
	/*
	 * Physical compare value currently in CMP_HI.
	 * With the Puppy prescaler enabled, CMP is also the number of
	 * logical timer ticks in the current physical segment.
	 */
	uint32_t compare_ticks;
	uint8_t compare_events;

	/* Alarm is in the logical period after the next TOP. */
	bool alarm_after_top;

	/* Alarm expires exactly at the next logical TOP/turnaround. */
	bool alarm_on_top;
};

struct counter_puppy_config {
	/*
	 * Zephyr counter API expects counter_config_info at the 
	 * beginning of dev->config.
	 */
	struct counter_config_info info;

	PUPPY_TIMER_Type *timer;

	void (*irq_config_func)(void);
};

static uint64_t counter_puppy_period(const struct device *dev)
{
	struct counter_puppy_data *data = dev->data;

	return (uint64_t)data->top_data.ticks;
}

static uint32_t counter_puppy_backward_distance(
	const struct device *dev, uint32_t now, uint32_t target)
{
	uint64_t period = counter_puppy_period(dev);

	if (now >= target) {
		return now - target;
	}

	return (uint32_t)((uint64_t)now + period - target);
}

/*
 * Convert the physical counter value into the virtual/logical counter value.
 * counter_base and raw are each smaller than TOP, so one subtraction is
 * sufficient in normal operation. uint64_t makes the addition safe.
 */
static uint32_t counter_puppy_logical_value(
	const struct device *dev, uint32_t raw)
{
	struct counter_puppy_data *data = dev->data;
	uint64_t top = counter_puppy_period(dev);
	uint64_t value = (uint64_t)data->counter_base + (uint64_t)raw;

	if (value >= top) {
		value -= top;
	}

	return (uint32_t)value;
}

/*
 * Stop counting while keeping MATCH/RESET and compare IRQ enabled.
 * This is important: at no point do we put the running timer in free-running
 * mode. The old CMP_HI remains a valid reset boundary until the timer is
 * stopped, so reprogramming CMP_HI cannot create a path to hardware overflow.
 */
static bool counter_puppy_freeze(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;
	bool was_running = (cfg->timer->CFG_HI & CFG_START) != 0U;

	cfg->timer->CFG_HI = CFG_SAFE_BASE;

	return was_running;
}

static void counter_puppy_resume(const struct device *dev, bool was_running)
{
	const struct counter_puppy_config *cfg = dev->config;

	/* MATCH/RESET + IRQ are always kept enabled. */
	cfg->timer->CFG_HI = CFG_SAFE_BASE;

	if (was_running) {
		cfg->timer->START_HI = 1U;
	}
}

static void counter_puppy_hw_reset(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;

	cfg->timer->RESET_HI = 1U;
	while ((cfg->timer->CFG_HI & CFG_RESET) != 0U) {
		/* Wait for the hardware reset to complete. */
	}
}

static void counter_puppy_clear_alarm_state(const struct device *dev)
{
	struct counter_puppy_data *data = dev->data;

	data->alarm_cfg.callback = NULL;
	data->alarm_cfg.user_data = NULL;
	data->alarm_target = 0U;
	data->alarm_after_top = false;
	data->alarm_on_top = false;
}

/*
 * Program the next physical segment.
 *
 * State needed before calling this function:
 *   - hardware timer is stopped;
 *   - CNT_HI == 0;
 *   - counter_base is the logical value represented by physical zero.
 *
 * Every physical segment ends in MATCH/RESET and always generates an IRQ.
 */
static void counter_puppy_schedule_locked(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t top = data->top_data.ticks;
	uint32_t now = data->counter_base;
	uint32_t compare;
	uint8_t events;

	/*
	 * counter_base is always normalized to [0, TOP - 1].
	 * IMPORTANT: do not subtract one here.
	 *
	 * In the Puppy RTL the main counter compare is detected when the
	 * incremented counter reaches CMP. Because the prescaler is enabled,
	 * CMP_CLR resets the main counter on the following FLL clock, before
	 * the next prescaled timer tick. Therefore N logical timer ticks are
	 * represented by CMP=N.
	 */
	compare = top - now;
	events = PUPPY_EVENT_TOP;

	if (data->alarm_cfg.callback != NULL)
	{
		if (data->alarm_on_top) {
			events |= PUPPY_EVENT_ALARM;
		}
		else if (!data->alarm_after_top)
		{
			/*
			 * The alarm belongs to this logical period and
			 * must be ahead of counter_base.
			 */
			if ((data->alarm_target > now) &&
			    (data->alarm_target < top))
			{
				compare = data->alarm_target - now;
				events = PUPPY_EVENT_ALARM;
			}
			else {  /*
				 * Defensive fallback!
				 * Never install CMP_HI == 0 or a
				 * compare behind CNT_HI.
				 * Just keep the safe TOP boundary.
				 */
				LOG_ERR("Invalid active alarm state: "
					"base=%u target=%u top=%u",
					now, data->alarm_target, top);
				data->alarm_after_top = true;
			}
		}
	}

	/*
	 * TOP is nonzero and counter_base < TOP, therefore compare is always
	 * in [1, TOP] and TOP <= UINT32_MAX - 1. The compare match is reached
	 * before the physical counter can overflow; CMP_CLR then resets it
	 * before the next prescaled timer tick.
	 */
	data->compare_ticks = compare;
	data->compare_events = events;

	cfg->timer->CMP_HI = compare;
	cfg->timer->CFG_HI = CFG_SAFE_BASE;
}

static int counter_puppy_start(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;

	cfg->timer->CFG_HI = CFG_SAFE_BASE;
	cfg->timer->START_HI = 1U;

	return 0;
}

static int counter_puppy_stop(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;

	/* Stop, but never leave free-running mode configured. */
	cfg->timer->CFG_HI = CFG_SAFE_BASE;

	return 0;
}

static int counter_puppy_get_value(const struct device *dev, uint32_t *ticks)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t base_before;
	uint32_t base_after;
	uint32_t raw;
	uint64_t value;
	uint64_t top;

	/*
	 * Avoid returning a value assembled from two different software bases
	 * if an ISR runs between the reads.
	 */
	do {
		base_before = data->counter_base;
		raw = cfg->timer->CNT_HI;
		base_after = data->counter_base;
	} while (base_before != base_after);

	top = counter_puppy_period(dev);
	value = (uint64_t)base_before + (uint64_t)raw;

	if (value >= top) {
		value -= top;
	}

	*ticks = (uint32_t)value;

	return 0;
}

static int counter_puppy_reset(const struct device *dev)
{
	struct counter_puppy_data *data = dev->data;
	bool was_running;
	unsigned int key;

	key = irq_lock();
	was_running = counter_puppy_freeze(dev);

	counter_puppy_hw_reset(dev);
	data->counter_base = 0U;

	/*
	 * Preserve an active alarm at its logical target after the reset.
	 * A target of zero/TOP is represented by the next turnaround.
	 */
	if (data->alarm_cfg.callback != NULL) {
		data->alarm_after_top = false;
		data->alarm_on_top =
			(data->alarm_target == 0U) ||
			(data->alarm_target == data->top_data.ticks);
	}

	counter_puppy_schedule_locked(dev);
	counter_puppy_resume(dev, was_running);

	irq_unlock(key);

	return 0;
}

static int counter_puppy_set_alarm(
	const struct device *dev, uint8_t chan_id,
	const struct counter_alarm_cfg *alarm_cfg)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t now;
	uint32_t raw;
	uint32_t target;
	uint32_t ticks;
	uint32_t top;
	uint32_t distance;
	uint64_t sum;
	bool absolute;
	bool alarm_after_top = false;
	bool alarm_on_top = false;
	bool was_running;
	unsigned int key;

	if (chan_id != 0U) {
		return -ENOTSUP;
	}
	if ((alarm_cfg == NULL) || (alarm_cfg->callback == NULL)) {
		return -EINVAL;
	}
	if ((alarm_cfg->flags & COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE) != 0U) {
		return -ENOTSUP;
	}

	top = data->top_data.ticks;
	ticks = alarm_cfg->ticks;
	absolute = (alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) != 0U;

	if (absolute) {
		if (ticks > top) {
			return -EINVAL;
		}
	} else {
		/* A relative alarm may be as long as one complete period. */
		if ((ticks == 0U) || (ticks > top)) {
			return -EINVAL;
		}
	}

	key = irq_lock();

	if (data->alarm_cfg.callback != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	/*
	 * Freeze before changing CMP_HI. MATCH/RESET remains enabled while
	 * the timer transitions to the stopped state.
	 */
	was_running = counter_puppy_freeze(dev);
	raw = cfg->timer->CNT_HI;
	now = counter_puppy_logical_value(dev, raw);

	if (absolute) {
		target = ticks;

		distance = counter_puppy_backward_distance(dev, now, target);

		if ((target == now) ||
		    ((data->guard_period != 0U) &&
		     (distance < data->guard_period))) {
			counter_puppy_resume(dev, was_running);
			irq_unlock(key);
			return -ETIME;
		}

		if ((target == 0U) || (target == top)) {
			/* Logical zero/TOP is the next turnaround. */
			alarm_on_top = true;
		} else if (target < now) {
			alarm_after_top = true;
		}
	} else {
		sum = (uint64_t)now + (uint64_t)ticks;

		if (sum < (uint64_t)top) {
			target = (uint32_t)sum;
		} else if (sum == (uint64_t)top) {
			target = top;
			alarm_on_top = true;
		} else {
			target = (uint32_t)(sum - (uint64_t)top);
			alarm_after_top = true;
		}
	}

	/*
	 * Rebase the physical counter at the exact logical value observed
	 * above.  From this point CMP_HI is always a positive distance from
	 * CNT_HI == 0 to the next event.
	 */
	counter_puppy_hw_reset(dev);
	data->counter_base = now;

	data->alarm_cfg = *alarm_cfg;
	data->alarm_target = target;
	data->alarm_after_top = alarm_after_top;
	data->alarm_on_top = alarm_on_top;

	counter_puppy_schedule_locked(dev);
	counter_puppy_resume(dev, was_running);

	irq_unlock(key);

	return 0;
}

static int counter_puppy_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t raw;
	uint32_t now;
	bool was_running;
	unsigned int key;

	if (chan_id != 0U) {
		return -ENOTSUP;
	}

	key = irq_lock();

	was_running = counter_puppy_freeze(dev);
	raw = cfg->timer->CNT_HI;
	now = counter_puppy_logical_value(dev, raw);

	counter_puppy_hw_reset(dev);
	data->counter_base = now;

	counter_puppy_clear_alarm_state(dev);
	counter_puppy_schedule_locked(dev);
	counter_puppy_resume(dev, was_running);

	irq_unlock(key);

	return 0;
}

static int counter_puppy_set_top_value(
	const struct device *dev, const struct counter_top_cfg *top_cfg)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	uint32_t raw;
	uint32_t now;
	bool reset_counter = false;
	bool was_running;
	unsigned int key;

	if (top_cfg == NULL) {
		return -EINVAL;
	}
	if (top_cfg->ticks == 0U) {
		return -EINVAL;
	}
	if (top_cfg->ticks > cfg->info.max_top_value) {
		return -EINVAL;
	}

	key = irq_lock();

	if (data->alarm_cfg.callback != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	was_running = counter_puppy_freeze(dev);
	raw = cfg->timer->CNT_HI;
	now = counter_puppy_logical_value(dev, raw);

	if ((top_cfg->flags & COUNTER_TOP_CFG_DONT_RESET) == 0U) {
		reset_counter = true;
	} else if (now >= top_cfg->ticks) {
		if ((top_cfg->flags & COUNTER_TOP_CFG_RESET_WHEN_LATE) != 0U) {
			reset_counter = true;
		} else {
			counter_puppy_resume(dev, was_running);
			irq_unlock(key);
			return -ETIME;
		}
	}

	counter_puppy_hw_reset(dev);

	data->top_data.ticks = top_cfg->ticks;
	data->top_data.callback = top_cfg->callback;
	data->top_data.user_data = top_cfg->user_data;

	data->counter_base = reset_counter ? 0U : now;
	data->compare_ticks = 0U;
	data->compare_events = 0U;

	/* Keep the guard period valid if TOP is reduced. */
	if (data->guard_period >= data->top_data.ticks) {
		data->guard_period = data->top_data.ticks - 1U;
	}

	counter_puppy_schedule_locked(dev);
	counter_puppy_resume(dev, was_running);

	irq_unlock(key);

	return 0;
}

static uint32_t counter_puppy_get_pending_int(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0U;
}

static uint32_t counter_puppy_get_top_value(const struct device *dev)
{
	struct counter_puppy_data *data = dev->data;

	return data->top_data.ticks;
}

static uint32_t counter_puppy_get_guard_period(
	const struct device *dev, uint32_t flags)
{
	struct counter_puppy_data *data = dev->data;

	if (flags != COUNTER_GUARD_PERIOD_LATE_TO_SET) {
		return 0U;
	}

	return data->guard_period;
}

static int counter_puppy_set_guard_period(
	const struct device *dev, uint32_t ticks, uint32_t flags)
{
	struct counter_puppy_data *data = dev->data;

	if (flags != COUNTER_GUARD_PERIOD_LATE_TO_SET) {
		return -ENOSYS;
	}
	if (ticks >= data->top_data.ticks) {
		return -EINVAL;
	}

	data->guard_period = ticks;

	return 0;
}

static uint32_t counter_puppy_get_freq(const struct device *dev)
{
	ARG_UNUSED(dev);

	return sys_clock_hw_cycles_per_sec() / COUNTER_PUPPY_PRESCALER_DIV;
}

static DEVICE_API(counter, counter_puppy_api) = {
	.start = counter_puppy_start,
	.stop = counter_puppy_stop,
	.reset = counter_puppy_reset,
	.get_value = counter_puppy_get_value,
	.set_alarm = counter_puppy_set_alarm,
	.cancel_alarm = counter_puppy_cancel_alarm,
	.set_top_value = counter_puppy_set_top_value,
	.get_top_value = counter_puppy_get_top_value,
	.get_pending_int = counter_puppy_get_pending_int,
	.get_freq = counter_puppy_get_freq,
	.get_guard_period = counter_puppy_get_guard_period,
	.set_guard_period = counter_puppy_set_guard_period,
};

static void counter_puppy_isr(void *arg)
{
	const struct device *dev = arg;
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;
	counter_alarm_callback_t alarm_cb = NULL;
	counter_top_callback_t top_cb = NULL;
	void *alarm_user_data = NULL;
	void *top_user_data = NULL;
	uint32_t alarm_target = 0U;
	uint32_t raw;
	uint32_t event_value;
	uint32_t now;
	uint32_t top;
	uint32_t old_base;
	uint32_t old_compare;
	uint8_t events;
	uint64_t value;
	bool event_top;
	bool event_alarm;
	bool crossed_top_after_match;
	bool was_running;

	/*
	 * Puppy raises the compare IRQ when CNT reaches CMP and performs
	 * CMP_CLR on the following FLL clock. In practice interrupt entry is
	 * later than that one-clock reset latency, but handle the tiny window
	 * explicitly as well.
	 *
	 * Stop it as soon as possible, while keeping MATCH/RESET + IRQ and the
	 * prescaler configuration intact.
	 */
	was_running = counter_puppy_freeze(dev);

	top = data->top_data.ticks;
	old_base = data->counter_base;
	old_compare = data->compare_ticks;
	events = data->compare_events;
	raw = cfg->timer->CNT_HI;

	/*
	 * If the ISR is entered in the single FLL-clock interval between the
	 * compare match and CMP_CLR, CNT still equals CMP. No additional
	 * prescaled timer tick has elapsed after the event, so treat latency
	 * as zero. Normally CNT has already been reset and this does nothing.
	 */
	if (raw == old_compare) {
		raw = 0U;
	}

	event_top = (events & PUPPY_EVENT_TOP) != 0U;
	event_alarm = (events & PUPPY_EVENT_ALARM) != 0U;

	/* Logical value exactly at the hardware compare that generated IRQ. */
	value = (uint64_t)old_base + (uint64_t)old_compare;
	if (value >= (uint64_t)top) {
		value -= (uint64_t)top;
	}
	event_value = (uint32_t)value;

	/*
	 * Account for the few hardware ticks that elapsed between MATCH/RESET
	 * and entry into this ISR.  At most one logical TOP can be crossed
	 * here as long as this IRQ is serviced before the same compare repeats.
	 */
	value = (uint64_t)event_value + (uint64_t)raw;
	crossed_top_after_match = value >= (uint64_t)top;
	if (crossed_top_after_match) {
		value -= (uint64_t)top;
	}
	now = (uint32_t)value;

	if (event_alarm) {
		alarm_cb = data->alarm_cfg.callback;
		alarm_user_data = data->alarm_cfg.user_data;
		alarm_target = data->alarm_target;
		counter_puppy_clear_alarm_state(dev);
	}

	/*
	 * A real TOP compare occurred. An alarm waiting for the next period
	 * now belongs to the period that has just started.
	 */
	if (event_top && (data->alarm_cfg.callback != NULL) &&
	    data->alarm_after_top) {
		data->alarm_after_top = false;
	}

	/*
	 * If ISR latency after TOP already passed a just-unblocked alarm
	 * target, do not program a compare behind CNT_HI. The hardware remains
	 * safe, just report the lost alarm and release the channel.
	 */
	if ((data->alarm_cfg.callback != NULL) &&
	    !data->alarm_after_top &&
	    !data->alarm_on_top &&
	    (data->alarm_target <= now) &&
	    (event_top || crossed_top_after_match)) {
		LOG_ERR("Alarm target %u missed while servicing compare "
			"(logical counter=%u)",
			data->alarm_target, now);
		counter_puppy_clear_alarm_state(dev);
	}

	/* 
	 * Capture TOP callback for a real TOP event or a TOP crossed while
	 * cpu was busy.
	 */
	if (event_top || crossed_top_after_match) {
		top_cb = data->top_data.callback;
		top_user_data = data->top_data.user_data;
	}

	/*
	 * Rebase at the logical value reached while entering the ISR. The
	 * physical counter is reset again while stopped, so no elapsed
	 * logical ticks are lost from the value represented by counter_base.
	 */
	counter_puppy_hw_reset(dev);
	data->counter_base = now;

	counter_puppy_schedule_locked(dev);
	counter_puppy_resume(dev, was_running);

	/*
	 * State and the next safe MATCH/RESET boundary are installed before
	 * invoking user callbacks, so callbacks may arm/cancel alarms safely.
	 */
	if (alarm_cb != NULL) {
		alarm_cb(dev, 0U, alarm_target, alarm_user_data);
	}
	if (top_cb != NULL) {
		top_cb(dev, top_user_data);
	}
}

static int counter_puppy_init(const struct device *dev)
{
	const struct counter_puppy_config *cfg = dev->config;
	struct counter_puppy_data *data = dev->data;

	/*
	 * Start stopped, with prescaler + MATCH/RESET + IRQ permanently
	 * configured. PVAL=9 gives 2 MHz from the current 20 MHz timer clock.
	 */
	cfg->timer->CFG_HI = CFG_SAFE_BASE;
	counter_puppy_hw_reset(dev);

	data->alarm_cfg.callback = NULL;
	data->alarm_cfg.user_data = NULL;
	data->alarm_target = 0U;
	data->guard_period = 0U;

	data->counter_base = 0U;
	data->compare_ticks = 0U;
	data->compare_events = 0U;
	data->alarm_after_top = false;
	data->alarm_on_top = false;

	data->top_data.callback = NULL;
	data->top_data.user_data = NULL;
	data->top_data.ticks = cfg->info.max_top_value;

	counter_puppy_schedule_locked(dev);

	cfg->irq_config_func();

	return 0;
}

#define PUPPY_COUNTER_IRQ_CONFIG(idx)                                          \
	static void counter_puppy_irq_config_##idx(void)                       \
	{                                                                      \
		IRQ_CONNECT(DT_INST_IRQN(idx),                                 \
			    0,                                                 \
			    counter_puppy_isr,                                 \
			    DEVICE_DT_INST_GET(idx),                           \
			    0);                                                \
		                                                               \
		irq_enable(DT_INST_IRQN(idx));                                 \
	}

#define PUPPY_COUNTER_INIT(idx)                                                \
	                                                                       \
	PUPPY_COUNTER_IRQ_CONFIG(idx)                                          \
	                                                                       \
	static struct counter_puppy_data counter_data_##idx;                   \
	                                                                       \
	static const struct counter_puppy_config                               \
		counter_config_##idx = {                                       \
	                                                                       \
		.info = {                                                      \
			.freq = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC /           \
				COUNTER_PUPPY_PRESCALER_DIV,                   \
			.max_top_value = COUNTER_PUPPY_MAX_TOP,                \
			.flags = COUNTER_CONFIG_INFO_COUNT_UP,                 \
			.channels = 1,                                         \
		},                                                             \
	                                                                       \
		.timer = (PUPPY_TIMER_Type *)                                  \
			DT_REG_ADDR(DT_INST_PARENT(idx)),                      \
	                                                                       \
		.irq_config_func = counter_puppy_irq_config_##idx,             \
	};                                                                     \
	                                                                       \
	DEVICE_DT_INST_DEFINE(idx,                                             \
			      counter_puppy_init,                              \
			      NULL,                                            \
			      &counter_data_##idx,                             \
			      &counter_config_##idx,                           \
			      PRE_KERNEL_1,                                    \
			      CONFIG_COUNTER_INIT_PRIORITY,                    \
			      &counter_puppy_api);

DT_INST_FOREACH_STATUS_OKAY(PUPPY_COUNTER_INIT);

