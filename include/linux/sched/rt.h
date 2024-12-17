/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RT_H
#define _LINUX_SCHED_RT_H

#include <linux/sched.h>

struct task_struct;

static inline bool rt_prio(int prio)
{
	return unlikely(prio < MAX_RT_PRIO && prio >= MAX_DL_PRIO);
}

static inline bool rt_or_dl_prio(int prio)
{
	return unlikely(prio < MAX_RT_PRIO);
}

/*
 * Returns true if a task has a priority that belongs to RT class. PI-boosted
 * tasks will return true. Use rt_policy() to ignore PI-boosted tasks.
 */
static inline bool rt_task(struct task_struct *p)
{
	return rt_prio(p->prio);
}

/*
 * Returns true if a task has a priority that belongs to RT or DL classes.
 * PI-boosted tasks will return true. Use rt_or_dl_task_policy() to ignore
 * PI-boosted tasks.
 */
static inline bool rt_or_dl_task(struct task_struct *p)
{
	return rt_or_dl_prio(p->prio);
}

/*
 * Returns true if a task has a policy that belongs to RT or DL classes.
 * PI-boosted tasks will return false.
 */
static inline bool rt_or_dl_task_policy(struct task_struct *tsk)
{
	int policy = tsk->policy;

	if (policy == SCHED_FIFO || policy == SCHED_RR)
		return true;
	if (policy == SCHED_DEADLINE)
		return true;
	return false;
}

#ifdef CONFIG_RT_MUTEXES
extern void rt_mutex_pre_schedule(void);
extern void rt_mutex_schedule(void);
extern void rt_mutex_post_schedule(void);

/*
 * Must hold either p->pi_lock or task_rq(p)->lock.
 */
static inline struct task_struct *rt_mutex_get_top_task(struct task_struct *p)
{
	return p->pi_top_task;
}
extern void rt_mutex_setprio(struct task_struct *p, struct task_struct *pi_task);
extern void rt_mutex_adjust_pi(struct task_struct *p);
#else
static inline struct task_struct *rt_mutex_get_top_task(struct task_struct *task)
{
	return NULL;
}
# define rt_mutex_adjust_pi(p)		do { } while (0)
#endif

extern void normalize_rt_tasks(void);


/*
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define RR_TIMESLICE		(100 * HZ / 1000)

#endif /* _LINUX_SCHED_RT_H */
