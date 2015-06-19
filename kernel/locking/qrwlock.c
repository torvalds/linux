/*
 * Queued read/write locks
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
#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <asm/qrwlock.h>

/*
 * This internal data structure is used for optimizing access to some of
 * the subfields within the atomic_t cnts.
 */
struct __qrwlock {
	union {
		atomic_t cnts;
		struct {
#ifdef __LITTLE_ENDIAN
			u8 wmode;	/* Writer mode   */
			u8 rcnts[3];	/* Reader counts */
#else
			u8 rcnts[3];	/* Reader counts */
			u8 wmode;	/* Writer mode   */
#endif
		};
	};
	arch_spinlock_t	lock;
};

/**
 * rspin_until_writer_unlock - inc reader count & spin until writer is gone
 * @lock  : Pointer to queue rwlock structure
 * @writer: Current queue rwlock writer status byte
 *
 * In interrupt context or at the head of the queue, the reader will just
 * increment the reader count & wait until the writer releases the lock.
 */
static __always_inline void
rspin_until_writer_unlock(struct qrwlock *lock, u32 cnts)
{
	while ((cnts & _QW_WMASK) == _QW_LOCKED) {
		cpu_relax_lowlatency();
		cnts = smp_load_acquire((u32 *)&lock->cnts);
	}
}

/**
 * queued_read_lock_slowpath - acquire read lock of a queue rwlock
 * @lock: Pointer to queue rwlock structure
 * @cnts: Current qrwlock lock value
 */
void queued_read_lock_slowpath(struct qrwlock *lock, u32 cnts)
{
	/*
	 * Readers come here when they cannot get the lock without waiting
	 */
	if (unlikely(in_interrupt())) {
		/*
		 * Readers in interrupt context will get the lock immediately
		 * if the writer is just waiting (not holding the lock yet).
		 * The rspin_until_writer_unlock() function returns immediately
		 * in this case. Otherwise, they will spin until the lock
		 * is available without waiting in the queue.
		 */
		rspin_until_writer_unlock(lock, cnts);
		return;
	}
	atomic_sub(_QR_BIAS, &lock->cnts);

	/*
	 * Put the reader into the wait queue
	 */
	arch_spin_lock(&lock->lock);

	/*
	 * At the head of the wait queue now, wait until the writer state
	 * goes to 0 and then try to increment the reader count and get
	 * the lock. It is possible that an incoming writer may steal the
	 * lock in the interim, so it is necessary to check the writer byte
	 * to make sure that the write lock isn't taken.
	 */
	while (atomic_read(&lock->cnts) & _QW_WMASK)
		cpu_relax_lowlatency();

	cnts = atomic_add_return(_QR_BIAS, &lock->cnts) - _QR_BIAS;
	rspin_until_writer_unlock(lock, cnts);

	/*
	 * Signal the next one in queue to become queue head
	 */
	arch_spin_unlock(&lock->lock);
}
EXPORT_SYMBOL(queued_read_lock_slowpath);

/**
 * queued_write_lock_slowpath - acquire write lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure
 */
void queued_write_lock_slowpath(struct qrwlock *lock)
{
	u32 cnts;

	/* Put the writer into the wait queue */
	arch_spin_lock(&lock->lock);

	/* Try to acquire the lock directly if no reader is present */
	if (!atomic_read(&lock->cnts) &&
	    (atomic_cmpxchg(&lock->cnts, 0, _QW_LOCKED) == 0))
		goto unlock;

	/*
	 * Set the waiting flag to notify readers that a writer is pending,
	 * or wait for a previous writer to go away.
	 */
	for (;;) {
		struct __qrwlock *l = (struct __qrwlock *)lock;

		if (!READ_ONCE(l->wmode) &&
		   (cmpxchg(&l->wmode, 0, _QW_WAITING) == 0))
			break;

		cpu_relax_lowlatency();
	}

	/* When no more readers, set the locked flag */
	for (;;) {
		cnts = atomic_read(&lock->cnts);
		if ((cnts == _QW_WAITING) &&
		    (atomic_cmpxchg(&lock->cnts, _QW_WAITING,
				    _QW_LOCKED) == _QW_WAITING))
			break;

		cpu_relax_lowlatency();
	}
unlock:
	arch_spin_unlock(&lock->lock);
}
EXPORT_SYMBOL(queued_write_lock_slowpath);
