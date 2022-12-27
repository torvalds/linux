// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Thomas Gleixner.
 * Copyright (C) 2016-2017 Christoph Hellwig.
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sort.h>

static void grp_spread_init_one(struct cpumask *irqmsk, struct cpumask *nmsk,
				unsigned int cpus_per_grp)
{
	const struct cpumask *siblmsk;
	int cpu, sibl;

	for ( ; cpus_per_grp > 0; ) {
		cpu = cpumask_first(nmsk);

		/* Should not happen, but I'm too lazy to think about it */
		if (cpu >= nr_cpu_ids)
			return;

		cpumask_clear_cpu(cpu, nmsk);
		cpumask_set_cpu(cpu, irqmsk);
		cpus_per_grp--;

		/* If the cpu has siblings, use them first */
		siblmsk = topology_sibling_cpumask(cpu);
		for (sibl = -1; cpus_per_grp > 0; ) {
			sibl = cpumask_next(sibl, siblmsk);
			if (sibl >= nr_cpu_ids)
				break;
			if (!cpumask_test_and_clear_cpu(sibl, nmsk))
				continue;
			cpumask_set_cpu(sibl, irqmsk);
			cpus_per_grp--;
		}
	}
}

static cpumask_var_t *alloc_node_to_cpumask(void)
{
	cpumask_var_t *masks;
	int node;

	masks = kcalloc(nr_node_ids, sizeof(cpumask_var_t), GFP_KERNEL);
	if (!masks)
		return NULL;

	for (node = 0; node < nr_node_ids; node++) {
		if (!zalloc_cpumask_var(&masks[node], GFP_KERNEL))
			goto out_unwind;
	}

	return masks;

out_unwind:
	while (--node >= 0)
		free_cpumask_var(masks[node]);
	kfree(masks);
	return NULL;
}

static void free_node_to_cpumask(cpumask_var_t *masks)
{
	int node;

	for (node = 0; node < nr_node_ids; node++)
		free_cpumask_var(masks[node]);
	kfree(masks);
}

static void build_node_to_cpumask(cpumask_var_t *masks)
{
	int cpu;

	for_each_possible_cpu(cpu)
		cpumask_set_cpu(cpu, masks[cpu_to_node(cpu)]);
}

static int get_nodes_in_cpumask(cpumask_var_t *node_to_cpumask,
				const struct cpumask *mask, nodemask_t *nodemsk)
{
	int n, nodes = 0;

	/* Calculate the number of nodes in the supplied affinity mask */
	for_each_node(n) {
		if (cpumask_intersects(mask, node_to_cpumask[n])) {
			node_set(n, *nodemsk);
			nodes++;
		}
	}
	return nodes;
}

struct node_groups {
	unsigned id;

	union {
		unsigned ngroups;
		unsigned ncpus;
	};
};

static int ncpus_cmp_func(const void *l, const void *r)
{
	const struct node_groups *ln = l;
	const struct node_groups *rn = r;

	return ln->ncpus - rn->ncpus;
}

/*
 * Allocate group number for each node, so that for each node:
 *
 * 1) the allocated number is >= 1
 *
 * 2) the allocated number is <= active CPU number of this node
 *
 * The actual allocated total groups may be less than @numgrps when
 * active total CPU number is less than @numgrps.
 *
 * Active CPUs means the CPUs in '@cpu_mask AND @node_to_cpumask[]'
 * for each node.
 */
static void alloc_nodes_groups(unsigned int numgrps,
			       cpumask_var_t *node_to_cpumask,
			       const struct cpumask *cpu_mask,
			       const nodemask_t nodemsk,
			       struct cpumask *nmsk,
			       struct node_groups *node_groups)
{
	unsigned n, remaining_ncpus = 0;

	for (n = 0; n < nr_node_ids; n++) {
		node_groups[n].id = n;
		node_groups[n].ncpus = UINT_MAX;
	}

	for_each_node_mask(n, nodemsk) {
		unsigned ncpus;

		cpumask_and(nmsk, cpu_mask, node_to_cpumask[n]);
		ncpus = cpumask_weight(nmsk);

		if (!ncpus)
			continue;
		remaining_ncpus += ncpus;
		node_groups[n].ncpus = ncpus;
	}

	numgrps = min_t(unsigned, remaining_ncpus, numgrps);

