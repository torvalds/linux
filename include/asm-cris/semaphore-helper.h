/* $Id: semaphore-helper.h,v 1.3 2001/03/26 15:00:33 orjanf Exp $
 *
 * SMP- and interrupt-safe semaphores helper functions. Generic versions, no
 * optimizations whatsoever... 
 *
 */

#ifndef _ASM_SEMAPHORE_HELPER_H
#define _ASM_SEMAPHORE_HELPER_H

#include <asm/atomic.h>
#include <linux/errno.h>

#define read(a) ((a)->counter)
#define inc(a) (((a)->counter)++)
#define dec(a) (((a)->counter)--)

#define count_inc(a) ((*(a))++)

/*
 * These two _must_ execute atomically wrt each other.
 */
extern inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

extern inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	local_save_flags(flags);
	local_irq_disable();
	if (read(&sem->waking) > 0) {
		dec(&sem->waking);
		ret = 1;
	}
	local_irq_restore(flags);
	return ret;
}

extern inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret = 0;
	unsigned long flags;

	local_save_flags(flags);
	local_irq_disable();
	if (read(&sem->waking) > 0) {
		dec(&sem->waking);
		ret = 1;
	} else if (signal_pending(tsk)) {
		inc(&sem->count);
		ret = -EINTR;
	}
	local_irq_restore(flags);
	return ret;
}

extern inline int waking_non_zero_trylock(struct semaphore *sem)
{
        int ret = 1;
	unsigned long flags;

	local_save_flags(flags);
	local_irq_disable();
	if (read(&sem->waking) <= 0)
		inc(&sem->count);
	else {
		dec(&sem->waking);
		ret = 0;
	}
	local_irq_restore(flags);
	return ret;
}

#endif /* _ASM_SEMAPHORE_HELPER_H */


