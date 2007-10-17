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

void percpu_counter_add(struct percpu_counter *fbc, s32 amount)
{
	long count;
	s32 *pcount;
	int cpu = get_cpu();

	pcount = per_cpu_ptr(fbc->counters, cpu);
	count = *pcount + amount;
	if (count >= FBC_BATCH || count <= -FBC_BATCH) {
		spin_lock(&fbc->lock);
		fbc->count += count;
		*pcount = 0;
		spin_unlock(&fbc->lock);
	} else {
		*pcount = count;
	}
	put_cpu();
}
EXPORT_SYMBOL(percpu_counter_add);

/*
 * Add up all the per-cpu counts, return the result.  This is a more accurate
 * but much slower version of percpu_counter_read_positive()
 */
s64 percpu_counter_sum(struct percpu_counter *fbc)
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
	return ret < 0 ? 0 : ret;
}
EXPORT_SYMBOL(percpu_counter_sum);

void percpu_counter_init(struct percpu_counter *fbc, s64 amount)
{
	spin_lock_init(&fbc->lock);
	fbc->count = amount;
	fbc->counters = alloc_percpu(s32);
#ifdef CONFIG_HOTPLUG_CPU
	mutex_lock(&percpu_counters_lock);
	list_add(&fbc->list, &percpu_counters);
	mutex_unlock(&percpu_counters_lock);
#endif
}
EXPORT_SYMBOL(percpu_counter_init);

void percpu_counter_destroy(struct percpu_counter *fbc)
{
	free_percpu(fbc->counters);
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

		spin_lock(&fbc->lock);
		pcount = per_cpu_ptr(fbc->counters, cpu);
		fbc->count += *pcount;
		*pcount = 0;
		spin_unlock(&fbc->lock);
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
