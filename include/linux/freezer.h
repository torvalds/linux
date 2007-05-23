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
	if (!unlikely(p->flags & PF_NOFREEZE)) {
		p->flags |= PF_FROZEN;
		wmb();
	}
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

/*
 * The PF_FREEZER_SKIP flag should be set by a vfork parent right before it
 * calls wait_for_completion(&vfork) and reset right after it returns from this
 * function.  Next, the parent should call try_to_freeze() to freeze itself
 * appropriately in case the child has exited before the freezing of tasks is
 * complete.  However, we don't want kernel threads to be frozen in unexpected
 * places, so we allow them to block freeze_processes() instead or to set
 * PF_NOFREEZE if needed and PF_FREEZER_SKIP is only set for userland vfork
 * parents.  Fortunately, in the ____call_usermodehelper() case the parent won't
 * really block freeze_processes(), since ____call_usermodehelper() (the child)
 * does a little before exec/exit and it can't be frozen before waking up the
 * parent.
 */

/*
 * If the current task is a user space one, tell the freezer not to count it as
 * freezable.
 */
static inline void freezer_do_not_count(void)
{
	if (current->mm)
		current->flags |= PF_FREEZER_SKIP;
}

/*
 * If the current task is a user space one, tell the freezer to count it as
 * freezable again and try to freeze it.
 */
static inline void freezer_count(void)
{
	if (current->mm) {
		current->flags &= ~PF_FREEZER_SKIP;
		try_to_freeze();
	}
}

/*
 * Check if the task should be counted as freezeable by the freezer
 */
static inline int freezer_should_skip(struct task_struct *p)
{
	return !!(p->flags & PF_FREEZER_SKIP);
}

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

static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
static inline int freezer_should_skip(struct task_struct *p) { return 0; }
#endif
