
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>

static void irq_spread_init_one(struct cpumask *irqmsk, struct cpumask *nmsk,
				int cpus_per_vec)
{
	const struct cpumask *siblmsk;
	int cpu, sibl;

	for ( ; cpus_per_vec > 0; ) {
		cpu = cpumask_first(nmsk);

		/* Should not happen, but I'm too lazy to think about it */
		if (cpu >= nr_cpu_ids)
			return;

		cpumask_clear_cpu(cpu, nmsk);
		cpumask_set_cpu(cpu, irqmsk);
		cpus_per_vec--;

		/* If the cpu has siblings, use them first */
		siblmsk = topology_sibling_cpumask(cpu);
		for (sibl = -1; cpus_per_vec > 0; ) {
			sibl = cpumask_next(sibl, siblmsk);
			if (sibl >= nr_cpu_ids)
				break;
			if (!cpumask_test_and_clear_cpu(sibl, nmsk))
				continue;
			cpumask_set_cpu(sibl, irqmsk);
			cpus_per_vec--;
		}
	}
}

static int get_nodes_in_cpumask(const struct cpumask *mask, nodemask_t *nodemsk)
{
	int n, nodes = 0;

	/* Calculate the number of nodes in the supplied affinity mask */
	for_each_online_node(n) {
		if (cpumask_intersects(mask, cpumask_of_node(n))) {
			node_set(n, *nodemsk);
			nodes++;
		}
	}
	return nodes;
}

/**
 * irq_create_affinity_masks - Create affinity masks for multiqueue spreading
 * @affinity:		The affinity mask to spread. If NULL cpu_online_mask
 *			is used
 * @nvecs:		The number of vectors
 *
 * Returns the masks pointer or NULL if allocation failed.
 */
struct cpumask *irq_create_affinity_masks(const struct cpumask *affinity,
					  int nvec)
{
	int n, nodes, vecs_per_node, cpus_per_vec, extra_vecs, curvec = 0;
	nodemask_t nodemsk = NODE_MASK_NONE;
	struct cpumask *masks;
	cpumask_var_t nmsk;

	if (!zalloc_cpumask_var(&nmsk, GFP_KERNEL))
		return NULL;

	masks = kzalloc(nvec * sizeof(*masks), GFP_KERNEL);
	if (!masks)
		goto out;

	/* Stabilize the cpumasks */
	get_online_cpus();
	/* If the supplied affinity mask is NULL, use cpu online mask */
	if (!affinity)
		affinity = cpu_online_mask;

	nodes = get_nodes_in_cpumask(affinity, &nodemsk);

	/*
	 * If the number of nodes in the mask is greater than or equal the
	 * number of vectors we just spread the vectors across the nodes.
	 */
	if (nvec <= nodes) {
		for_each_node_mask(n, nodemsk) {
			cpumask_copy(masks + curvec, cpumask_of_node(n));
			if (++curvec == nvec)
				break;
		}
		goto outonl;
	}

	/* Spread the vectors per node */
	vecs_per_node = nvec / nodes;
	/* Account for rounding errors */
	extra_vecs = nvec - (nodes * vecs_per_node);

	for_each_node_mask(n, nodemsk) {
		int ncpus, v, vecs_to_assign = vecs_per_node;

		/* Get the cpus on this node which are in the mask */
		cpumask_and(nmsk, affinity, cpumask_of_node(n));

		/* Calculate the number of cpus per vector */
		ncpus = cpumask_weight(nmsk);

		for (v = 0; curvec < nvec && v < vecs_to_assign; curvec++, v++) {
			cpus_per_vec = ncpus / vecs_to_assign;

			/* Account for extra vectors to compensate rounding errors */
			if (extra_vecs) {
				cpus_per_vec++;
				if (!--extra_vecs)
					vecs_per_node++;
			}
			irq_spread_init_one(masks + curvec, nmsk, cpus_per_vec);
		}

		if (curvec >= nvec)
			break;
	}

outonl:
	put_online_cpus();
out:
	free_cpumask_var(nmsk);
	return masks;
}

/**
 * irq_calc_affinity_vectors - Calculate to optimal number of vectors for a given affinity mask
 * @affinity:		The affinity mask to spread. If NULL cpu_online_mask
 *			is used
 * @maxvec:		The maximum number of vectors available
 */
int irq_calc_affinity_vectors(const struct cpumask *affinity, int maxvec)
{
	int cpus, ret;

	/* Stabilize the cpumasks */
	get_online_cpus();
	/* If the supplied affinity mask is NULL, use cpu online mask */
	if (!affinity)
		affinity = cpu_online_mask;

	cpus = cpumask_weight(affinity);
	ret = (cpus < maxvec) ? cpus : maxvec;

	put_online_cpus();
	return ret;
}
