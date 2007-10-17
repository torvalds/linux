#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef __KERNEL__

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 * Modified 1999-02-14 by Andrea Arcangeli, split the sched.c helper
 *		       functions in asm/sempahore-helper.h while fixing a
 *		       potential and subtle race discovered by Ulrich Schmid
 *		       in down_interruptible(). Since I started to play here I
 *		       also implemented the `trylock' semaphore operation.
 *          1999-07-02 Artur Skawina <skawina@geocities.com>
 *                     Optimized "0(ecx)" -> "(ecx)" (the assembler does not
 *                     do this). Changed calling sequences from push/jmp to
 *                     traditional call/ret.
 * Modified 2001-01-01 Andreas Franck <afranck@gmx.de>
 *		       Some hacks to ensure compatibility with recent
 *		       GCC snapshots, to avoid stack corruption when compiling
 *		       with -fomit-frame-pointer. It's not sure if this will
 *		       be fixed in GCC, as our previous implementation was a
 *		       bit dubious.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};


#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void sema_init (struct semaphore *sem, int val)
{
/*
 *	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesn't. Oh well.
 */
	atomic_set(&sem->count, val);
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

fastcall void __down_failed(void /* special register calling convention */);
fastcall int  __down_failed_interruptible(void  /* params in registers */);
fastcall int  __down_failed_trylock(void  /* params in registers */);
fastcall void __up_wakeup(void /* special register calling convention */);

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/kernel/semaphore.c
 */
static inline void down(struct semaphore * sem)
{
	might_sleep();
	__asm__ __volatile__(
		"# atomic down operation\n\t"
		LOCK_PREFIX "decl %0\n\t"     /* --sem->count */
		"jns 2f\n"
		"\tlea %0,%%eax\n\t"
		"call __down_failed\n"
		"2:"
		:"+m" (sem->count)
		:
		:"memory","ax");
}

/*
 * Interruptible try to acquire a semaphore.  If we obtained
 * it, return zero.  If we were interrupted, returns -EINTR
 */
static inline int down_interruptible(struct semaphore * sem)
{
	int result;

	might_sleep();
	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		"xorl %0,%0\n\t"
		LOCK_PREFIX "decl %1\n\t"     /* --sem->count */
		"jns 2f\n\t"
		"lea %1,%%eax\n\t"
		"call __down_failed_interruptible\n"
		"2:"
		:"=&a" (result), "+m" (sem->count)
		:
		:"memory");
	return result;
}

/*
 * Non-blockingly attempt to down() a semaphore.
 * Returns zero if we acquired it
 */
static inline int down_trylock(struct semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		"xorl %0,%0\n\t"
		LOCK_PREFIX "decl %1\n\t"     /* --sem->count */
		"jns 2f\n\t"
		"lea %1,%%eax\n\t"
		"call __down_failed_trylock\n\t"
		"2:\n"
		:"=&a" (result), "+m" (sem->count)
		:
		:"memory");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
static inline void up(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic up operation\n\t"
		LOCK_PREFIX "incl %0\n\t"     /* ++sem->count */
		"jg 1f\n\t"
		"lea %0,%%eax\n\t"
		"call __up_wakeup\n"
		"1:"
		:"+m" (sem->count)
		:
		:"memory","ax");
}

#endif
#endif
