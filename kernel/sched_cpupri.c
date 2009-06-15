/*
 *  kernel/sched_cpupri.c
 *
 *  CPU priority management
 *
 *  Copyright (C) 2007-2008 Novell
 *
 *  Author: Gregory Haskins <ghaskins@novell.com>
 *
 *  This code tracks the priority of each CPU so that global migration
 *  decisions are easy to calculate.  Each CPU can be in a state as follows:
 *
 *                 (INVALID), IDLE, NORMAL, RT1, ... RT99
 *
 *  going from the lowest priority to the highest.  CPUs in the INVALID state
 *  are not eligible for routing.  The system maintains this state with
 *  a 2 dimensional bitmap (the first for priority class, the second for cpus
 *  in that class).  Therefore a typical application without affinity
 *  restrictions can find a suitable CPU with O(1) complexity (e.g. two bit
 *  searches).  For tasks with affinity restrictions, the algorithm has a
 *  worst case complexity of O(min(102, nr_domcpus)), though the scenario that
 *  yields the worst case search is fairly contrived.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; version 2
 *  of the License.
 */

#include "sched_cpupri.h"

/* Convert between a 140 based task->prio, and our 102 based cpupri */
static int convert_prio(int prio)
{
	int cpupri;

	if (prio == CPUPRI_INVALID)
		cpupri = CPUPRI_INVALID;
	else if (prio == MAX_PRIO)
		cpupri = CPUPRI_IDLE;
	else if (prio >= MAX_RT_PRIO)
		cpupri = CPUPRI_NORMAL;
	else
		cpupri = MAX_RT_PRIO - prio + 1;

	return cpupri;
}

#define for_each_cpupri_active(array, idx)                    \
  for (idx = find_first_bit(array, CPUPRI_NR_PRIORITIES);     \
       idx < CPUPRI_NR_PRIORITIES;                            \
       idx = find_next_bit(array, CPUPRI_NR_PRIORITIES, idx+1))

/**
 * cpupri_find - find the best (lowest-pri) CPU in the system
 * @cp: The cpupri context
 * @p: The task
 * @lowest_mask: A mask to fill in with selected CPUs (or NULL)
 *
 * Note: This function returns the recommended CPUs as calculated during the
 * current invokation.  By the time the call returns, the CPUs may have in
 * fact changed priorities any number of times.  While not ideal, it is not
 * an issue of correctness since the normal rebalancer logic will correct
 * any discrepancies created by racing against the uncertainty of the current
 * priority configuration.
 *
 * Returns: (int)bool - CPUs were found
 */
int cpupri_find(struct cpupri *cp, struct task_struct *p,
		struct cpumask *lowest_mask)
{
	int                  idx      = 0;
	int                  task_pri = convert_prio(p->prio);

	for_each_cpupri_active(cp->pri_active, idx) {
		struct cpupri_vec *vec  = &cp->pri_to_cpu[idx];

		if (idx >= task_pri)
			break;

		if (cpumask_any_and(&p->cpus_allowed, vec->mask) >= nr_cpu_ids)
			continue;

		if (lowest_mask)
			cpumask_and(lowest_mask, &p->cpus_allowed, vec->mask);
		return 1;
	}

	return 0;
}

/**
 * cpupri_set - update the cpu priority setting
 * @cp: The cpupri context
 * @cpu: The target cpu
 * @pri: The priority (INVALID-RT99) to assign to this CPU
 *
 * Note: Assumes cpu_rq(cpu)->lock is locked
 *
 * Returns: (void)
 */
void cpupri_set(struct cpupri *cp, int cpu, int newpri)
{
	int                 *currpri = &cp->cpu_to_pri[cpu];
	int                  oldpri  = *currpri;
	unsigned long        flags;

	newpri = convert_prio(newpri);

	BUG_ON(newpri >= CPUPRI_NR_PRIORITIES);

	if (newpri == oldpri)
		return;

	/*
	 * If the cpu was currently mapped to a different value, we
	 * first need to unmap the old value
	 */
	if (likely(oldpri != CPUPRI_INVALID)) {
		struct cpupri_vec *vec  = &cp->pri_to_cpu[oldpri];

		spin_lock_irqsave(&vec->lock, flags);

		vec->count--;
		if (!vec->count)
			clear_bit(oldpri, cp->pri_active);
		cpumask_clear_cpu(cpu, vec->mask);

		spin_unlock_irqrestore(&vec->lock, flags);
	}

	if (likely(newpri != CPUPRI_INVALID)) {
		struct cpupri_vec *vec = &cp->pri_to_cpu[newpri];

		spin_lock_irqsave(&vec->lock, flags);

		cpumask_set_cpu(cpu, vec->mask);
		vec->count++;
		if (vec->count == 1)
			set_bit(newpri, cp->pri_active);

		spin_unlock_irqrestore(&vec->lock, flags);
	}

	*currpri = newpri;
}

/**
 * cpupri_init - initialize the cpupri structure
 * @cp: The cpupri context
 * @bootmem: true if allocations need to use bootmem
 *
 * Returns: -ENOMEM if memory fails.
 */
int __init_refok cpupri_init(struct cpupri *cp, bool bootmem)
{
	gfp_t gfp = GFP_KERNEL;
	int i;

	if (bootmem)
		gfp = GFP_NOWAIT;

	memset(cp, 0, sizeof(*cp));

	for (i = 0; i < CPUPRI_NR_PRIORITIES; i++) {
		struct cpupri_vec *vec = &cp->pri_to_cpu[i];

		spin_lock_init(&vec->lock);
		vec->count = 0;
		if (!zalloc_cpumask_var(&vec->mask, gfp))
			goto cleanup;
	}

	for_each_possible_cpu(i)
		cp->cpu_to_pri[i] = CPUPRI_INVALID;
	return 0;

cleanup:
	for (i--; i >= 0; i--)
		free_cpumask_var(cp->pri_to_cpu[i].mask);
	return -ENOMEM;
}

/**
 * cpupri_cleanup - clean up the cpupri structure
 * @cp: The cpupri context
 */
void cpupri_cleanup(struct cpupri *cp)
{
	int i;

	for (i = 0; i < CPUPRI_NR_PRIORITIES; i++)
		free_cpumask_var(cp->pri_to_cpu[i].mask);
}
