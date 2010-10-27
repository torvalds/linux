/*
 * Fast batching percpu counters.
 */

#include <linux/percpu_counter.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>

static LIST_HEAD(percpu_counters);
static DEFINE_MUTEX(percpu_counters_lock);

void percpu_counter_set(struct percpu_counter *fbc, s64 amount)
{
	int cpu;

	spin_lock(&fbc->lock);
	for_each_possible_cpu(cpu) {
		s32 *pcount = per_cpu_ptr(fbc->counters, cpu);
		*pcount = 0;
	}
	fbc->count = amount;
	spin_unlock(&fbc->lock);
}
EXPORT_SYMBOL(percpu_counter_set);

void __percpu_counter_add(struct percpu_counter *fbc, s64 amount, s32 batch)
{
	s64 count;
	s32 *pcount;
	int cpu = get_cpu();

	pcount = per_cpu_ptr(fbc->counters, cpu);
	count = *pcount + amount;
	if (count >= batch || count <= -batch) {
		spin_lock(&fbc->lock);
		fbc->count += count;
		*pcount = 0;
		spin_unlock(&fbc->lock);
	} else {
		*pcount = count;
	}
	put_cpu();
}
EXPORT_SYMBOL(__percpu_counter_add);

/*
 * Add up all the per-cpu counts, return the result.  This is a more accurate
 * but much slower version of percpu_counter_read_positive()
 */
s64 __percpu_counter_sum(struct percpu_counter *fbc)
{
	s64 ret;
	int cpu;

	spin_lock(&fbc->lock);
	ret = fbc->count;
	for_each_online_cpu(cpu) {
		s32 *pcount = per_cpu_ptr(fbc->counters, cpu);
		ret += *pcount;
	}
	spin_unlock(&fbc->lock);
	return ret;
}
EXPORT_SYMBOL(__percpu_counter_sum);

int __percpu_counter_init(struct percpu_counter *fbc, s64 amount,
			  struct lock_class_key *key)
{
	spin_lock_init(&fbc->lock);
	lockdep_set_class(&fbc->lock, key);
	fbc->count = amount;
	fbc->counters = alloc_percpu(s32);
	if (!fbc->counters)
		return -ENOMEM;
#ifdef CONFIG_HOTPLUG_CPU
	mutex_lock(&percpu_counters_lock);
	list_add(&fbc->list, &percpu_counters);
	mutex_unlock(&percpu_counters_lock);
#endif
	return 0;
}
EXPORT_SYMBOL(__percpu_counter_init);

void percpu_counter_destroy(struct percpu_counter *fbc)
{
	if (!fbc->counters)
		return;

#ifdef CONFIG_HOTPLUG_CPU
	mutex_lock(&percpu_counters_lock);
	list_del(&fbc->list);
	mutex_unlock(&percpu_counters_lock);
#endif
	free_percpu(fbc->counters);
	fbc->counters = NULL;
}
EXPORT_SYMBOL(percpu_counter_destroy);

int percpu_counter_batch __read_mostly = 32;
EXPORT_SYMBOL(percpu_counter_batch);

static void compute_batch_value(void)
{
	int nr = num_online_cpus();

	percpu_counter_batch = max(32, nr*2);
}

static int __cpuinit percpu_counter_hotcpu_callback(struct notifier_block *nb,
					unsigned long action, void *hcpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	unsigned int cpu;
	struct percpu_counter *fbc;

	compute_batch_value();
	if (action != CPU_DEAD)
		return NOTIFY_OK;

	cpu = (unsigned long)hcpu;
	mutex_lock(&percpu_counters_lock);
	list_for_each_entry(fbc, &percpu_counters, list) {
		s32 *pcount;
		unsigned long flags;

		spin_lock_irqsave(&fbc->lock, flags);
		pcount = per_cpu_ptr(fbc->counters, cpu);
		fbc->count += *pcount;
		*pcount = 0;
		spin_unlock_irqrestore(&fbc->lock, flags);
	}
	mutex_unlock(&percpu_counters_lock);
#endif
	return NOTIFY_OK;
}

/*
 * Compare counter against given value.
 * Return 1 if greater, 0 if equal and -1 if less
 */
int percpu_counter_compare(struct percpu_counter *fbc, s64 rhs)
{
	s64	count;

	count = percpu_counter_read(fbc);
	/* Check to see if rough count will be sufficient for comparison */
	if (abs(count - rhs) > (percpu_counter_batch*num_online_cpus())) {
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
EXPORT_SYMBOL(percpu_counter_compare);

static int __init percpu_counter_startup(void)
{
	compute_batch_value();
	hotcpu_notifier(percpu_counter_hotcpu_callback, 0);
	return 0;
}
module_init(percpu_counter_startup);
