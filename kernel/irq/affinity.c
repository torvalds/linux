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

static void irq_spread_init_one(struct cpumask *irqmsk, struct cpumask *nmsk,
				unsigned int cpus_per_vec)
{
	const struct cpumask *siblmsk;
	int cpu, sibl;

	for ( ; cpus_per_vec > 0; ) {
		cpu = cpumask_first(nmsk);

		/* Should yest happen, but I'm too lazy to think about it */
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

static cpumask_var_t *alloc_yesde_to_cpumask(void)
{
	cpumask_var_t *masks;
	int yesde;

	masks = kcalloc(nr_yesde_ids, sizeof(cpumask_var_t), GFP_KERNEL);
	if (!masks)
		return NULL;

	for (yesde = 0; yesde < nr_yesde_ids; yesde++) {
		if (!zalloc_cpumask_var(&masks[yesde], GFP_KERNEL))
			goto out_unwind;
	}

	return masks;

out_unwind:
	while (--yesde >= 0)
		free_cpumask_var(masks[yesde]);
	kfree(masks);
	return NULL;
}

static void free_yesde_to_cpumask(cpumask_var_t *masks)
{
	int yesde;

	for (yesde = 0; yesde < nr_yesde_ids; yesde++)
		free_cpumask_var(masks[yesde]);
	kfree(masks);
}

static void build_yesde_to_cpumask(cpumask_var_t *masks)
{
	int cpu;

	for_each_possible_cpu(cpu)
		cpumask_set_cpu(cpu, masks[cpu_to_yesde(cpu)]);
}

static int get_yesdes_in_cpumask(cpumask_var_t *yesde_to_cpumask,
				const struct cpumask *mask, yesdemask_t *yesdemsk)
{
	int n, yesdes = 0;

	/* Calculate the number of yesdes in the supplied affinity mask */
	for_each_yesde(n) {
		if (cpumask_intersects(mask, yesde_to_cpumask[n])) {
			yesde_set(n, *yesdemsk);
			yesdes++;
		}
	}
	return yesdes;
}

struct yesde_vectors {
	unsigned id;

	union {
		unsigned nvectors;
		unsigned ncpus;
	};
};

static int ncpus_cmp_func(const void *l, const void *r)
{
	const struct yesde_vectors *ln = l;
	const struct yesde_vectors *rn = r;

	return ln->ncpus - rn->ncpus;
}

/*
 * Allocate vector number for each yesde, so that for each yesde:
 *
 * 1) the allocated number is >= 1
 *
 * 2) the allocated numbver is <= active CPU number of this yesde
 *
 * The actual allocated total vectors may be less than @numvecs when
 * active total CPU number is less than @numvecs.
 *
 * Active CPUs means the CPUs in '@cpu_mask AND @yesde_to_cpumask[]'
 * for each yesde.
 */
static void alloc_yesdes_vectors(unsigned int numvecs,
				cpumask_var_t *yesde_to_cpumask,
				const struct cpumask *cpu_mask,
				const yesdemask_t yesdemsk,
				struct cpumask *nmsk,
				struct yesde_vectors *yesde_vectors)
{
	unsigned n, remaining_ncpus = 0;

	for (n = 0; n < nr_yesde_ids; n++) {
		yesde_vectors[n].id = n;
		yesde_vectors[n].ncpus = UINT_MAX;
	}

	for_each_yesde_mask(n, yesdemsk) {
		unsigned ncpus;

		cpumask_and(nmsk, cpu_mask, yesde_to_cpumask[n]);
		ncpus = cpumask_weight(nmsk);

		if (!ncpus)
			continue;
		remaining_ncpus += ncpus;
		yesde_vectors[n].ncpus = ncpus;
	}

	numvecs = min_t(unsigned, remaining_ncpus, numvecs);

	sort(yesde_vectors, nr_yesde_ids, sizeof(yesde_vectors[0]),
	     ncpus_cmp_func, NULL);

	/*
	 * Allocate vectors for each yesde according to the ratio of this
	 * yesde's nr_cpus to remaining un-assigned ncpus. 'numvecs' is
	 * bigger than number of active numa yesdes. Always start the
	 * allocation from the yesde with minimized nr_cpus.
	 *
	 * This way guarantees that each active yesde gets allocated at
	 * least one vector, and the theory is simple: over-allocation
	 * is only done when this yesde is assigned by one vector, so
	 * other yesdes will be allocated >= 1 vector, since 'numvecs' is
	 * bigger than number of numa yesdes.
	 *
	 * One perfect invariant is that number of allocated vectors for
	 * each yesde is <= CPU count of this yesde:
	 *
	 * 1) suppose there are two yesdes: A and B
	 * 	ncpu(X) is CPU count of yesde X
	 * 	vecs(X) is the vector count allocated to yesde X via this
	 * 	algorithm
	 *
	 * 	ncpu(A) <= ncpu(B)
	 * 	ncpu(A) + ncpu(B) = N
	 * 	vecs(A) + vecs(B) = V
	 *
	 * 	vecs(A) = max(1, round_down(V * ncpu(A) / N))
	 * 	vecs(B) = V - vecs(A)
	 *
	 * 	both N and V are integer, and 2 <= V <= N, suppose
	 * 	V = N - delta, and 0 <= delta <= N - 2
	 *
	 * 2) obviously vecs(A) <= ncpu(A) because:
	 *
	 * 	if vecs(A) is 1, then vecs(A) <= ncpu(A) given
	 * 	ncpu(A) >= 1
	 *
	 * 	otherwise,
	 * 		vecs(A) <= V * ncpu(A) / N <= ncpu(A), given V <= N
	 *
	 * 3) prove how vecs(B) <= ncpu(B):
	 *
	 * 	if round_down(V * ncpu(A) / N) == 0, vecs(B) won't be
	 * 	over-allocated, so vecs(B) <= ncpu(B),
	 *
	 * 	otherwise:
	 *
	 * 	vecs(A) =
	 * 		round_down(V * ncpu(A) / N) =
	 * 		round_down((N - delta) * ncpu(A) / N) =
	 * 		round_down((N * ncpu(A) - delta * ncpu(A)) / N)	 >=
	 * 		round_down((N * ncpu(A) - delta * N) / N)	 =
	 * 		cpu(A) - delta
	 *
	 * 	then:
	 *
	 * 	vecs(A) - V >= ncpu(A) - delta - V
	 * 	=>
	 * 	V - vecs(A) <= V + delta - ncpu(A)
	 * 	=>
	 * 	vecs(B) <= N - ncpu(A)
	 * 	=>
	 * 	vecs(B) <= cpu(B)
	 *
	 * For yesdes >= 3, it can be thought as one yesde and ayesther big
	 * yesde given that is exactly what this algorithm is implemented,
	 * and we always re-calculate 'remaining_ncpus' & 'numvecs', and
	 * finally for each yesde X: vecs(X) <= ncpu(X).
	 *
	 */
	for (n = 0; n < nr_yesde_ids; n++) {
		unsigned nvectors, ncpus;

		if (yesde_vectors[n].ncpus == UINT_MAX)
			continue;

		WARN_ON_ONCE(numvecs == 0);

		ncpus = yesde_vectors[n].ncpus;
		nvectors = max_t(unsigned, 1,
				 numvecs * ncpus / remaining_ncpus);
		WARN_ON_ONCE(nvectors > ncpus);

		yesde_vectors[n].nvectors = nvectors;

		remaining_ncpus -= ncpus;
		numvecs -= nvectors;
	}
}

static int __irq_build_affinity_masks(unsigned int startvec,
				      unsigned int numvecs,
				      unsigned int firstvec,
				      cpumask_var_t *yesde_to_cpumask,
				      const struct cpumask *cpu_mask,
				      struct cpumask *nmsk,
				      struct irq_affinity_desc *masks)
{
	unsigned int i, n, yesdes, cpus_per_vec, extra_vecs, done = 0;
	unsigned int last_affv = firstvec + numvecs;
	unsigned int curvec = startvec;
	yesdemask_t yesdemsk = NODE_MASK_NONE;
	struct yesde_vectors *yesde_vectors;

	if (!cpumask_weight(cpu_mask))
		return 0;

	yesdes = get_yesdes_in_cpumask(yesde_to_cpumask, cpu_mask, &yesdemsk);

	/*
	 * If the number of yesdes in the mask is greater than or equal the
	 * number of vectors we just spread the vectors across the yesdes.
	 */
	if (numvecs <= yesdes) {
		for_each_yesde_mask(n, yesdemsk) {
			cpumask_or(&masks[curvec].mask, &masks[curvec].mask,
				   yesde_to_cpumask[n]);
			if (++curvec == last_affv)
				curvec = firstvec;
		}
		return numvecs;
	}

	yesde_vectors = kcalloc(nr_yesde_ids,
			       sizeof(struct yesde_vectors),
			       GFP_KERNEL);
	if (!yesde_vectors)
		return -ENOMEM;

	/* allocate vector number for each yesde */
	alloc_yesdes_vectors(numvecs, yesde_to_cpumask, cpu_mask,
			    yesdemsk, nmsk, yesde_vectors);

	for (i = 0; i < nr_yesde_ids; i++) {
		unsigned int ncpus, v;
		struct yesde_vectors *nv = &yesde_vectors[i];

		if (nv->nvectors == UINT_MAX)
			continue;

		/* Get the cpus on this yesde which are in the mask */
		cpumask_and(nmsk, cpu_mask, yesde_to_cpumask[nv->id]);
		ncpus = cpumask_weight(nmsk);
		if (!ncpus)
			continue;

		WARN_ON_ONCE(nv->nvectors > ncpus);

		/* Account for rounding errors */
		extra_vecs = ncpus - nv->nvectors * (ncpus / nv->nvectors);

		/* Spread allocated vectors on CPUs of the current yesde */
		for (v = 0; v < nv->nvectors; v++, curvec++) {
			cpus_per_vec = ncpus / nv->nvectors;

			/* Account for extra vectors to compensate rounding errors */
			if (extra_vecs) {
				cpus_per_vec++;
				--extra_vecs;
			}

			/*
			 * wrapping has to be considered given 'startvec'
			 * may start anywhere
			 */
			if (curvec >= last_affv)
				curvec = firstvec;
			irq_spread_init_one(&masks[curvec].mask, nmsk,
						cpus_per_vec);
		}
		done += nv->nvectors;
	}
	kfree(yesde_vectors);
	return done;
}

/*
 * build affinity in two stages:
 *	1) spread present CPU on these vectors
 *	2) spread other possible CPUs on these vectors
 */
static int irq_build_affinity_masks(unsigned int startvec, unsigned int numvecs,
				    unsigned int firstvec,
				    struct irq_affinity_desc *masks)
{
	unsigned int curvec = startvec, nr_present = 0, nr_others = 0;
	cpumask_var_t *yesde_to_cpumask;
	cpumask_var_t nmsk, npresmsk;
	int ret = -ENOMEM;

	if (!zalloc_cpumask_var(&nmsk, GFP_KERNEL))
		return ret;

	if (!zalloc_cpumask_var(&npresmsk, GFP_KERNEL))
		goto fail_nmsk;

	yesde_to_cpumask = alloc_yesde_to_cpumask();
	if (!yesde_to_cpumask)
		goto fail_npresmsk;

	/* Stabilize the cpumasks */
	get_online_cpus();
	build_yesde_to_cpumask(yesde_to_cpumask);

	/* Spread on present CPUs starting from affd->pre_vectors */
	ret = __irq_build_affinity_masks(curvec, numvecs, firstvec,
					 yesde_to_cpumask, cpu_present_mask,
					 nmsk, masks);
	if (ret < 0)
		goto fail_build_affinity;
	nr_present = ret;

	/*
	 * Spread on yesn present CPUs starting from the next vector to be
	 * handled. If the spreading of present CPUs already exhausted the
	 * vector space, assign the yesn present CPUs to the already spread
	 * out vectors.
	 */
	if (nr_present >= numvecs)
		curvec = firstvec;
	else
		curvec = firstvec + nr_present;
	cpumask_andyest(npresmsk, cpu_possible_mask, cpu_present_mask);
	ret = __irq_build_affinity_masks(curvec, numvecs, firstvec,
					 yesde_to_cpumask, npresmsk, nmsk,
					 masks);
	if (ret >= 0)
		nr_others = ret;

 fail_build_affinity:
	put_online_cpus();

	if (ret >= 0)
		WARN_ON(nr_present + nr_others < numvecs);

	free_yesde_to_cpumask(yesde_to_cpumask);

 fail_npresmsk:
	free_cpumask_var(npresmsk);

 fail_nmsk:
	free_cpumask_var(nmsk);
	return ret < 0 ? ret : 0;
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
	 * then yesthing to do here except for invoking the calc_sets()
	 * callback so the device driver can adjust to the situation.
	 */
	if (nvecs > affd->pre_vectors + affd->post_vectors)
		affvecs = nvecs - affd->pre_vectors - affd->post_vectors;
	else
		affvecs = 0;

	/*
	 * Simple invocations do yest provide a calc_sets() callback. Install
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
		int ret;

		ret = irq_build_affinity_masks(curvec, this_vecs,
					       curvec, masks);
		if (ret) {
			kfree(masks);
			return NULL;
		}
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
		get_online_cpus();
		set_vecs = cpumask_weight(cpu_possible_mask);
		put_online_cpus();
	}

	return resv + min(set_vecs, maxvec - resv);
}
