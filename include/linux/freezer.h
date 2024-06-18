/* SPDX-License-Identifier: GPL-2.0 */
/* Freezer declarations */

#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED

#include <linux/debug_locks.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/jump_label.h>

#ifdef CONFIG_FREEZER
DECLARE_STATIC_KEY_FALSE(freezer_active);

extern bool pm_freezing;		/* PM freezing in effect */
extern bool pm_nosig_freezing;		/* PM nosig freezing in effect */

/*
 * Timeout for stopping processes
 */
extern unsigned int freeze_timeout_msecs;

/*
 * Check if a process has been frozen
 */
extern bool frozen(struct task_struct *p);

extern bool freezing_slow_path(struct task_struct *p);

/*
 * Check if there is a request to freeze a process
 */
static inline bool freezing(struct task_struct *p)
{
	if (static_branch_unlikely(&freezer_active))
		return freezing_slow_path(p);

	return false;
}

/* Takes and releases task alloc lock using task_lock() */
extern void __thaw_task(struct task_struct *t);

extern bool __refrigerator(bool check_kthr_stop);
extern int freeze_processes(void);
extern int freeze_kernel_threads(void);
extern void thaw_processes(void);
extern void thaw_kernel_threads(void);

static inline bool try_to_freeze(void)
{
	might_sleep();
	if (likely(!freezing(current)))
		return false;
	if (!(current->flags & PF_NOFREEZE))
		debug_check_no_locks_held();
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

#else /* !CONFIG_FREEZER */
static inline bool frozen(struct task_struct *p) { return false; }
static inline bool freezing(struct task_struct *p) { return false; }
static inline void __thaw_task(struct task_struct *t) {}

static inline bool __refrigerator(bool check_kthr_stop) { return false; }
static inline int freeze_processes(void) { return -ENOSYS; }
static inline int freeze_kernel_threads(void) { return -ENOSYS; }
static inline void thaw_processes(void) {}
static inline void thaw_kernel_threads(void) {}

static inline bool try_to_freeze(void) { return false; }

static inline void set_freezable(void) {}

#endif /* !CONFIG_FREEZER */

#endif	/* FREEZER_H_INCLUDED */