	sort(node_groups, nr_node_ids, sizeof(node_groups[0]),
	     ncpus_cmp_func, NULL);

	/*
	 * Allocate groups for each node according to the ratio of this
	 * node's nr_cpus to remaining un-assigned ncpus. 'numgrps' is
	 * bigger than number of active numa nodes. Always start the
	 * allocation from the node with minimized nr_cpus.
	 *
	 * This way guarantees that each active node gets allocated at
	 * least one group, and the theory is simple: over-allocation
	 * is only done when this node is assigned by one group, so
	 * other nodes will be allocated >= 1 groups, since 'numgrps' is
	 * bigger than number of numa nodes.
	 *
	 * One perfect invariant is that number of allocated groups for
	 * each node is <= CPU count of this node:
	 *
	 * 1) suppose there are two nodes: A and B
	 * 	ncpu(X) is CPU count of node X
	 * 	grps(X) is the group count allocated to node X via this
	 * 	algorithm
	 *
	 * 	ncpu(A) <= ncpu(B)
	 * 	ncpu(A) + ncpu(B) = N
	 * 	grps(A) + grps(B) = G
	 *
	 * 	grps(A) = max(1, round_down(G * ncpu(A) / N))
	 * 	grps(B) = G - grps(A)
	 *
	 * 	both N and G are integer, and 2 <= G <= N, suppose
	 * 	G = N - delta, and 0 <= delta <= N - 2
	 *
	 * 2) obviously grps(A) <= ncpu(A) because:
	 *
	 * 	if grps(A) is 1, then grps(A) <= ncpu(A) given
	 * 	ncpu(A) >= 1
	 *
	 * 	otherwise,
	 * 		grps(A) <= G * ncpu(A) / N <= ncpu(A), given G <= N
	 *
	 * 3) prove how grps(B) <= ncpu(B):
	 *
	 * 	if round_down(G * ncpu(A) / N) == 0, vecs(B) won't be
	 * 	over-allocated, so grps(B) <= ncpu(B),
	 *
	 * 	otherwise:
	 *
	 * 	grps(A) =
	 * 		round_down(G * ncpu(A) / N) =
	 * 		round_down((N - delta) * ncpu(A) / N) =
	 * 		round_down((N * ncpu(A) - delta * ncpu(A)) / N)	 >=
	 * 		round_down((N * ncpu(A) - delta * N) / N)	 =
	 * 		cpu(A) - delta
	 *
	 * 	then:
	 *
	 * 	grps(A) - G >= ncpu(A) - delta - G
	 * 	=>
	 * 	G - grps(A) <= G + delta - ncpu(A)
	 * 	=>
	 * 	grps(B) <= N - ncpu(A)
	 * 	=>
	 * 	grps(B) <= cpu(B)
	 *
	 * For nodes >= 3, it can be thought as one node and another big
	 * node given that is exactly what this algorithm is implemented,
	 * and we always re-calculate 'remaining_ncpus' & 'numgrps', and
	 * finally for each node X: grps(X) <= ncpu(X).
	 *
	 */
	for (n = 0; n < nr_node_ids; n++) {
		unsigned ngroups, ncpus;

		if (node_groups[n].ncpus == UINT_MAX)
			continue;

		WARN_ON_ONCE(numgrps == 0);

		ncpus = node_groups[n].ncpus;
		ngroups = max_t(unsigned, 1,
				 numgrps * ncpus / remaining_ncpus);
		WARN_ON_ONCE(ngroups > ncpus);

		node_groups[n].ngroups = ngroups;

		remaining_ncpus -= ncpus;
		numgrps -= ngroups;
	}
}

static int __group_cpus_evenly(unsigned int startgrp, unsigned int numgrps,
			       cpumask_var_t *node_to_cpumask,
			       const struct cpumask *cpu_mask,
			       struct cpumask *nmsk, struct cpumask *masks)
{
	unsigned int i, n, nodes, cpus_per_grp, extra_grps, done = 0;
	unsigned int last_grp = numgrps;
	unsigned int curgrp = startgrp;
	nodemask_t nodemsk = NODE_MASK_NONE;
	struct node_groups *node_groups;

	if (cpumask_empty(cpu_mask))
		return 0;

