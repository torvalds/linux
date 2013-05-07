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
 * Timeout for stopping processes
 */
extern unsigned int freeze_timeout_msecs;

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
extern void thaw_kernel_threads(void);

/*
 * DO NOT ADD ANY NEW CALLERS OF THIS FUNCTION
 * If try_to_freeze causes a lockdep warning it means the caller may deadlock
 */
static inline bool try_to_freeze_unsafe(void)
{
	might_sleep();
	if (likely(!freezing(current)))
		return false;
	return __refrigerator(false);
}

static inline bool try_to_freeze(void)
{
	return try_to_freeze_unsafe();
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
 * PF_NOFREEZE if needed. Fortunately, in the ____call_usermodehelper() case the
 * parent won't really block freeze_processes(), since ____call_usermodehelper()
 * (the child) does a little before exec/exit and it can't be frozen before
 * waking up the parent.
 */


/**
 * freezer_do_not_count - tell freezer to ignore %current
 *
 * Tell freezers to ignore the current task when determining whether the
 * target frozen state is reached.  IOW, the current task will be
 * considered frozen enough by freezers.
 *
 * The caller shouldn't do anything which isn't allowed for a frozen task
 * until freezer_cont() is called.  Usually, freezer[_do_not]_count() pair
 * wrap a scheduling operation and nothing much else.
 */
static inline void freezer_do_not_count(void)
{
	current->flags |= PF_FREEZER_SKIP;
}

/**
 * freezer_count - tell freezer to stop ignoring %current
 *
 * Undo freezer_do_not_count().  It tells freezers that %current should be
 * considered again and tries to freeze if freezing condition is already in
 * effect.
 */
static inline void freezer_count(void)
{
	current->flags &= ~PF_FREEZER_SKIP;
	/*
	 * If freezing is in progress, the following paired with smp_mb()
	 * in freezer_should_skip() ensures that either we see %true
	 * freezing() or freezer_should_skip() sees !PF_FREEZER_SKIP.
	 */
	smp_mb();
	try_to_freeze();
}

/* DO NOT ADD ANY NEW CALLERS OF THIS FUNCTION */
static inline void freezer_count_unsafe(void)
{
	current->flags &= ~PF_FREEZER_SKIP;
	smp_mb();
	try_to_freeze_unsafe();
}

/**
 * freezer_should_skip - whether to skip a task when determining frozen
 *			 state is reached
 * @p: task in quesion
 *
 * This function is used by freezers after establishing %true freezing() to
 * test whether a task should be skipped when determining the target frozen
 * state is reached.  IOW, if this function returns %true, @p is considered
 * frozen enough.
 */
static inline bool freezer_should_skip(struct task_struct *p)
{
	/*
	 * The following smp_mb() paired with the one in freezer_count()
	 * ensures that either freezer_count() sees %true freezing() or we
	 * see cleared %PF_FREEZER_SKIP and return %false.  This makes it
	 * impossible for a task to slip frozen state testing after
	 * clearing %PF_FREEZER_SKIP.
	 */
	smp_mb();
	return p->flags & PF_FREEZER_SKIP;
}

/*
 * These macros are intended to be used whenever you want allow a sleeping
 * task to be frozen. Note that neither return any clear indication of
 * whether a freeze event happened while in this function.
 */

/* Like schedule(), but should not block the freezer. */
#define freezable_schedule()						\
({									\
	freezer_do_not_count();						\
	schedule();							\
	freezer_count();						\
})

/* DO NOT ADD ANY NEW CALLERS OF THIS FUNCTION */
#define freezable_schedule_unsafe()					\
({									\
	freezer_do_not_count();						\
	schedule();							\
	freezer_count_unsafe();						\
})

/* Like schedule_timeout_killable(), but should not block the freezer. */
#define freezable_schedule_timeout_killable(timeout)			\
({									\
	long __retval;							\
	freezer_do_not_count();						\
	__retval = schedule_timeout_killable(timeout);			\
	freezer_count();						\
	__retval;							\
})

/* DO NOT ADD ANY NEW CALLERS OF THIS FUNCTION */
#define freezable_schedule_timeout_killable_unsafe(timeout)		\
({									\
	long __retval;							\
	freezer_do_not_count();						\
	__retval = schedule_timeout_killable(timeout);			\
	freezer_count_unsafe();						\
	__retval;							\
})

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

/* DO NOT ADD ANY NEW CALLERS OF THIS FUNCTION */
#define wait_event_freezekillable_unsafe(wq, condition)			\
({									\
	int __retval;							\
	freezer_do_not_count();						\
	__retval = wait_event_killable(wq, (condition));		\
	freezer_count_unsafe();						\
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
static inline void __thaw_task(struct task_struct *t) {}

static inline bool __refrigerator(bool check_kthr_stop) { return false; }
static inline int freeze_processes(void) { return -ENOSYS; }
static inline int freeze_kernel_threads(void) { return -ENOSYS; }
static inline void thaw_processes(void) {}
static inline void thaw_kernel_threads(void) {}

static inline bool try_to_freeze_nowarn(void) { return false; }
static inline bool try_to_freeze(void) { return false; }

static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
static inline int freezer_should_skip(struct task_struct *p) { return 0; }
static inline void set_freezable(void) {}

#define freezable_schedule()  schedule()

#define freezable_schedule_unsafe()  schedule()

#define freezable_schedule_timeout_killable(timeout)			\
	schedule_timeout_killable(timeout)

#define freezable_schedule_timeout_killable_unsafe(timeout)		\
	schedule_timeout_killable(timeout)

#define wait_event_freezable(wq, condition)				\
		wait_event_interruptible(wq, condition)

#define wait_event_freezable_timeout(wq, condition, timeout)		\
		wait_event_interruptible_timeout(wq, condition, timeout)

#define wait_event_freezekillable(wq, condition)		\
		wait_event_killable(wq, condition)

#define wait_event_freezekillable_unsafe(wq, condition)			\
		wait_event_killable(wq, condition)

#endif /* !CONFIG_FREEZER */

#endif	/* FREEZER_H_INCLUDED */
