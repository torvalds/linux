/*
 * SMP- and interrupt-safe semaphores.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * Based on include/asm-i386/semaphore.h
 *   Copyright (C) 1996 Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_SEMAPHORE_H
#define __ASM_AVR32_SEMAPHORE_H

#include <linux/linkage.h>

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
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void sema_init (struct semaphore *sem, int val)
{
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

void __down(struct semaphore * sem);
int  __down_interruptible(struct semaphore * sem);
void __up(struct semaphore * sem);

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/kernel/semaphore.c
 */
static inline void down(struct semaphore * sem)
{
	might_sleep();
	if (unlikely(atomic_dec_return (&sem->count) < 0))
		__down (sem);
}

/*
 * Interruptible try to acquire a semaphore.  If we obtained
 * it, return zero.  If we were interrupted, returns -EINTR
 */
static inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

	might_sleep();
	if (unlikely(atomic_dec_return (&sem->count) < 0))
		ret = __down_interruptible (sem);
	return ret;
}

/*
 * Non-blockingly attempt to down() a semaphore.
 * Returns zero if we acquired it
 */
static inline int down_trylock(struct semaphore * sem)
{
	return atomic_dec_if_positive(&sem->count) < 0;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore * sem)
{
	if (unlikely(atomic_inc_return (&sem->count) <= 0))
		__up (sem);
}

#endif /*__ASM_AVR32_SEMAPHORE_H */
