/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_NOCOW_LOCKING_TYPES_H
#define _BCACHEFS_NOCOW_LOCKING_TYPES_H

#define BUCKET_NOCOW_LOCKS_BITS		10
#define BUCKET_NOCOW_LOCKS		(1U << BUCKET_NOCOW_LOCKS_BITS)

struct nocow_lock_bucket {
	struct closure_waitlist		wait;
	spinlock_t			lock;
	u64				b[4];
	atomic_t			l[4];
} __aligned(SMP_CACHE_BYTES);

struct bucket_nocow_lock_table {
	struct nocow_lock_bucket	l[BUCKET_NOCOW_LOCKS];
};

#endif /* _BCACHEFS_NOCOW_LOCKING_TYPES_H */

