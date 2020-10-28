// SPDX-License-Identifier: GPL-2.0
/*
 * Fast batching percpu counters.
 */

#include <linux/percpu_counter.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/debugobjects.h>

#ifdef CONFIG_HOTPLUG_CPU
static LIST_HEAD(percpu_counters);
static DEFINE_SPINLOCK(percpu_counters_lock);
#endif

#ifdef CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER

static const struct debug_obj_descr percpu_counter_debug_descr;

static bool percpu_counter_fixup_free(void *addr, enum debug_obj_state state)
{
	struct percpu_counter *fbc = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		percpu_counter_destroy(fbc);
		debug_object_free(fbc, &percpu_counter_debug_descr);
		return true;
	default:
		return false;
	}
}

static const struct debug_obj_descr percpu_counter_debug_descr = {
	.name		= "percpu_counter",
	.fixup_free	= percpu_counter_fixup_free,
};

static inline void debug_percpu_counter_activate(struct percpu_counter *fbc)
{
	debug_object_init(fbc, &percpu_counter_debug_descr);
	debug_object_activate(fbc, &percpu_counter_debug_descr);
}

static inline void debug_percpu_counter_deactivate(struct percpu_counter *fbc)
{
	debug_object_deactivate(fbc, &percpu_counter_debug_descr);
	debug_object_free(fbc, &percpu_counter_debug_descr);
}

#else	/* CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER */
static inline void debug_percpu_counter_activate(struct percpu_counter *fbc)
{ }
static inline void debug_percpu_counter_deactivate(struct percpu_counter *fbc)
{ }
#endif	/* CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER */

void percpu_counter_set(struct percpu_counter *fbc, s64 amount)
{
	int cpu;
	unsigned long flags;

	raw_spin_lock_irqsave(&fbc->lock, flags);
	for_each_possible_cpu(cpu) {
		s32 *pcount = per_cpu_ptr(fbc->counters, cpu);
		*pcount = 0;
	}
	fbc->count = amount;
	raw_spin_unlock_irqrestore(&fbc->lock, flags);
}
EXPORT_SYMBOL(percpu_counter_set);

/**
 * This function is both preempt and irq safe. The former is due to explicit
 * preemption disable. The latter is guaranteed by the fact that the slow path
 * is explicitly protected by an irq-safe spinlock whereas the fast patch uses
 * this_cpu_add which is irq-safe by definition. Hence there is no need muck
 * with irq state before calling this one
 */
void percpu_counter_add_batch(struct percpu_counter *fbc, s64 amount, s32 batch)
{
	s64 count;

	preempt_disable();
	count = __this_cpu_read(*fbc->counters) + amount;
	if (abs(count) >= batch) {
		unsigned long flags;
		raw_spin_lock_irqsave(&fbc->lock, flags);
		fbc->count += count;
		__this_cpu_sub(*fbc->counters, count - amount);
		raw_spin_unlock_irqrestore(&fbc->lock, flags);
	} else {
		this_cpu_add(*fbc->counters, amount);
	}
	preempt_enable();
}
EXPORT_SYMBOL(percpu_counter_add_batch);

/*
 * For percpu_counter with a big batch, the devication of its count could
 * be big, and there is requirement to reduce the deviation, like when the
 * counter's batch could be runtime decreased to get a better accuracy,
 * which can be achieved by running this sync function on each CPU.
 */
void percpu_counter_sync(struct percpu_counter *fbc)
{
	unsigned long flags;
	s64 count;

	raw_spin_lock_irqsave(&fbc->lock, flags);
	count = __this_cpu_read(*fbc->counters);
	fbc->count += count;
	__this_cpu_sub(*fbc->counters, count);
	raw_spin_unlock_irqrestore(&fbc->lock, flags);
}
EXPORT_SYMBOL(percpu_counter_sync);

/*
 * Add up all the per-cpu counts, return the result.  This is a more accurate
 * but much slower version of percpu_counter_read_positive()
 */
