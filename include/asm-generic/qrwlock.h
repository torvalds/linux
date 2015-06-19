/*
 * Queue read/write lock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * (C) Copyright 2013-2014 Hewlett-Packard Development Company, L.P.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 */
#ifndef __ASM_GENERIC_QRWLOCK_H
#define __ASM_GENERIC_QRWLOCK_H

#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/processor.h>

#include <asm-generic/qrwlock_types.h>

/*
 * Writer states & reader shift and bias
 */
#define	_QW_WAITING	1		/* A writer is waiting	   */
#define	_QW_LOCKED	0xff		/* A writer holds the lock */
#define	_QW_WMASK	0xff		/* Writer mask		   */
#define	_QR_SHIFT	8		/* Reader count shift	   */
#define _QR_BIAS	(1U << _QR_SHIFT)

/*
 * External function declarations
 */
extern void queued_read_lock_slowpath(struct qrwlock *lock, u32 cnts);
extern void queued_write_lock_slowpath(struct qrwlock *lock);

/**
 * queued_read_can_lock- would read_trylock() succeed?
 * @lock: Pointer to queue rwlock structure
 */
static inline int queued_read_can_lock(struct qrwlock *lock)
{
	return !(atomic_read(&lock->cnts) & _QW_WMASK);
}

/**
 * queued_write_can_lock- would write_trylock() succeed?
 * @lock: Pointer to queue rwlock structure
 */
static inline int queued_write_can_lock(struct qrwlock *lock)
{
	return !atomic_read(&lock->cnts);
}

/**
 * queued_read_trylock - try to acquire read lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 * Return: 1 if lock acquired, 0 if failed
 */
static inline int queued_read_trylock(struct qrwlock *lock)
{
	u32 cnts;

	cnts = atomic_read(&lock->cnts);
	if (likely(!(cnts & _QW_WMASK))) {
		cnts = (u32)atomic_add_return(_QR_BIAS, &lock->cnts);
		if (likely(!(cnts & _QW_WMASK)))
			return 1;
		atomic_sub(_QR_BIAS, &lock->cnts);
	}
	return 0;
}

/**
 * queued_write_trylock - try to acquire write lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 * Return: 1 if lock acquired, 0 if failed
 */
static inline int queued_write_trylock(struct qrwlock *lock)
{
	u32 cnts;

	cnts = atomic_read(&lock->cnts);
	if (unlikely(cnts))
		return 0;

	return likely(atomic_cmpxchg(&lock->cnts,
				     cnts, cnts | _QW_LOCKED) == cnts);
}
/**
 * queued_read_lock - acquire read lock of a queue rwlock
 * @lock: Pointer to queue rwlock structure
 */
static inline void queued_read_lock(struct qrwlock *lock)
{
	u32 cnts;

	cnts = atomic_add_return(_QR_BIAS, &lock->cnts);
	if (likely(!(cnts & _QW_WMASK)))
		return;

	/* The slowpath will decrement the reader count, if necessary. */
	queued_read_lock_slowpath(lock, cnts);
}

/**
 * queued_write_lock - acquire write lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 */
static inline void queued_write_lock(struct qrwlock *lock)
{
	/* Optimize for the unfair lock case where the fair flag is 0. */
	if (atomic_cmpxchg(&lock->cnts, 0, _QW_LOCKED) == 0)
		return;

	queued_write_lock_slowpath(lock);
}

/**
 * queued_read_unlock - release read lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 */
static inline void queued_read_unlock(struct qrwlock *lock)
{
	/*
	 * Atomically decrement the reader count
	 */
	smp_mb__before_atomic();
	atomic_sub(_QR_BIAS, &lock->cnts);
}

#ifndef queued_write_unlock
/**
 * queued_write_unlock - release write lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 */
static inline void queued_write_unlock(struct qrwlock *lock)
{
	/*
	 * If the writer field is atomic, it can be cleared directly.
	 * Otherwise, an atomic subtraction will be used to clear it.
	 */
	smp_mb__before_atomic();
	atomic_sub(_QW_LOCKED, &lock->cnts);
}
#endif

/*
 * Remapping rwlock architecture specific functions to the corresponding
 * queue rwlock functions.
 */
#define arch_read_can_lock(l)	queued_read_can_lock(l)
#define arch_write_can_lock(l)	queued_write_can_lock(l)
#define arch_read_lock(l)	queued_read_lock(l)
#define arch_write_lock(l)	queued_write_lock(l)
#define arch_read_trylock(l)	queued_read_trylock(l)
#define arch_write_trylock(l)	queued_write_trylock(l)
#define arch_read_unlock(l)	queued_read_unlock(l)
#define arch_write_unlock(l)	queued_write_unlock(l)

#endif /* __ASM_GENERIC_QRWLOCK_H */
