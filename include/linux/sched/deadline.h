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

struct root_domain;
extern void dl_add_task_root_domain(struct task_struct *p);
extern void dl_clear_root_domain(struct root_domain *rd);
extern void dl_clear_root_domain_cpu(int cpu);

extern u64 dl_cookie;
extern bool dl_bw_visited(int cpu, u64 cookie);

static inline bool dl_server(struct sched_dl_entity *dl_se)
{
	return dl_se->dl_server;
}

static inline struct task_struct *dl_task_of(struct sched_dl_entity *dl_se)
{
	BUG_ON(dl_server(dl_se));
	return container_of(dl_se, struct task_struct, dl);
}

/*
 * Regarding the deadline, a task with implicit deadline has a relative
 * deadline == relative period. A task with constrained deadline has a
 * relative deadline <= relative period.
 *
 * We support constrained deadline tasks. However, there are some restrictions
 * applied only for tasks which do not have an implicit deadline. See
 * update_dl_entity() to know more about such restrictions.
 *
 * The dl_is_implicit() returns true if the task has an implicit deadline.
 */
static inline bool dl_is_implicit(struct sched_dl_entity *dl_se)
{
	return dl_se->dl_deadline == dl_se->dl_period;
}

#endif /* _LINUX_SCHED_DEADLINE_H */