	nodes = get_nodes_in_cpumask(node_to_cpumask, cpu_mask, &nodemsk);

	/*
	 * If the number of nodes in the mask is greater than or equal the
	 * number of groups we just spread the groups across the nodes.
	 */
	if (numgrps <= nodes) {
		for_each_node_mask(n, nodemsk) {
			/* Ensure that only CPUs which are in both masks are set */
			cpumask_and(nmsk, cpu_mask, node_to_cpumask[n]);
			cpumask_or(&masks[curgrp], &masks[curgrp], nmsk);
			if (++curgrp == last_grp)
				curgrp = 0;
		}
		return numgrps;
	}

	node_groups = kcalloc(nr_node_ids,
			       sizeof(struct node_groups),
			       GFP_KERNEL);
	if (!node_groups)
		return -ENOMEM;

	/* allocate group number for each node */
	alloc_nodes_groups(numgrps, node_to_cpumask, cpu_mask,
			   nodemsk, nmsk, node_groups);
	for (i = 0; i < nr_node_ids; i++) {
		unsigned int ncpus, v;
		struct node_groups *nv = &node_groups[i];

		if (nv->ngroups == UINT_MAX)
			continue;

		/* Get the cpus on this node which are in the mask */
		cpumask_and(nmsk, cpu_mask, node_to_cpumask[nv->id]);
		ncpus = cpumask_weight(nmsk);
		if (!ncpus)
			continue;

		WARN_ON_ONCE(nv->ngroups > ncpus);

		/* Account for rounding errors */
		extra_grps = ncpus - nv->ngroups * (ncpus / nv->ngroups);

		/* Spread allocated groups on CPUs of the current node */
		for (v = 0; v < nv->ngroups; v++, curgrp++) {
			cpus_per_grp = ncpus / nv->ngroups;

			/* Account for extra groups to compensate rounding errors */
			if (extra_grps) {
				cpus_per_grp++;
				--extra_grps;
			}

			/*
			 * wrapping has to be considered given 'startgrp'
			 * may start anywhere
			 */
			if (curgrp >= last_grp)
				curgrp = 0;
			grp_spread_init_one(&masks[curgrp], nmsk,
						cpus_per_grp);
		}
		done += nv->ngroups;
	}
	kfree(node_groups);
	return done;
}

/*
 * build affinity in two stages for each group, and try to put close CPUs
 * in viewpoint of CPU and NUMA locality into same group, and we run
 * two-stage grouping:
 *
 *	1) allocate present CPUs on these groups evenly first
 *	2) allocate other possible CPUs on these groups evenly
 */
static struct cpumask *group_cpus_evenly(unsigned int numgrps)
{
	unsigned int curgrp = 0, nr_present = 0, nr_others = 0;
	cpumask_var_t *node_to_cpumask;
	cpumask_var_t nmsk, npresmsk;
	int ret = -ENOMEM;
	struct cpumask *masks = NULL;

	if (!zalloc_cpumask_var(&nmsk, GFP_KERNEL))
		return NULL;

	if (!zalloc_cpumask_var(&npresmsk, GFP_KERNEL))
		goto fail_nmsk;

	node_to_cpumask = alloc_node_to_cpumask();
	if (!node_to_cpumask)
		goto fail_npresmsk;

