#ifndef _SPARC64_SEMAPHORE_H
#define _SPARC64_SEMAPHORE_H

/* These are actually reasonable on the V9.
 *
 * See asm-ppc/semaphore.h for implementation commentary,
 * only sparc64 specific issues are commented here.
 */
#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/system.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	atomic_t count;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name, count) \
	{ ATOMIC_INIT(count), \
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name, 1)

#define __DECLARE_SEMAPHORE_GENERIC(name, count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC(name, 1)
#define DECLARE_MUTEX_LOCKED(name)	__DECLARE_SEMAPHORE_GENERIC(name, 0)

static inline void sema_init (struct semaphore *sem, int val)
{
	atomic_set(&sem->count, val);
	init_waitqueue_head(&sem->wait);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

extern void up(struct semaphore *sem);
extern void down(struct semaphore *sem);
extern int down_trylock(struct semaphore *sem);
extern int down_interruptible(struct semaphore *sem);

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_SEMAPHORE_H) */
