#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/module.h>

int __first_cpu(const cpumask_t *srcp)
{
	return min_t(int, NR_CPUS, find_first_bit(srcp->bits, NR_CPUS));
}
EXPORT_SYMBOL(__first_cpu);

int __next_cpu(int n, const cpumask_t *srcp)
{
	return min_t(int, NR_CPUS, find_next_bit(srcp->bits, NR_CPUS, n+1));
}
EXPORT_SYMBOL(__next_cpu);

/*
 * Find the highest possible smp_processor_id()
 *
 * Note: if we're prepared to assume that cpu_possible_map never changes
 * (reasonable) then this function should cache its return value.
 */
int highest_possible_processor_id(void)
{
	unsigned int cpu;
	unsigned highest = 0;

	for_each_cpu_mask(cpu, cpu_possible_map)
		highest = cpu;
	return highest;
}
EXPORT_SYMBOL(highest_possible_processor_id);

int __any_online_cpu(const cpumask_t *mask)
{
	int cpu;

	for_each_cpu_mask(cpu, *mask) {
		if (cpu_online(cpu))
			break;
	}
	return cpu;
}
EXPORT_SYMBOL(__any_online_cpu);