	masks = kcalloc(numgrps, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		goto fail_node_to_cpumask;

	/* Stabilize the cpumasks */
	cpus_read_lock();
	build_node_to_cpumask(node_to_cpumask);

	/* grouping present CPUs first */
	ret = __group_cpus_evenly(curgrp, numgrps, node_to_cpumask,
				  cpu_present_mask, nmsk, masks);
	if (ret < 0)
		goto fail_build_affinity;
	nr_present = ret;

	/*
	 * Allocate non present CPUs starting from the next group to be
	 * handled. If the grouping of present CPUs already exhausted the
	 * group space, assign the non present CPUs to the already
	 * allocated out groups.
	 */
	if (nr_present >= numgrps)
		curgrp = 0;
	else
		curgrp = nr_present;
	cpumask_andnot(npresmsk, cpu_possible_mask, cpu_present_mask);
	ret = __group_cpus_evenly(curgrp, numgrps, node_to_cpumask,
				  npresmsk, nmsk, masks);
	if (ret >= 0)
		nr_others = ret;

 fail_build_affinity:
	cpus_read_unlock();

	if (ret >= 0)
		WARN_ON(nr_present + nr_others < numgrps);

 fail_node_to_cpumask:
	free_node_to_cpumask(node_to_cpumask);

 fail_npresmsk:
	free_cpumask_var(npresmsk);

 fail_nmsk:
	free_cpumask_var(nmsk);
	if (ret < 0) {
		kfree(masks);
		return NULL;
	}
	return masks;
}

static void default_calc_sets(struct irq_affinity *affd, unsigned int affvecs)
{
	affd->nr_sets = 1;
	affd->set_size[0] = affvecs;
}

/**
 * irq_create_affinity_masks - Create affinity masks for multiqueue spreading
 * @nvecs:	The total number of vectors
 * @affd:	Description of the affinity requirements
 *
 * Returns the irq_affinity_desc pointer or NULL if allocation failed.
 */
struct irq_affinity_desc *
irq_create_affinity_masks(unsigned int nvecs, struct irq_affinity *affd)
{
	unsigned int affvecs, curvec, usedvecs, i;
	struct irq_affinity_desc *masks = NULL;

	/*
	 * Determine the number of vectors which need interrupt affinities
	 * assigned. If the pre/post request exhausts the available vectors
	 * then nothing to do here except for invoking the calc_sets()
	 * callback so the device driver can adjust to the situation.
	 */
	if (nvecs > affd->pre_vectors + affd->post_vectors)
		affvecs = nvecs - affd->pre_vectors - affd->post_vectors;
	else
		affvecs = 0;

	/*
	 * Simple invocations do not provide a calc_sets() callback. Install
	 * the generic one.
	 */
	if (!affd->calc_sets)
		affd->calc_sets = default_calc_sets;

	/* Recalculate the sets */
	affd->calc_sets(affd, affvecs);

	if (WARN_ON_ONCE(affd->nr_sets > IRQ_AFFINITY_MAX_SETS))
		return NULL;

	/* Nothing to assign? */
	if (!affvecs)
		return NULL;

	masks = kcalloc(nvecs, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		return NULL;

	/* Fill out vectors at the beginning that don't need affinity */
	for (curvec = 0; curvec < affd->pre_vectors; curvec++)
		cpumask_copy(&masks[curvec].mask, irq_default_affinity);

	/*
	 * Spread on present CPUs starting from affd->pre_vectors. If we
	 * have multiple sets, build each sets affinity mask separately.
	 */
	for (i = 0, usedvecs = 0; i < affd->nr_sets; i++) {
		unsigned int this_vecs = affd->set_size[i];
		int j;
		struct cpumask *result = group_cpus_evenly(this_vecs);

		if (!result) {
			kfree(masks);
			return NULL;
		}

		for (j = 0; j < this_vecs; j++)
			cpumask_copy(&masks[curvec + j].mask, &result[j]);
		kfree(result);

		curvec += this_vecs;
		usedvecs += this_vecs;
	}

	/* Fill out vectors at the end that don't need affinity */
	if (usedvecs >= affvecs)
		curvec = affd->pre_vectors + affvecs;
	else
		curvec = affd->pre_vectors + usedvecs;
	for (; curvec < nvecs; curvec++)
		cpumask_copy(&masks[curvec].mask, irq_default_affinity);

	/* Mark the managed interrupts */
	for (i = affd->pre_vectors; i < nvecs - affd->post_vectors; i++)
		masks[i].is_managed = 1;

	return masks;
}

/**
 * irq_calc_affinity_vectors - Calculate the optimal number of vectors
 * @minvec:	The minimum number of vectors available
 * @maxvec:	The maximum number of vectors available
 * @affd:	Description of the affinity requirements
 */
unsigned int irq_calc_affinity_vectors(unsigned int minvec, unsigned int maxvec,
				       const struct irq_affinity *affd)
{
	unsigned int resv = affd->pre_vectors + affd->post_vectors;
	unsigned int set_vecs;

	if (resv > minvec)
		return 0;

	if (affd->calc_sets) {
		set_vecs = maxvec - resv;
	} else {
		cpus_read_lock();
		set_vecs = cpumask_weight(cpu_possible_mask);
		cpus_read_unlock();
	}

	return resv + min(set_vecs, maxvec - resv);
}
