/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_TWO_STATE_LOCK_H
#define _BCACHEFS_TWO_STATE_LOCK_H

#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "util.h"

/*
 * Two-state lock - can be taken for add or block - both states are shared,
 * like read side of rwsem, but conflict with other state:
 */
typedef struct {
	atomic_long_t		v;
	wait_queue_head_t	wait;
} two_state_lock_t;

static inline void two_state_lock_init(two_state_lock_t *lock)
{
	atomic_long_set(&lock->v, 0);
	init_waitqueue_head(&lock->wait);
}

static inline void bch2_two_state_unlock(two_state_lock_t *lock, int s)
{
	long i = s ? 1 : -1;

	EBUG_ON(atomic_long_read(&lock->v) == 0);

	if (atomic_long_sub_return_release(i, &lock->v) == 0)
		wake_up_all(&lock->wait);
}

static inline bool bch2_two_state_trylock(two_state_lock_t *lock, int s)
{
	long i = s ? 1 : -1;
	long old;

	old = atomic_long_read(&lock->v);
	do {
		if (i > 0 ? old < 0 : old > 0)
			return false;
	} while (!atomic_long_try_cmpxchg_acquire(&lock->v, &old, old + i));

	return true;
}

void __bch2_two_state_lock(two_state_lock_t *, int);

static inline void bch2_two_state_lock(two_state_lock_t *lock, int s)
{
	if (!bch2_two_state_trylock(lock, s))
		__bch2_two_state_lock(lock, s);
}

#endif /* _BCACHEFS_TWO_STATE_LOCK_H */
