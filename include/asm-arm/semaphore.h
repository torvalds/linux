/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_ARM_SEMAPHORE_H
#define __ASM_ARM_SEMAPHORE_H

#include <linux/linkage.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

#include <asm/atomic.h>
#include <asm/locks.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INIT(name, cnt)				\
{								\
	.count	= ATOMIC_INIT(cnt),				\
	.wait	= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count)	\
	struct semaphore name = __SEMAPHORE_INIT(name,count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name)	__DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init(struct semaphore *sem, int val)
{
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
}

static inline void init_MUTEX(struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED(struct semaphore *sem)
{
	sema_init(sem, 0);
}

/*
 * special register calling convention
 */
asmlinkage void __down_failed(void);
asmlinkage int  __down_interruptible_failed(void);
asmlinkage int  __down_trylock_failed(void);
asmlinkage void __up_wakeup(void);

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/*
 * This is ugly, but we want the default case to fall through.
 * "__down" is the actual routine that waits...
 */
static inline void down(struct semaphore * sem)
{
	might_sleep();
	__down_op(sem, __down_failed);
}

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_interruptible" is the actual routine that waits...
 */
static inline int down_interruptible (struct semaphore * sem)
{
	might_sleep();
	return __down_op_ret(sem, __down_interruptible_failed);
}

static inline int down_trylock(struct semaphore *sem)
{
	return __down_op_ret(sem, __down_trylock_failed);
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore * sem)
{
	__up_op(sem, __up_wakeup);
}

#endif
