/* Freezer declarations */

#include <linux/sched.h>

#ifdef CONFIG_PM
/*
 * Check if a process has been frozen
 */
static inline int frozen(struct task_struct *p)
{
	return p->flags & PF_FROZEN;
}

/*
 * Check if there is a request to freeze a process
 */
static inline int freezing(struct task_struct *p)
{
	return test_tsk_thread_flag(p, TIF_FREEZE);
}

/*
 * Request that a process be frozen
 */
static inline void freeze(struct task_struct *p)
{
	set_tsk_thread_flag(p, TIF_FREEZE);
}

/*
 * Sometimes we may need to cancel the previous 'freeze' request
 */
static inline void do_not_freeze(struct task_struct *p)
{
	clear_tsk_thread_flag(p, TIF_FREEZE);
}

/*
 * Wake up a frozen process
 *
 * task_lock() is taken to prevent the race with refrigerator() which may
 * occur if the freezing of tasks fails.  Namely, without the lock, if the
 * freezing of tasks failed, thaw_tasks() might have run before a task in
 * refrigerator() could call frozen_process(), in which case the task would be
 * frozen and no one would thaw it.
 */
static inline int thaw_process(struct task_struct *p)
{
	task_lock(p);
	if (frozen(p)) {
		p->flags &= ~PF_FROZEN;
		task_unlock(p);
		wake_up_process(p);
		return 1;
	}
	clear_tsk_thread_flag(p, TIF_FREEZE);
	task_unlock(p);
	return 0;
}

/*
 * freezing is complete, mark process as frozen
 */
static inline void frozen_process(struct task_struct *p)
{
	p->flags |= PF_FROZEN;
	wmb();
	clear_tsk_thread_flag(p, TIF_FREEZE);
}

extern void refrigerator(void);
extern int freeze_processes(void);
extern void thaw_processes(void);

static inline int try_to_freeze(void)
{
	if (freezing(current)) {
		refrigerator();
		return 1;
	} else
		return 0;
}

extern void thaw_some_processes(int all);

#else
static inline int frozen(struct task_struct *p) { return 0; }
static inline int freezing(struct task_struct *p) { return 0; }
static inline void freeze(struct task_struct *p) { BUG(); }
static inline int thaw_process(struct task_struct *p) { return 1; }
static inline void frozen_process(struct task_struct *p) { BUG(); }

static inline void refrigerator(void) {}
static inline int freeze_processes(void) { BUG(); return 0; }
static inline void thaw_processes(void) {}

static inline int try_to_freeze(void) { return 0; }


#endif
