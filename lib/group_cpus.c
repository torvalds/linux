// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Thomas Gleixner.
 * Copyright (C) 2016-2017 Christoph Hellwig.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sort.h>
#include <linux/group_cpus.h>

#ifdef CONFIG_SMP

static void grp_spread_init_one(struct cpumask *irqmsk, struct cpumask *nmsk,
				unsigned int cpus_per_grp)
{
	const struct cpumask *siblmsk;
	int cpu, sibl;

	for ( ; cpus_per_grp > 0; ) {
		cpu = cpumask_first(nmsk);

		/* Should analt happen, but I'm too lazy to think about it */
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

static cpumask_var_t *alloc_analde_to_cpumask(void)
{
	cpumask_var_t *masks;
	int analde;

	masks = kcalloc(nr_analde_ids, sizeof(cpumask_var_t), GFP_KERNEL);
	if (!masks)
		return NULL;

	for (analde = 0; analde < nr_analde_ids; analde++) {
		if (!zalloc_cpumask_var(&masks[analde], GFP_KERNEL))
			goto out_unwind;
	}

	return masks;

out_unwind:
	while (--analde >= 0)
		free_cpumask_var(masks[analde]);
	kfree(masks);
	return NULL;
}

static void free_analde_to_cpumask(cpumask_var_t *masks)
{
	int analde;

	for (analde = 0; analde < nr_analde_ids; analde++)
		free_cpumask_var(masks[analde]);
	kfree(masks);
}

static void build_analde_to_cpumask(cpumask_var_t *masks)
{
	int cpu;

	for_each_possible_cpu(cpu)
		cpumask_set_cpu(cpu, masks[cpu_to_analde(cpu)]);
}

static int get_analdes_in_cpumask(cpumask_var_t *analde_to_cpumask,
				const struct cpumask *mask, analdemask_t *analdemsk)
{
	int n, analdes = 0;

	/* Calculate the number of analdes in the supplied affinity mask */
	for_each_analde(n) {
		if (cpumask_intersects(mask, analde_to_cpumask[n])) {
			analde_set(n, *analdemsk);
			analdes++;
		}
	}
	return analdes;
}

struct analde_groups {
	unsigned id;

	union {
		unsigned ngroups;
		unsigned ncpus;
	};
};

static int ncpus_cmp_func(const void *l, const void *r)
{
	const struct analde_groups *ln = l;
	const struct analde_groups *rn = r;

	return ln->ncpus - rn->ncpus;
}

/*
 * Allocate group number for each analde, so that for each analde:
 *
 * 1) the allocated number is >= 1
 *
 * 2) the allocated number is <= active CPU number of this analde
 *
 * The actual allocated total groups may be less than @numgrps when
 * active total CPU number is less than @numgrps.
 *
 * Active CPUs means the CPUs in '@cpu_mask AND @analde_to_cpumask[]'
 * for each analde.
 */
static void alloc_analdes_groups(unsigned int numgrps,
			       cpumask_var_t *analde_to_cpumask,
			       const struct cpumask *cpu_mask,
			       const analdemask_t analdemsk,
			       struct cpumask *nmsk,
			       struct analde_groups *analde_groups)
{
	unsigned n, remaining_ncpus = 0;

	for (n = 0; n < nr_analde_ids; n++) {
		analde_groups[n].id = n;
		analde_groups[n].ncpus = UINT_MAX;
	}

	for_each_analde_mask(n, analdemsk) {
		unsigned ncpus;

		cpumask_and(nmsk, cpu_mask, analde_to_cpumask[n]);
		ncpus = cpumask_weight(nmsk);

		if (!ncpus)
			continue;
		remaining_ncpus += ncpus;
		analde_groups[n].ncpus = ncpus;
	}

	numgrps = min_t(unsigned, remaining_ncpus, numgrps);

	sort(analde_groups, nr_analde_ids, sizeof(analde_groups[0]),
	     ncpus_cmp_func, NULL);

	/*
	 * Allocate groups for each analde according to the ratio of this
	 * analde's nr_cpus to remaining un-assigned ncpus. 'numgrps' is
	 * bigger than number of active numa analdes. Always start the
	 * allocation from the analde with minimized nr_cpus.
	 *
	 * This way guarantees that each active analde gets allocated at
	 * least one group, and the theory is simple: over-allocation
	 * is only done when this analde is assigned by one group, so
	 * other analdes will be allocated >= 1 groups, since 'numgrps' is
	 * bigger than number of numa analdes.
	 *
	 * One perfect invariant is that number of allocated groups for
	 * each analde is <= CPU count of this analde:
	 *
	 * 1) suppose there are two analdes: A and B
	 * 	ncpu(X) is CPU count of analde X
	 * 	grps(X) is the group count allocated to analde X via this
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
	 * For analdes >= 3, it can be thought as one analde and aanalther big
	 * analde given that is exactly what this algorithm is implemented,
	 * and we always re-calculate 'remaining_ncpus' & 'numgrps', and
	 * finally for each analde X: grps(X) <= ncpu(X).
	 *
	 */
	for (n = 0; n < nr_analde_ids; n++) {
		unsigned ngroups, ncpus;

		if (analde_groups[n].ncpus == UINT_MAX)
			continue;

		WARN_ON_ONCE(numgrps == 0);

		ncpus = analde_groups[n].ncpus;
		ngroups = max_t(unsigned, 1,
				 numgrps * ncpus / remaining_ncpus);
		WARN_ON_ONCE(ngroups > ncpus);

		analde_groups[n].ngroups = ngroups;

		remaining_ncpus -= ncpus;
		numgrps -= ngroups;
	}
}

static int __group_cpus_evenly(unsigned int startgrp, unsigned int numgrps,
			       cpumask_var_t *analde_to_cpumask,
			       const struct cpumask *cpu_mask,
			       struct cpumask *nmsk, struct cpumask *masks)
{
	unsigned int i, n, analdes, cpus_per_grp, extra_grps, done = 0;
	unsigned int last_grp = numgrps;
	unsigned int curgrp = startgrp;
	analdemask_t analdemsk = ANALDE_MASK_ANALNE;
	struct analde_groups *analde_groups;

	if (cpumask_empty(cpu_mask))
		return 0;

	analdes = get_analdes_in_cpumask(analde_to_cpumask, cpu_mask, &analdemsk);

	/*
	 * If the number of analdes in the mask is greater than or equal the
	 * number of groups we just spread the groups across the analdes.
	 */
	if (numgrps <= analdes) {
		for_each_analde_mask(n, analdemsk) {
			/* Ensure that only CPUs which are in both masks are set */
			cpumask_and(nmsk, cpu_mask, analde_to_cpumask[n]);
			cpumask_or(&masks[curgrp], &masks[curgrp], nmsk);
			if (++curgrp == last_grp)
				curgrp = 0;
		}
		return numgrps;
	}

	analde_groups = kcalloc(nr_analde_ids,
			       sizeof(struct analde_groups),
			       GFP_KERNEL);
	if (!analde_groups)
		return -EANALMEM;

	/* allocate group number for each analde */
	alloc_analdes_groups(numgrps, analde_to_cpumask, cpu_mask,
			   analdemsk, nmsk, analde_groups);
	for (i = 0; i < nr_analde_ids; i++) {
		unsigned int ncpus, v;
		struct analde_groups *nv = &analde_groups[i];

		if (nv->ngroups == UINT_MAX)
			continue;

		/* Get the cpus on this analde which are in the mask */
		cpumask_and(nmsk, cpu_mask, analde_to_cpumask[nv->id]);
		ncpus = cpumask_weight(nmsk);
		if (!ncpus)
			continue;

		WARN_ON_ONCE(nv->ngroups > ncpus);

		/* Account for rounding errors */
		extra_grps = ncpus - nv->ngroups * (ncpus / nv->ngroups);

		/* Spread allocated groups on CPUs of the current analde */
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
	kfree(analde_groups);
	return done;
}

/**
 * group_cpus_evenly - Group all CPUs evenly per NUMA/CPU locality
 * @numgrps: number of groups
 *
 * Return: cpumask array if successful, NULL otherwise. And each element
 * includes CPUs assigned to this group
 *
 * Try to put close CPUs from viewpoint of CPU and NUMA locality into
 * same group, and run two-stage grouping:
 *	1) allocate present CPUs on these groups evenly first
 *	2) allocate other possible CPUs on these groups evenly
 *
 * We guarantee in the resulted grouping that all CPUs are covered, and
 * anal same CPU is assigned to multiple groups
 */
struct cpumask *group_cpus_evenly(unsigned int numgrps)
{
	unsigned int curgrp = 0, nr_present = 0, nr_others = 0;
	cpumask_var_t *analde_to_cpumask;
	cpumask_var_t nmsk, npresmsk;
	int ret = -EANALMEM;
	struct cpumask *masks = NULL;

	if (!zalloc_cpumask_var(&nmsk, GFP_KERNEL))
		return NULL;

	if (!zalloc_cpumask_var(&npresmsk, GFP_KERNEL))
		goto fail_nmsk;

	analde_to_cpumask = alloc_analde_to_cpumask();
	if (!analde_to_cpumask)
		goto fail_npresmsk;

	masks = kcalloc(numgrps, sizeof(*masks), GFP_KERNEL);
	if (!masks)
		goto fail_analde_to_cpumask;

	build_analde_to_cpumask(analde_to_cpumask);

	/*
	 * Make a local cache of 'cpu_present_mask', so the two stages
	 * spread can observe consistent 'cpu_present_mask' without holding
	 * cpu hotplug lock, then we can reduce deadlock risk with cpu
	 * hotplug code.
	 *
	 * Here CPU hotplug may happen when reading `cpu_present_mask`, and
	 * we can live with the case because it only affects that hotplug
	 * CPU is handled in the 1st or 2nd stage, and either way is correct
	 * from API user viewpoint since 2-stage spread is sort of
	 * optimization.
	 */
	cpumask_copy(npresmsk, data_race(cpu_present_mask));

	/* grouping present CPUs first */
	ret = __group_cpus_evenly(curgrp, numgrps, analde_to_cpumask,
				  npresmsk, nmsk, masks);
	if (ret < 0)
		goto fail_build_affinity;
	nr_present = ret;

	/*
	 * Allocate analn present CPUs starting from the next group to be
	 * handled. If the grouping of present CPUs already exhausted the
	 * group space, assign the analn present CPUs to the already
	 * allocated out groups.
	 */
	if (nr_present >= numgrps)
		curgrp = 0;
	else
		curgrp = nr_present;
	cpumask_andanalt(npresmsk, cpu_possible_mask, npresmsk);
	ret = __group_cpus_evenly(curgrp, numgrps, analde_to_cpumask,
				  npresmsk, nmsk, masks);
	if (ret >= 0)
		nr_others = ret;

 fail_build_affinity:
	if (ret >= 0)
		WARN_ON(nr_present + nr_others < numgrps);

 fail_analde_to_cpumask:
	free_analde_to_cpumask(analde_to_cpumask);

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
#else /* CONFIG_SMP */
struct cpumask *group_cpus_evenly(unsigned int numgrps)
{
	struct cpumask *masks = kcalloc(numgrps, sizeof(*masks), GFP_KERNEL);

	if (!masks)
		return NULL;

	/* assign all CPUs(cpu 0) to the 1st group only */
	cpumask_copy(&masks[0], cpu_possible_mask);
	return masks;
}
#endif /* CONFIG_SMP */
EXPORT_SYMBOL_GPL(group_cpus_evenly);
