/*
 *  kernel/sched/cpudl.c
 *
 *  Global CPU deadline management
 *
 *  Author: Juri Lelli <j.lelli@sssup.it>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; version 2
 *  of the License.
 */

#include <linux/gfp.h>
#include <linux/kernel.h>
#include "cpudeadline.h"

static inline int parent(int i)
{
	return (i - 1) >> 1;
}

static inline int left_child(int i)
{
	return (i << 1) + 1;
}

static inline int right_child(int i)
{
	return (i << 1) + 2;
}

static inline int dl_time_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

void cpudl_exchange(struct cpudl *cp, int a, int b)
{
	int cpu_a = cp->elements[a].cpu, cpu_b = cp->elements[b].cpu;

	swap(cp->elements[a], cp->elements[b]);
	swap(cp->cpu_to_idx[cpu_a], cp->cpu_to_idx[cpu_b]);
}

void cpudl_heapify(struct cpudl *cp, int idx)
{
	int l, r, largest;

	/* adapted from lib/prio_heap.c */
	while(1) {
		l = left_child(idx);
		r = right_child(idx);
		largest = idx;

		if ((l < cp->size) && dl_time_before(cp->elements[idx].dl,
							cp->elements[l].dl))
			largest = l;
		if ((r < cp->size) && dl_time_before(cp->elements[largest].dl,
							cp->elements[r].dl))
			largest = r;
		if (largest == idx)
			break;

		/* Push idx down the heap one level and bump one up */
		cpudl_exchange(cp, largest, idx);
		idx = largest;
	}
}

void cpudl_change_key(struct cpudl *cp, int idx, u64 new_dl)
{
	WARN_ON(idx > num_present_cpus() || idx == IDX_INVALID);

	if (dl_time_before(new_dl, cp->elements[idx].dl)) {
		cp->elements[idx].dl = new_dl;
		cpudl_heapify(cp, idx);
	} else {
		cp->elements[idx].dl = new_dl;
		while (idx > 0 && dl_time_before(cp->elements[parent(idx)].dl,
					cp->elements[idx].dl)) {
			cpudl_exchange(cp, idx, parent(idx));
			idx = parent(idx);
		}
	}
}

static inline int cpudl_maximum(struct cpudl *cp)
{
	return cp->elements[0].cpu;
}

/*
 * cpudl_find - find the best (later-dl) CPU in the system
 * @cp: the cpudl max-heap context
 * @p: the task
 * @later_mask: a mask to fill in with the selected CPUs (or NULL)
 *
 * Returns: int - best CPU (heap maximum if suitable)
 */
int cpudl_find(struct cpudl *cp, struct task_struct *p,
	       struct cpumask *later_mask)
{
	int best_cpu = -1;
	const struct sched_dl_entity *dl_se = &p->dl;

	if (later_mask && cpumask_and(later_mask, cp->free_cpus,
			&p->cpus_allowed) && cpumask_and(later_mask,
			later_mask, cpu_active_mask)) {
		best_cpu = cpumask_any(later_mask);
		goto out;
	} else if (cpumask_test_cpu(cpudl_maximum(cp), &p->cpus_allowed) &&
			dl_time_before(dl_se->deadline, cp->elements[0].dl)) {
		best_cpu = cpudl_maximum(cp);
		if (later_mask)
			cpumask_set_cpu(best_cpu, later_mask);
	}

out:
	WARN_ON(best_cpu > num_present_cpus() && best_cpu != -1);

	return best_cpu;
}

/*
 * cpudl_set - update the cpudl max-heap
 * @cp: the cpudl max-heap context
 * @cpu: the target cpu
 * @dl: the new earliest deadline for this cpu
 *
 * Notes: assumes cpu_rq(cpu)->lock is locked
 *
 * Returns: (void)
 */
void cpudl_set(struct cpudl *cp, int cpu, u64 dl, int is_valid)
{
	int old_idx, new_cpu;
	unsigned long flags;

	WARN_ON(cpu > num_present_cpus());

	raw_spin_lock_irqsave(&cp->lock, flags);
	old_idx = cp->cpu_to_idx[cpu];
	if (!is_valid) {
		/* remove item */
		if (old_idx == IDX_INVALID) {
			/*
			 * Nothing to remove if old_idx was invalid.
			 * This could happen if a rq_offline_dl is
			 * called for a CPU without -dl tasks running.
			 */
			goto out;
		}
		new_cpu = cp->elements[cp->size - 1].cpu;
		cp->elements[old_idx].dl = cp->elements[cp->size - 1].dl;
		cp->elements[old_idx].cpu = new_cpu;
		cp->size--;
		cp->cpu_to_idx[new_cpu] = old_idx;
		cp->cpu_to_idx[cpu] = IDX_INVALID;
		while (old_idx > 0 && dl_time_before(
				cp->elements[parent(old_idx)].dl,
				cp->elements[old_idx].dl)) {
			cpudl_exchange(cp, old_idx, parent(old_idx));
			old_idx = parent(old_idx);
		}
		cpumask_set_cpu(cpu, cp->free_cpus);
                cpudl_heapify(cp, old_idx);

		goto out;
	}

	if (old_idx == IDX_INVALID) {
		cp->size++;
		cp->elements[cp->size - 1].dl = 0;
		cp->elements[cp->size - 1].cpu = cpu;
		cp->cpu_to_idx[cpu] = cp->size - 1;
		cpudl_change_key(cp, cp->size - 1, dl);
		cpumask_clear_cpu(cpu, cp->free_cpus);
	} else {
		cpudl_change_key(cp, old_idx, dl);
	}

out:
	raw_spin_unlock_irqrestore(&cp->lock, flags);
}

/*
 * cpudl_init - initialize the cpudl structure
 * @cp: the cpudl max-heap context
 */
int cpudl_init(struct cpudl *cp)
{
	int i;

	memset(cp, 0, sizeof(*cp));
	raw_spin_lock_init(&cp->lock);
	cp->size = 0;
	for (i = 0; i < NR_CPUS; i++)
		cp->cpu_to_idx[i] = IDX_INVALID;
	if (!alloc_cpumask_var(&cp->free_cpus, GFP_KERNEL))
		return -ENOMEM;
	cpumask_setall(cp->free_cpus);

	return 0;
}

/*
 * cpudl_cleanup - clean up the cpudl structure
 * @cp: the cpudl max-heap context
 */
void cpudl_cleanup(struct cpudl *cp)
{
	/*
	 * nothing to do for the moment
	 */
}
