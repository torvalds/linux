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

/*
 * local_irq_save() is needed to make the function irq safe:
 * - The slow path would be ok as protected by an irq-safe spinlock.
 * - this_cpu_add would be ok as it is irq-safe by definition.
 * But:
 * The decision slow path/fast path and the actual update must be atomic, too.
 * Otherwise a call in process context could check the current values and
 * decide that the fast path can be used. If now an interrupt occurs before
 * the this_cpu_add(), and the interrupt updates this_cpu(*fbc->counters),
 * then the this_cpu_add() that is executed after the interrupt has completed
 * can produce values larger than "batch" or even overflows.
 */
void percpu_counter_add_batch(struct percpu_counter *fbc, s64 amount, s32 batch)
{
	s64 count;
	unsigned long flags;

	local_irq_save(flags);
	count = __this_cpu_read(*fbc->counters) + amount;
	if (abs(count) >= batch) {
		raw_spin_lock(&fbc->lock);
		fbc->count += count;
		__this_cpu_sub(*fbc->counters, count - amount);
		raw_spin_unlock(&fbc->lock);
	} else {
		this_cpu_add(*fbc->counters, amount);
	}
	local_irq_restore(flags);
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
 * but much slower version of percpu_counter_read_positive().
 *
 * We use the cpu mask of (cpu_online_mask | cpu_dying_mask) to capture sums
 * from CPUs that are in the process of being taken offline. Dying cpus have
 * been removed from the online mask, but may not have had the hotplug dead
 * notifier called to fold the percpu count back into the global counter sum.
 * By including dying CPUs in the iteration mask, we avoid this race condition
 * so __percpu_counter_sum() just does the right thing when CPUs are being taken
 * offline.
 */
s64 __percpu_counter_sum(struct percpu_counter *fbc)
{
	s64 ret;
	int cpu;
	unsigned long flags;

	raw_spin_lock_irqsave(&fbc->lock, flags);
	ret = fbc->count;
	for_each_cpu_or(cpu, cpu_online_mask, cpu_dying_mask) {
		s32 *pcount = per_cpu_ptr(fbc->counters, cpu);
		ret += *pcount;
	}
	raw_spin_unlock_irqrestore(&fbc->lock, flags);
	return ret;
}
EXPORT_SYMBOL(__percpu_counter_sum);

int __percpu_counter_init_many(struct percpu_counter *fbc, s64 amount,
			       gfp_t gfp, u32 nr_counters,
			       struct lock_class_key *key)
{
	unsigned long flags __maybe_unused;
	size_t counter_size;
	s32 __percpu *counters;
	u32 i;

	counter_size = ALIGN(sizeof(*counters), __alignof__(*counters));
	counters = __alloc_percpu_gfp(nr_counters * counter_size,
				      __alignof__(*counters), gfp);
	if (!counters) {
		fbc[0].counters = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < nr_counters; i++) {
		raw_spin_lock_init(&fbc[i].lock);
		lockdep_set_class(&fbc[i].lock, key);
#ifdef CONFIG_HOTPLUG_CPU
		INIT_LIST_HEAD(&fbc[i].list);
#endif
		fbc[i].count = amount;
		fbc[i].counters = (void *)counters + (i * counter_size);

		debug_percpu_counter_activate(&fbc[i]);
	}

#ifdef CONFIG_HOTPLUG_CPU
	spin_lock_irqsave(&percpu_counters_lock, flags);
	for (i = 0; i < nr_counters; i++)
		list_add(&fbc[i].list, &percpu_counters);
	spin_unlock_irqrestore(&percpu_counters_lock, flags);
#endif
	return 0;
}
EXPORT_SYMBOL(__percpu_counter_init_many);

void percpu_counter_destroy_many(struct percpu_counter *fbc, u32 nr_counters)
{
	unsigned long flags __maybe_unused;
	u32 i;

	if (WARN_ON_ONCE(!fbc))
		return;

	if (!fbc[0].counters)
		return;

	for (i = 0; i < nr_counters; i++)
		debug_percpu_counter_deactivate(&fbc[i]);

#ifdef CONFIG_HOTPLUG_CPU
	spin_lock_irqsave(&percpu_counters_lock, flags);
	for (i = 0; i < nr_counters; i++)
		list_del(&fbc[i].list);
	spin_unlock_irqrestore(&percpu_counters_lock, flags);
#endif

	free_percpu(fbc[0].counters);

	for (i = 0; i < nr_counters; i++)
		fbc[i].counters = NULL;
}
EXPORT_SYMBOL(percpu_counter_destroy_many);

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
