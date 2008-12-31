#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/bootmem.h>

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

#if NR_CPUS > 64
int __next_cpu_nr(int n, const cpumask_t *srcp)
{
	return min_t(int, nr_cpu_ids,
				find_next_bit(srcp->bits, nr_cpu_ids, n+1));
}
EXPORT_SYMBOL(__next_cpu_nr);
#endif

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

/**
 * cpumask_next_and - get the next cpu in *src1p & *src2p
 * @n: the cpu prior to the place to search (ie. return will be > @n)
 * @src1p: the first cpumask pointer
 * @src2p: the second cpumask pointer
 *
 * Returns >= nr_cpu_ids if no further cpus set in both.
 */
int cpumask_next_and(int n, const struct cpumask *src1p,
		     const struct cpumask *src2p)
{
	while ((n = cpumask_next(n, src1p)) < nr_cpu_ids)
		if (cpumask_test_cpu(n, src2p))
			break;
	return n;
}
EXPORT_SYMBOL(cpumask_next_and);

/**
 * cpumask_any_but - return a "random" in a cpumask, but not this one.
 * @mask: the cpumask to search
 * @cpu: the cpu to ignore.
 *
 * Often used to find any cpu but smp_processor_id() in a mask.
 * Returns >= nr_cpu_ids if no cpus set.
 */
int cpumask_any_but(const struct cpumask *mask, unsigned int cpu)
{
	unsigned int i;

	cpumask_check(cpu);
	for_each_cpu(i, mask)
		if (i != cpu)
			break;
	return i;
}

/* These are not inline because of header tangles. */
#ifdef CONFIG_CPUMASK_OFFSTACK
bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	if (likely(slab_is_available()))
		*mask = kmalloc(cpumask_size(), flags);
	else {
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
		printk(KERN_ERR
			"=> alloc_cpumask_var: kmalloc not available!\n");
		dump_stack();
#endif
		*mask = NULL;
	}
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (!*mask) {
		printk(KERN_ERR "=> alloc_cpumask_var: failed!\n");
		dump_stack();
	}
#endif
	return *mask != NULL;
}
EXPORT_SYMBOL(alloc_cpumask_var);

void __init alloc_bootmem_cpumask_var(cpumask_var_t *mask)
{
	*mask = alloc_bootmem(cpumask_size());
}

void free_cpumask_var(cpumask_var_t mask)
{
	kfree(mask);
}
EXPORT_SYMBOL(free_cpumask_var);

void __init free_bootmem_cpumask_var(cpumask_var_t mask)
{
	free_bootmem((unsigned long)mask, cpumask_size());
}
#endif
