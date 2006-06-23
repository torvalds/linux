/*
 * Fast batching percpu counters.
 */

#include <linux/percpu_counter.h>
#include <linux/module.h>

void percpu_counter_mod(struct percpu_counter *fbc, long amount)
{
	long count;
	long *pcount;
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
EXPORT_SYMBOL(percpu_counter_mod);

/*
 * Add up all the per-cpu counts, return the result.  This is a more accurate
 * but much slower version of percpu_counter_read_positive()
 */
long percpu_counter_sum(struct percpu_counter *fbc)
{
	long ret;
	int cpu;

	spin_lock(&fbc->lock);
	ret = fbc->count;
	for_each_possible_cpu(cpu) {
		long *pcount = per_cpu_ptr(fbc->counters, cpu);
		ret += *pcount;
	}
	spin_unlock(&fbc->lock);
	return ret < 0 ? 0 : ret;
}
EXPORT_SYMBOL(percpu_counter_sum);
