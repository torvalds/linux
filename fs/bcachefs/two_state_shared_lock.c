// SPDX-License-Identifier: GPL-2.0

#include "two_state_shared_lock.h"

void bch2_two_state_unlock(two_state_lock_t *lock, int s)
{
	long i = s ? 1 : -1;

	BUG_ON(atomic_long_read(&lock->v) == 0);

	if (atomic_long_sub_return_release(i, &lock->v) == 0)
		wake_up_all(&lock->wait);
}

bool bch2_two_state_trylock(two_state_lock_t *lock, int s)
{
	long i = s ? 1 : -1;
	long v = atomic_long_read(&lock->v), old;

	do {
		old = v;

		if (i > 0 ? v < 0 : v > 0)
			return false;
	} while ((v = atomic_long_cmpxchg_acquire(&lock->v,
					old, old + i)) != old);
	return true;
}

void bch2_two_state_lock(two_state_lock_t *lock, int s)
{
	wait_event(lock->wait, bch2_two_state_trylock(lock, s));
}