s64 __percpu_counter_sum(struct percpu_counter *fbc)
{
	s64 ret;
	int cpu;
	unsigned long flags;

	raw_spin_lock_irqsave(&fbc->lock, flags);
	ret = fbc->count;
	for_each_online_cpu(cpu) {
		s32 *pcount = per_cpu_ptr(fbc->counters, cpu);
		ret += *pcount;
	}
	raw_spin_unlock_irqrestore(&fbc->lock, flags);
	return ret;
}
EXPORT_SYMBOL(__percpu_counter_sum);

int __percpu_counter_init(struct percpu_counter *fbc, s64 amount, gfp_t gfp,
			  struct lock_class_key *key)
{
	unsigned long flags __maybe_unused;

	raw_spin_lock_init(&fbc->lock);
	lockdep_set_class(&fbc->lock, key);
	fbc->count = amount;
	fbc->counters = alloc_percpu_gfp(s32, gfp);
	if (!fbc->counters)
		return -ENOMEM;

	debug_percpu_counter_activate(fbc);

#ifdef CONFIG_HOTPLUG_CPU
	INIT_LIST_HEAD(&fbc->list);
	spin_lock_irqsave(&percpu_counters_lock, flags);
	list_add(&fbc->list, &percpu_counters);
	spin_unlock_irqrestore(&percpu_counters_lock, flags);
#endif
	return 0;
}
EXPORT_SYMBOL(__percpu_counter_init);

void percpu_counter_destroy(struct percpu_counter *fbc)
{
	unsigned long flags __maybe_unused;

	if (!fbc->counters)
		return;

	debug_percpu_counter_deactivate(fbc);

#ifdef CONFIG_HOTPLUG_CPU
	spin_lock_irqsave(&percpu_counters_lock, flags);
	list_del(&fbc->list);
	spin_unlock_irqrestore(&percpu_counters_lock, flags);
#endif
	free_percpu(fbc->counters);
	fbc->counters = NULL;
}
EXPORT_SYMBOL(percpu_counter_destroy);

int percpu_counter_batch __read_mostly = 32;
EXPORT_SYMBOL(percpu_counter_batch);

static int compute_batch_value(unsigned int cpu)
{
	int nr = num_online_cpus();

	percpu_counter_batch = max(32, nr*2);
	return 0;
}

static int percpu_counter_cpu_dead(unsigned int cpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	struct percpu_counter *fbc;

	compute_batch_value(cpu);

	spin_lock_irq(&percpu_counters_lock);
	list_for_each_entry(fbc, &percpu_counters, list) {
		s32 *pcount;

		raw_spin_lock(&fbc->lock);
		pcount = per_cpu_ptr(fbc->counters, cpu);
		fbc->count += *pcount;
		*pcount = 0;
		raw_spin_unlock(&fbc->lock);
	}
	spin_unlock_irq(&percpu_counters_lock);
#endif
	return 0;
}

/*
 * Compare counter against given value.
 * Return 1 if greater, 0 if equal and -1 if less
 */
int __percpu_counter_compare(struct percpu_counter *fbc, s64 rhs, s32 batch)
{
	s64	count;

	count = percpu_counter_read(fbc);
	/* Check to see if rough count will be sufficient for comparison */
	if (abs(count - rhs) > (batch * num_online_cpus())) {
		if (count > rhs)
			return 1;
		else
			return -1;
	}
	/* Need to use precise count */
	count = percpu_counter_sum(fbc);
	if (count > rhs)
		return 1;
	else if (count < rhs)
		return -1;
	else
		return 0;
}
EXPORT_SYMBOL(__percpu_counter_compare);

static int __init percpu_counter_startup(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "lib/percpu_cnt:online",
				compute_batch_value, NULL);
	WARN_ON(ret < 0);
	ret = cpuhp_setup_state_nocalls(CPUHP_PERCPU_CNT_DEAD,
					"lib/percpu_cnt:dead", NULL,
					percpu_counter_cpu_dead);
	WARN_ON(ret < 0);
	return 0;
}
module_init(percpu_counter_startup);
