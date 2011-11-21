#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#ifdef CONFIG_LOCK_KERNEL
#include <linux/sched.h>

#define kernel_locked()		(current->lock_depth >= 0)

extern int __lockfunc __reacquire_kernel_lock(void);
extern void __lockfunc __release_kernel_lock(void);

/*
 * Release/re-acquire global kernel lock for the scheduler
 */
#define release_kernel_lock(tsk) do { 		\
	if (unlikely((tsk)->lock_depth >= 0))	\
		__release_kernel_lock();	\
} while (0)

static inline int reacquire_kernel_lock(struct task_struct *task)
{
	if (unlikely(task->lock_depth >= 0))
		return __reacquire_kernel_lock();
	return 0;
}

extern void __lockfunc lock_kernel(void)	__acquires(kernel_lock);
extern void __lockfunc unlock_kernel(void)	__releases(kernel_lock);

/*
 * Various legacy drivers don't really need the BKL in a specific
 * function, but they *do* need to know that the BKL became available.
 * This function just avoids wrapping a bunch of lock/unlock pairs
 * around code which doesn't really need it.
 */
static inline void cycle_kernel_lock(void)
{
	lock_kernel();
	unlock_kernel();
}

#else

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task)		do { } while(0)
#define cycle_kernel_lock()			do { } while(0)
#define reacquire_kernel_lock(task)		0
#define kernel_locked()				1

#endif /* CONFIG_LOCK_KERNEL */
#endif /* __LINUX_SMPLOCK_H */
