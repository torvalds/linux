// SPDX-License-Identifier: GPL-2.0

#include "two_state_shared_lock.h"

void __bch2_two_state_lock(two_state_lock_t *lock, int s)
{
	__wait_event(lock->wait, bch2_two_state_trylock(lock, s));
}
