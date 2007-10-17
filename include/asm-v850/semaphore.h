#ifndef __V850_SEMAPHORE_H__
#define __V850_SEMAPHORE_H__

#include <linux/linkage.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name,count)				      \
	{ ATOMIC_INIT (count), 0,					      \
	  __WAIT_QUEUE_HEAD_INITIALIZER ((name).wait) }

#define __DECLARE_SEMAPHORE_GENERIC(name,count)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER (name,count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC (name,1)

static inline void sema_init (struct semaphore *sem, int val)
{
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init (sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init (sem, 0);
}

/*
 * special register calling convention
 */
asmlinkage void __down_failed (void);
asmlinkage int  __down_interruptible_failed (void);
asmlinkage int  __down_trylock_failed (void);
asmlinkage void __up_wakeup (void);

extern void __down (struct semaphore * sem);
extern int  __down_interruptible (struct semaphore * sem);
extern int  __down_trylock (struct semaphore * sem);
extern void __up (struct semaphore * sem);

static inline void down (struct semaphore * sem)
{
	might_sleep();
	if (atomic_dec_return (&sem->count) < 0)
		__down (sem);
}

static inline int down_interruptible (struct semaphore * sem)
{
	int ret = 0;
	might_sleep();
	if (atomic_dec_return (&sem->count) < 0)
		ret = __down_interruptible (sem);
	return ret;
}

static inline int down_trylock (struct semaphore *sem)
{
	int ret = 0;
	if (atomic_dec_return (&sem->count) < 0)
		ret = __down_trylock (sem);
	return ret;
}

static inline void up (struct semaphore * sem)
{
	if (atomic_inc_return (&sem->count) <= 0)
		__up (sem);
}

#endif /* __V850_SEMAPHORE_H__ */
