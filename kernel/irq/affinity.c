
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
 * @nvecs:	The total number of vectors
 * @affd:	Description of the affinity requirements
 *
 * Returns the masks pointer or NULL if allocation failed.
 */
struct cpumask *
irq_create_affinity_masks(int nvecs, const struct irq_affinity *affd)
{
	int n, nodes, cpus_per_vec, extra_vecs, curvec;
	int affv = nvecs - affd->pre_vectors - affd->post_vectors;
	int last_affv = affv + affd->pre_vectors;
	nodemask_t nodemsk = NODE_MASK_NONE;
	struct cpumask *masks;
	cpumask_var_t nmsk;

	if (!zalloc_cpumask_var(&nmsk, GFP_KERNEL))
		return NULL;

	masks = kcalloc(nvecs, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		goto out;

	/* Fill out vectors at the beginning that don't need affinity */
	for (curvec = 0; curvec < affd->pre_vectors; curvec++)
		cpumask_copy(masks + curvec, irq_default_affinity);

	/* Stabilize the cpumasks */
	get_online_cpus();
	nodes = get_nodes_in_cpumask(cpu_online_mask, &nodemsk);

	/*
	 * If the number of nodes in the mask is greater than or equal the
	 * number of vectors we just spread the vectors across the nodes.
	 */
	if (affv <= nodes) {
		for_each_node_mask(n, nodemsk) {
			cpumask_copy(masks + curvec, cpumask_of_node(n));
			if (++curvec == last_affv)
				break;
		}
		goto done;
	}

	for_each_node_mask(n, nodemsk) {
		int ncpus, v, vecs_to_assign, vecs_per_node;

		/* Spread the vectors per node */
		vecs_per_node = (affv - (curvec - affd->pre_vectors)) / nodes;

		/* Get the cpus on this node which are in the mask */
		cpumask_and(nmsk, cpu_online_mask, cpumask_of_node(n));

		/* Calculate the number of cpus per vector */
		ncpus = cpumask_weight(nmsk);
		vecs_to_assign = min(vecs_per_node, ncpus);

		/* Account for rounding errors */
		extra_vecs = ncpus - vecs_to_assign * (ncpus / vecs_to_assign);

		for (v = 0; curvec < last_affv && v < vecs_to_assign;
		     curvec++, v++) {
			cpus_per_vec = ncpus / vecs_to_assign;

			/* Account for extra vectors to compensate rounding errors */
			if (extra_vecs) {
				cpus_per_vec++;
				--extra_vecs;
			}
			irq_spread_init_one(masks + curvec, nmsk, cpus_per_vec);
		}

		if (curvec >= last_affv)
			break;
		--nodes;
	}

done:
	put_online_cpus();

	/* Fill out vectors at the end that don't need affinity */
	for (; curvec < nvecs; curvec++)
		cpumask_copy(masks + curvec, irq_default_affinity);
out:
	free_cpumask_var(nmsk);
	return masks;
}

/**
 * irq_calc_affinity_vectors - Calculate the optimal number of vectors
 * @maxvec:	The maximum number of vectors available
 * @affd:	Description of the affinity requirements
 */
int irq_calc_affinity_vectors(int maxvec, const struct irq_affinity *affd)
{
	int resv = affd->pre_vectors + affd->post_vectors;
	int vecs = maxvec - resv;
	int cpus;

	/* Stabilize the cpumasks */
	get_online_cpus();
	cpus = cpumask_weight(cpu_online_mask);
	put_online_cpus();

	return min(cpus, vecs) + resv;
}
