/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_TWO_STATE_LOCK_H
#define _BCACHEFS_TWO_STATE_LOCK_H

#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/wait.h>

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

void bch2_two_state_unlock(two_state_lock_t *, int);
bool bch2_two_state_trylock(two_state_lock_t *, int);
void bch2_two_state_lock(two_state_lock_t *, int);

#endif /* _BCACHEFS_TWO_STATE_LOCK_H */
