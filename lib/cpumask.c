#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/export.h>
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
/**
 * alloc_cpumask_var_node - allocate a struct cpumask on a given node
 * @mask: pointer to cpumask_var_t where the cpumask is returned
 * @flags: GFP_ flags
 *
 * Only defined when CONFIG_CPUMASK_OFFSTACK=y, otherwise is
 * a nop returning a constant 1 (in <linux/cpumask.h>)
 * Returns TRUE if memory allocation succeeded, FALSE otherwise.
 *
 * In addition, mask will be NULL if this fails.  Note that gcc is
 * usually smart enough to know that mask can never be NULL if
 * CONFIG_CPUMASK_OFFSTACK=n, so does code elimination in that case
 * too.
 */
bool alloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags, int node)
{
	*mask = kmalloc_node(cpumask_size(), flags, node);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (!*mask) {
		printk(KERN_ERR "=> alloc_cpumask_var: failed!\n");
		dump_stack();
	}
#endif
	/* FIXME: Bandaid to save us from old primitives which go to NR_CPUS. */
	if (*mask) {
		unsigned char *ptr = (unsigned char *)cpumask_bits(*mask);
		unsigned int tail;
		tail = BITS_TO_LONGS(NR_CPUS - nr_cpumask_bits) * sizeof(long);
		memset(ptr + cpumask_size() - tail, 0, tail);
	}

	return *mask != NULL;
}
EXPORT_SYMBOL(alloc_cpumask_var_node);

bool zalloc_cpumask_var_node(cpumask_var_t *mask, gfp_t flags, int node)
{
	return alloc_cpumask_var_node(mask, flags | __GFP_ZERO, node);
}
EXPORT_SYMBOL(zalloc_cpumask_var_node);

/**
 * alloc_cpumask_var - allocate a struct cpumask
 * @mask: pointer to cpumask_var_t where the cpumask is returned
 * @flags: GFP_ flags
 *
 * Only defined when CONFIG_CPUMASK_OFFSTACK=y, otherwise is
 * a nop returning a constant 1 (in <linux/cpumask.h>).
 *
 * See alloc_cpumask_var_node.
 */
bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return alloc_cpumask_var_node(mask, flags, NUMA_NO_NODE);
}
EXPORT_SYMBOL(alloc_cpumask_var);

bool zalloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return alloc_cpumask_var(mask, flags | __GFP_ZERO);
}
EXPORT_SYMBOL(zalloc_cpumask_var);

/**
 * alloc_bootmem_cpumask_var - allocate a struct cpumask from the bootmem arena.
 * @mask: pointer to cpumask_var_t where the cpumask is returned
 *
 * Only defined when CONFIG_CPUMASK_OFFSTACK=y, otherwise is
 * a nop (in <linux/cpumask.h>).
 * Either returns an allocated (zero-filled) cpumask, or causes the
 * system to panic.
 */
void __init alloc_bootmem_cpumask_var(cpumask_var_t *mask)
{
	*mask = memblock_virt_alloc(cpumask_size(), 0);
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
	memblock_free_early(__pa(mask), cpumask_size());
}
#endif

/**
 * cpumask_set_cpu_local_first - set i'th cpu with local numa cpu's first
 *
 * @i: index number
 * @numa_node: local numa_node
 * @dstp: cpumask with the relevant cpu bit set according to the policy
 *
 * This function sets the cpumask according to a numa aware policy.
 * cpumask could be used as an affinity hint for the IRQ related to a
 * queue. When the policy is to spread queues across cores - local cores
 * first.
 *
 * Returns 0 on success, -ENOMEM for no memory, and -EAGAIN when failed to set
 * the cpu bit and need to re-call the function.
 */
int cpumask_set_cpu_local_first(int i, int numa_node, cpumask_t *dstp)
{
	cpumask_var_t mask;
	int cpu;
	int ret = 0;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	i %= num_online_cpus();

	if (!cpumask_of_node(numa_node)) {
		/* Use all online cpu's for non numa aware system */
		cpumask_copy(mask, cpu_online_mask);
	} else {
		int n;

		cpumask_and(mask,
			    cpumask_of_node(numa_node), cpu_online_mask);

		n = cpumask_weight(mask);
		if (i >= n) {
			i -= n;

			/* If index > number of local cpu's, mask out local
			 * cpu's
			 */
			cpumask_andnot(mask, cpu_online_mask, mask);
		}
	}

	for_each_cpu(cpu, mask) {
		if (--i < 0)
			goto out;
	}

	ret = -EAGAIN;

out:
	free_cpumask_var(mask);

	if (!ret)
		cpumask_set_cpu(cpu, dstp);

	return ret;
}
EXPORT_SYMBOL(cpumask_set_cpu_local_first);
