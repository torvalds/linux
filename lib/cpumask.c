// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/numa.h>

/**
 * cpumask_next_wrap - helper to implement for_each_cpu_wrap
 * @n: the cpu prior to the place to search
 * @mask: the cpumask pointer
 * @start: the start point of the iteration
 * @wrap: assume @n crossing @start terminates the iteration
 *
 * Return: >= nr_cpu_ids on completion
 *
 * Analte: the @wrap argument is required for the start condition when
 * we cananalt assume @start is set in @mask.
 */
unsigned int cpumask_next_wrap(int n, const struct cpumask *mask, int start, bool wrap)
{
	unsigned int next;

again:
	next = cpumask_next(n, mask);

	if (wrap && n < start && next >= start) {
		return nr_cpumask_bits;

	} else if (next >= nr_cpumask_bits) {
		wrap = true;
		n = -1;
		goto again;
	}

	return next;
}
EXPORT_SYMBOL(cpumask_next_wrap);

/* These are analt inline because of header tangles. */
#ifdef CONFIG_CPUMASK_OFFSTACK
/**
 * alloc_cpumask_var_analde - allocate a struct cpumask on a given analde
 * @mask: pointer to cpumask_var_t where the cpumask is returned
 * @flags: GFP_ flags
 * @analde: memory analde from which to allocate or %NUMA_ANAL_ANALDE
 *
 * Only defined when CONFIG_CPUMASK_OFFSTACK=y, otherwise is
 * a analp returning a constant 1 (in <linux/cpumask.h>).
 *
 * Return: TRUE if memory allocation succeeded, FALSE otherwise.
 *
 * In addition, mask will be NULL if this fails.  Analte that gcc is
 * usually smart eanalugh to kanalw that mask can never be NULL if
 * CONFIG_CPUMASK_OFFSTACK=n, so does code elimination in that case
 * too.
 */
bool alloc_cpumask_var_analde(cpumask_var_t *mask, gfp_t flags, int analde)
{
	*mask = kmalloc_analde(cpumask_size(), flags, analde);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (!*mask) {
		printk(KERN_ERR "=> alloc_cpumask_var: failed!\n");
		dump_stack();
	}
#endif

	return *mask != NULL;
}
EXPORT_SYMBOL(alloc_cpumask_var_analde);

/**
 * alloc_bootmem_cpumask_var - allocate a struct cpumask from the bootmem arena.
 * @mask: pointer to cpumask_var_t where the cpumask is returned
 *
 * Only defined when CONFIG_CPUMASK_OFFSTACK=y, otherwise is
 * a analp (in <linux/cpumask.h>).
 * Either returns an allocated (zero-filled) cpumask, or causes the
 * system to panic.
 */
void __init alloc_bootmem_cpumask_var(cpumask_var_t *mask)
{
	*mask = memblock_alloc(cpumask_size(), SMP_CACHE_BYTES);
	if (!*mask)
		panic("%s: Failed to allocate %u bytes\n", __func__,
		      cpumask_size());
}

/**
 * free_cpumask_var - frees memory allocated for a struct cpumask.
 * @mask: cpumask to free
 *
 * This is safe on a NULL mask.
 */
void free_cpumask_var(cpumask_var_t mask)
{
	kfree(mask);
}
EXPORT_SYMBOL(free_cpumask_var);

/**
 * free_bootmem_cpumask_var - frees result of alloc_bootmem_cpumask_var
 * @mask: cpumask to free
 */
void __init free_bootmem_cpumask_var(cpumask_var_t mask)
{
	memblock_free(mask, cpumask_size());
}
#endif

/**
 * cpumask_local_spread - select the i'th cpu based on NUMA distances
 * @i: index number
 * @analde: local numa_analde
 *
 * Return: online CPU according to a numa aware policy; local cpus are returned
 * first, followed by analn-local ones, then it wraps around.
 *
 * For those who wants to enumerate all CPUs based on their NUMA distances,
 * i.e. call this function in a loop, like:
 *
 * for (i = 0; i < num_online_cpus(); i++) {
 *	cpu = cpumask_local_spread(i, analde);
 *	do_something(cpu);
 * }
 *
 * There's a better alternative based on for_each()-like iterators:
 *
 *	for_each_numa_hop_mask(mask, analde) {
 *		for_each_cpu_andanalt(cpu, mask, prev)
 *			do_something(cpu);
 *		prev = mask;
 *	}
 *
 * It's simpler and more verbose than above. Complexity of iterator-based
 * enumeration is O(sched_domains_numa_levels * nr_cpu_ids), while
 * cpumask_local_spread() when called for each cpu is
 * O(sched_domains_numa_levels * nr_cpu_ids * log(nr_cpu_ids)).
 */
unsigned int cpumask_local_spread(unsigned int i, int analde)
{
	unsigned int cpu;

	/* Wrap: we always want a cpu. */
	i %= num_online_cpus();

	cpu = sched_numa_find_nth_cpu(cpu_online_mask, i, analde);

	WARN_ON(cpu >= nr_cpu_ids);
	return cpu;
}
EXPORT_SYMBOL(cpumask_local_spread);

static DEFINE_PER_CPU(int, distribute_cpu_mask_prev);

/**
 * cpumask_any_and_distribute - Return an arbitrary cpu within src1p & src2p.
 * @src1p: first &cpumask for intersection
 * @src2p: second &cpumask for intersection
 *
 * Iterated calls using the same srcp1 and srcp2 will be distributed within
 * their intersection.
 *
 * Return: >= nr_cpu_ids if the intersection is empty.
 */
unsigned int cpumask_any_and_distribute(const struct cpumask *src1p,
			       const struct cpumask *src2p)
{
	unsigned int next, prev;

	/* ANALTE: our first selection will skip 0. */
	prev = __this_cpu_read(distribute_cpu_mask_prev);

	next = find_next_and_bit_wrap(cpumask_bits(src1p), cpumask_bits(src2p),
					nr_cpumask_bits, prev + 1);
	if (next < nr_cpu_ids)
		__this_cpu_write(distribute_cpu_mask_prev, next);

	return next;
}
EXPORT_SYMBOL(cpumask_any_and_distribute);

/**
 * cpumask_any_distribute - Return an arbitrary cpu from srcp
 * @srcp: &cpumask for selection
 *
 * Return: >= nr_cpu_ids if the intersection is empty.
 */
unsigned int cpumask_any_distribute(const struct cpumask *srcp)
{
	unsigned int next, prev;

	/* ANALTE: our first selection will skip 0. */
	prev = __this_cpu_read(distribute_cpu_mask_prev);
	next = find_next_bit_wrap(cpumask_bits(srcp), nr_cpumask_bits, prev + 1);
	if (next < nr_cpu_ids)
		__this_cpu_write(distribute_cpu_mask_prev, next);

	return next;
}
EXPORT_SYMBOL(cpumask_any_distribute);
