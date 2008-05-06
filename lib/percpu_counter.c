/*
 * Fast batching percpu counters.
 */

#include <linux/percpu_counter.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>

#ifdef CONFIG_HOTPLUG_CPU
static LIST_HEAD(percpu_counters);
static DEFINE_MUTEX(percpu_counters_lock);
#endif

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

static struct lock_class_key percpu_counter_irqsafe;

int percpu_counter_init(struct percpu_counter *fbc, s64 amount)
{
	spin_lock_init(&fbc->lock);
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
EXPORT_SYMBOL(percpu_counter_init);

int percpu_counter_init_irq(struct percpu_counter *fbc, s64 amount)
{
	int err;

	err = percpu_counter_init(fbc, amount);
	if (!err)
		lockdep_set_class(&fbc->lock, &percpu_counter_irqsafe);
	return err;
}

void percpu_counter_destroy(struct percpu_counter *fbc)
{
	if (!fbc->counters)
		return;

	free_percpu(fbc->counters);
	fbc->counters = NULL;
#ifdef CONFIG_HOTPLUG_CPU
	mutex_lock(&percpu_counters_lock);
	list_del(&fbc->list);
	mutex_unlock(&percpu_counters_lock);
#endif
}
EXPORT_SYMBOL(percpu_counter_destroy);

#ifdef CONFIG_HOTPLUG_CPU
static int __cpuinit percpu_counter_hotcpu_callback(struct notifier_block *nb,
					unsigned long action, void *hcpu)
{
	unsigned int cpu;
	struct percpu_counter *fbc;

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
	return NOTIFY_OK;
}

static int __init percpu_counter_startup(void)
{
	hotcpu_notifier(percpu_counter_hotcpu_callback, 0);
	return 0;
}
module_init(percpu_counter_startup);
#endif
