/* $Id: semaphore.h,v 1.3 2001/05/08 13:54:09 bjornw Exp $ */

/* On the i386 these are coded in asm, perhaps we should as well. Later.. */

#ifndef _CRIS_SEMAPHORE_H
#define _CRIS_SEMAPHORE_H

#define RW_LOCK_BIAS             0x01000000

#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#include <asm/system.h>
#include <asm/atomic.h>

/*
 * CRIS semaphores, implemented in C-only so far. 
 */

struct semaphore {
	atomic_t count;
	atomic_t waking;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.waking		= ATOMIC_INIT(0),				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)    \
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
        struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void sema_init(struct semaphore *sem, int val)
{
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
}

static inline void init_MUTEX (struct semaphore *sem)
{
        sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
        sema_init(sem, 0);
}

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern int __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/* notice - we probably can do cli/sti here instead of saving */

static inline void down(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

	might_sleep();

	/* atomically decrement the semaphores count, and if its negative, we wait */
	cris_atomic_save(sem, flags);
	failed = --(sem->count.counter) < 0;
	cris_atomic_restore(sem, flags);
	if(failed) {
		__down(sem);
	}
}

/*
 * This version waits in interruptible state so that the waiting
 * process can be killed.  The down_interruptible routine
 * returns negative for signalled and zero for semaphore acquired.
 */

static inline int down_interruptible(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

	might_sleep();

	/* atomically decrement the semaphores count, and if its negative, we wait */
	cris_atomic_save(sem, flags);
	failed = --(sem->count.counter) < 0;
	cris_atomic_restore(sem, flags);
	if(failed)
		failed = __down_interruptible(sem);
	return(failed);
}

static inline int down_trylock(struct semaphore * sem)
{
	unsigned long flags;
	int failed;

	cris_atomic_save(sem, flags);
	failed = --(sem->count.counter) < 0;
	cris_atomic_restore(sem, flags);
	if(failed)
		failed = __down_trylock(sem);
	return(failed);

}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore * sem)
{  
	unsigned long flags;
	int wakeup;

	/* atomically increment the semaphores count, and if it was negative, we wake people */
	cris_atomic_save(sem, flags);
	wakeup = ++(sem->count.counter) <= 0;
	cris_atomic_restore(sem, flags);
	if(wakeup) {
		__up(sem);
	}
}

#endif
