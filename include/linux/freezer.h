/* Freezer declarations */

#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#ifdef CONFIG_FREEZER
extern atomic_t system_freezing_cnt;	/* nr of freezing conds in effect */
extern bool pm_freezing;		/* PM freezing in effect */
extern bool pm_nosig_freezing;		/* PM nosig freezing in effect */

/*
 * Check if a process has been frozen
 */
static inline bool frozen(struct task_struct *p)
{
	return p->flags & PF_FROZEN;
}

extern bool freezing_slow_path(struct task_struct *p);

/*
 * Check if there is a request to freeze a process
 */
static inline bool freezing(struct task_struct *p)
{
	if (likely(!atomic_read(&system_freezing_cnt)))
		return false;
	return freezing_slow_path(p);
}

/* Takes and releases task alloc lock using task_lock() */
extern void __thaw_task(struct task_struct *t);

extern bool __refrigerator(bool check_kthr_stop);
extern int freeze_processes(void);
extern int freeze_kernel_threads(void);
extern void thaw_processes(void);

static inline bool try_to_freeze(void)
{
	might_sleep();
	if (likely(!freezing(current)))
		return false;
	return __refrigerator(false);
}

extern bool freeze_task(struct task_struct *p);
extern bool set_freezable(void);

#ifdef CONFIG_CGROUP_FREEZER
extern bool cgroup_freezing(struct task_struct *task);
#else /* !CONFIG_CGROUP_FREEZER */
static inline bool cgroup_freezing(struct task_struct *task)
{
	return false;
}
#endif /* !CONFIG_CGROUP_FREEZER */

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
 * Check if the task should be counted as freezable by the freezer
 */
static inline int freezer_should_skip(struct task_struct *p)
{
	return !!(p->flags & PF_FREEZER_SKIP);
}

/*
 * Freezer-friendly wrappers around wait_event_interruptible(),
 * wait_event_killable() and wait_event_interruptible_timeout(), originally
 * defined in <linux/wait.h>
 */

#define wait_event_freezekillable(wq, condition)			\
({									\
	int __retval;							\
	freezer_do_not_count();						\
	__retval = wait_event_killable(wq, (condition));		\
	freezer_count();						\
	__retval;							\
})

#define wait_event_freezable(wq, condition)				\
({									\
	int __retval;							\
	for (;;) {							\
		__retval = wait_event_interruptible(wq, 		\
				(condition) || freezing(current));	\
		if (__retval || (condition))				\
			break;						\
		try_to_freeze();					\
	}								\
	__retval;							\
})

#define wait_event_freezable_timeout(wq, condition, timeout)		\
({									\
	long __retval = timeout;					\
	for (;;) {							\
		__retval = wait_event_interruptible_timeout(wq,		\
				(condition) || freezing(current),	\
				__retval); 				\
		if (__retval <= 0 || (condition))			\
			break;						\
		try_to_freeze();					\
	}								\
	__retval;							\
})

#else /* !CONFIG_FREEZER */
static inline bool frozen(struct task_struct *p) { return false; }
static inline bool freezing(struct task_struct *p) { return false; }

static inline bool __refrigerator(bool check_kthr_stop) { return false; }
static inline int freeze_processes(void) { return -ENOSYS; }
static inline int freeze_kernel_threads(void) { return -ENOSYS; }
static inline void thaw_processes(void) {}

static inline bool try_to_freeze(void) { return false; }

static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
static inline int freezer_should_skip(struct task_struct *p) { return 0; }
static inline void set_freezable(void) {}

#define wait_event_freezable(wq, condition)				\
		wait_event_interruptible(wq, condition)

#define wait_event_freezable_timeout(wq, condition, timeout)		\
		wait_event_interruptible_timeout(wq, condition, timeout)

#define wait_event_freezekillable(wq, condition)		\
		wait_event_killable(wq, condition)

#endif /* !CONFIG_FREEZER */

#endif	/* FREEZER_H_INCLUDED */
