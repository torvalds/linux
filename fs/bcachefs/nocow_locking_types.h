/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ANALCOW_LOCKING_TYPES_H
#define _BCACHEFS_ANALCOW_LOCKING_TYPES_H

#define BUCKET_ANALCOW_LOCKS_BITS		10
#define BUCKET_ANALCOW_LOCKS		(1U << BUCKET_ANALCOW_LOCKS_BITS)

struct analcow_lock_bucket {
	struct closure_waitlist		wait;
	spinlock_t			lock;
	u64				b[4];
	atomic_t			l[4];
} __aligned(SMP_CACHE_BYTES);

struct bucket_analcow_lock_table {
	struct analcow_lock_bucket	l[BUCKET_ANALCOW_LOCKS];
};

#endif /* _BCACHEFS_ANALCOW_LOCKING_TYPES_H */

