/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_CPUTIME_H
#define _LINUX_SCHED_CPUTIME_H

#include <linux/sched/signal.h>

/*
 * cputime accounting APIs:
 */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#include <asm/cputime.h>

#ifndef cputime_to_nsecs
# define cputime_to_nsecs(__ct)	\
	(cputime_to_usecs(__ct) * NSEC_PER_USEC)
#endif
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
extern bool task_cputime(struct task_struct *t,
			 u64 *utime, u64 *stime);
extern u64 task_gtime(struct task_struct *t);
#else
static inline bool task_cputime(struct task_struct *t,
				u64 *utime, u64 *stime)
{
	*utime = t->utime;
	*stime = t->stime;
	return false;
}

static inline u64 task_gtime(struct task_struct *t)
{
	return t->gtime;
}
#endif

#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
static inline void task_cputime_scaled(struct task_struct *t,
				       u64 *utimescaled,
				       u64 *stimescaled)
{
	*utimescaled = t->utimescaled;
	*stimescaled = t->stimescaled;
}
#else
static inline void task_cputime_scaled(struct task_struct *t,
				       u64 *utimescaled,
				       u64 *stimescaled)
{
	task_cputime(t, utimescaled, stimescaled);
}
#endif

extern void task_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st);
extern void thread_group_cputime_adjusted(struct task_struct *p, u64 *ut, u64 *st);
extern void cputime_adjust(struct task_cputime *curr, struct prev_cputime *prev,
			   u64 *ut, u64 *st);

/*
 * Thread group CPU time accounting.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times);
void thread_group_sample_cputime(struct task_struct *tsk, u64 *samples);

/*
 * The following are functions that support scheduler-internal time accounting.
 * These functions are generally called at the timer tick.  None of this depends
 * on CONFIG_SCHEDSTATS.
 */

/**
 * get_running_cputimer - return &tsk->signal->cputimer if cputimers are active
 *
 * @tsk:	Pointer to target task.
 */
#ifdef CONFIG_POSIX_TIMERS
static inline
struct thread_group_cputimer *get_running_cputimer(struct task_struct *tsk)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;

	/*
	 * Check whether posix CPU timers are active. If not the thread
	 * group accounting is not active either. Lockless check.
	 */
	if (!READ_ONCE(tsk->signal->posix_cputimers.timers_active))
		return NULL;

	/*
	 * After we flush the task's sum_exec_runtime to sig->sum_sched_runtime
	 * in __exit_signal(), we won't account to the signal struct further
	 * cputime consumed by that task, even though the task can still be
	 * ticking after __exit_signal().
	 *
	 * In order to keep a consistent behaviour between thread group cputime
	 * and thread group cputimer accounting, lets also ignore the cputime
	 * elapsing after __exit_signal() in any thread group timer running.
	 *
	 * This makes sure that POSIX CPU clocks and timers are synchronized, so
	 * that a POSIX CPU timer won't expire while the corresponding POSIX CPU
	 * clock delta is behind the expiring timer value.
	 */
	if (unlikely(!tsk->sighand))
		return NULL;

	return cputimer;
}
#else
static inline
struct thread_group_cputimer *get_running_cputimer(struct task_struct *tsk)
{
	return NULL;
}
#endif

/**
 * account_group_user_time - Maintain utime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the utime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the utime field there.
 */
static inline void account_group_user_time(struct task_struct *tsk,
					   u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.utime);
}

/**
 * account_group_system_time - Maintain stime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the stime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the stime field there.
 */
static inline void account_group_system_time(struct task_struct *tsk,
					     u64 cputime)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(cputime, &cputimer->cputime_atomic.stime);
}

/**
 * account_group_exec_runtime - Maintain exec runtime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @ns:		Time value by which to increment the sum_exec_runtime field
 *		of the thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the sum_exec_runtime field there.
 */
static inline void account_group_exec_runtime(struct task_struct *tsk,
					      unsigned long long ns)
{
	struct thread_group_cputimer *cputimer = get_running_cputimer(tsk);

	if (!cputimer)
		return;

	atomic64_add(ns, &cputimer->cputime_atomic.sum_exec_runtime);
}

static inline void prev_cputime_init(struct prev_cputime *prev)
{
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	prev->utime = prev->stime = 0;
	raw_spin_lock_init(&prev->lock);
#endif
}

extern unsigned long long
task_sched_runtime(struct task_struct *task);

#endif /* _LINUX_SCHED_CPUTIME_H */
