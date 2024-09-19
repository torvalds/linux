/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_DEADLINE_H
#define _LINUX_SCHED_DEADLINE_H

/*
 * SCHED_DEADLINE tasks has negative priorities, reflecting
 * the fact that any of them has higher prio than RT and
 * NORMAL/BATCH tasks.
 */

#include <linux/sched.h>

static inline bool dl_prio(int prio)
{
	return unlikely(prio < MAX_DL_PRIO);
}

/*
 * Returns true if a task has a priority that belongs to DL class. PI-boosted
 * tasks will return true. Use dl_policy() to ignore PI-boosted tasks.
 */
static inline bool dl_task(struct task_struct *p)
{
	return dl_prio(p->prio);
}

static inline bool dl_time_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

#ifdef CONFIG_SMP

struct root_domain;
extern void dl_add_task_root_domain(struct task_struct *p);
extern void dl_clear_root_domain(struct root_domain *rd);

#endif /* CONFIG_SMP */

#endif /* _LINUX_SCHED_DEADLINE_H */
