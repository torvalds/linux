#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	atomic24_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC24_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init (struct semaphore *sem, int val)
{
	atomic24_set(&sem->count, val);
	sem->sleepers = 0;
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

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern int __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

static inline void down(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

	might_sleep();

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_sub\n\t"
	" add	%%o7, 8, %%o7\n\t"
	"tst	%%g2\n\t"
	"bl	2f\n\t"
	" nop\n"
	"1:\n\t"
	".subsection 2\n"
	"2:\n\t"
	"save	%%sp, -64, %%sp\n\t"
	"mov	%%g1, %%l1\n\t"
	"mov	%%g5, %%l5\n\t"
	"call	%3\n\t"
	" mov	%%g1, %%o0\n\t"
	"mov	%%l1, %%g1\n\t"
	"ba	1b\n\t"
	" restore %%l5, %%g0, %%g5\n\t"
	".previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down)
	: "g3", "g4", "g7", "memory", "cc");
}

static inline int down_interruptible(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

	might_sleep();

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_sub\n\t"
	" add	%%o7, 8, %%o7\n\t"
	"tst	%%g2\n\t"
	"bl	2f\n\t"
	" clr	%%g2\n"
	"1:\n\t"
	".subsection 2\n"
	"2:\n\t"
	"save	%%sp, -64, %%sp\n\t"
	"mov	%%g1, %%l1\n\t"
	"mov	%%g5, %%l5\n\t"
	"call	%3\n\t"
	" mov	%%g1, %%o0\n\t"
	"mov	%%l1, %%g1\n\t"
	"mov	%%l5, %%g5\n\t"
	"ba	1b\n\t"
	" restore %%o0, %%g0, %%g2\n\t"
	".previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_interruptible)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

static inline int down_trylock(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_sub\n\t"
	" add	%%o7, 8, %%o7\n\t"
	"tst	%%g2\n\t"
	"bl	2f\n\t"
	" clr	%%g2\n"
	"1:\n\t"
	".subsection 2\n"
	"2:\n\t"
	"save	%%sp, -64, %%sp\n\t"
	"mov	%%g1, %%l1\n\t"
	"mov	%%g5, %%l5\n\t"
	"call	%3\n\t"
	" mov	%%g1, %%o0\n\t"
	"mov	%%l1, %%g1\n\t"
	"mov	%%l5, %%g5\n\t"
	"ba	1b\n\t"
	" restore %%o0, %%g0, %%g2\n\t"
	".previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__down_trylock)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

static inline void up(struct semaphore * sem)
{
	register volatile int *ptr asm("g1");
	register int increment asm("g2");

	ptr = &(sem->count.counter);
	increment = 1;

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___atomic24_add\n\t"
	" add	%%o7, 8, %%o7\n\t"
	"tst	%%g2\n\t"
	"ble	2f\n\t"
	" nop\n"
	"1:\n\t"
	".subsection 2\n"
	"2:\n\t"
	"save	%%sp, -64, %%sp\n\t"
	"mov	%%g1, %%l1\n\t"
	"mov	%%g5, %%l5\n\t"
	"call	%3\n\t"
	" mov	%%g1, %%o0\n\t"
	"mov	%%l1, %%g1\n\t"
	"ba	1b\n\t"
	" restore %%l5, %%g0, %%g5\n\t"
	".previous\n"
	: "=&r" (increment)
	: "0" (increment), "r" (ptr), "i" (__up)
	: "g3", "g4", "g7", "memory", "cc");
}	

#endif /* __KERNEL__ */

#endif /* !(_SPARC_SEMAPHORE_H) */
