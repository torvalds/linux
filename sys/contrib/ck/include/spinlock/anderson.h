/*
 * Copyright 2010-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_SPINLOCK_ANDERSON_H
#define CK_SPINLOCK_ANDERSON_H

#include <ck_cc.h>
#include <ck_limits.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdbool.h>

#ifndef CK_F_SPINLOCK_ANDERSON
#define CK_F_SPINLOCK_ANDERSON
/*
 * This is an implementation of Anderson's array-based queuing lock.
 */
struct ck_spinlock_anderson_thread {
	unsigned int locked;
	unsigned int position;
};
typedef struct ck_spinlock_anderson_thread ck_spinlock_anderson_thread_t;

struct ck_spinlock_anderson {
	struct ck_spinlock_anderson_thread *slots;
	unsigned int count;
	unsigned int wrap;
	unsigned int mask;
	char pad[CK_MD_CACHELINE - sizeof(unsigned int) * 3 - sizeof(void *)];
	unsigned int next;
};
typedef struct ck_spinlock_anderson ck_spinlock_anderson_t;

CK_CC_INLINE static void
ck_spinlock_anderson_init(struct ck_spinlock_anderson *lock,
    struct ck_spinlock_anderson_thread *slots,
    unsigned int count)
{
	unsigned int i;

	slots[0].locked = false;
	slots[0].position = 0;
	for (i = 1; i < count; i++) {
		slots[i].locked = true;
		slots[i].position = i;
	}

	lock->slots = slots;
	lock->count = count;
	lock->mask = count - 1;
	lock->next = 0;

	/*
	 * If the number of threads is not a power of two then compute
	 * appropriate wrap-around value in the case of next slot counter
	 * overflow.
	 */
	if (count & (count - 1))
		lock->wrap = (UINT_MAX % count) + 1;
	else
		lock->wrap = 0;

	ck_pr_barrier();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_anderson_locked(struct ck_spinlock_anderson *lock)
{
	unsigned int position;
	bool r;

	position = ck_pr_load_uint(&lock->next) & lock->mask;
	r = ck_pr_load_uint(&lock->slots[position].locked);
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_spinlock_anderson_lock(struct ck_spinlock_anderson *lock,
    struct ck_spinlock_anderson_thread **slot)
{
	unsigned int position, next;
	unsigned int count = lock->count;

	/*
	 * If count is not a power of 2, then it is possible for an overflow
	 * to reallocate beginning slots to more than one thread. To avoid this
	 * use a compare-and-swap.
	 */
	if (lock->wrap != 0) {
		position = ck_pr_load_uint(&lock->next);

		do {
			if (position == UINT_MAX)
				next = lock->wrap;
			else
				next = position + 1;
		} while (ck_pr_cas_uint_value(&lock->next, position,
					      next, &position) == false);

		position %= count;
	} else {
		position = ck_pr_faa_uint(&lock->next, 1);
		position &= lock->mask;
	}

	/* Serialize with respect to previous thread's store. */
	ck_pr_fence_load();

	/*
	 * Spin until slot is marked as unlocked. First slot is initialized to
	 * false.
	 */
	while (ck_pr_load_uint(&lock->slots[position].locked) == true)
		ck_pr_stall();

	/* Prepare slot for potential re-use by another thread. */
	ck_pr_store_uint(&lock->slots[position].locked, true);
	ck_pr_fence_lock();

	*slot = lock->slots + position;
	return;
}

CK_CC_INLINE static void
ck_spinlock_anderson_unlock(struct ck_spinlock_anderson *lock,
    struct ck_spinlock_anderson_thread *slot)
{
	unsigned int position;

	ck_pr_fence_unlock();

	/* Mark next slot as available. */
	if (lock->wrap == 0)
		position = (slot->position + 1) & lock->mask;
	else
		position = (slot->position + 1) % lock->count;

	ck_pr_store_uint(&lock->slots[position].locked, false);
	return;
}
#endif /* CK_F_SPINLOCK_ANDERSON */
#endif /* CK_SPINLOCK_ANDERSON_H */
