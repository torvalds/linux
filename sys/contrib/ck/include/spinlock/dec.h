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

#ifndef CK_SPINLOCK_DEC_H
#define CK_SPINLOCK_DEC_H

#include <ck_backoff.h>
#include <ck_cc.h>
#include <ck_elide.h>
#include <ck_pr.h>
#include <ck_stdbool.h>

#ifndef CK_F_SPINLOCK_DEC
#define CK_F_SPINLOCK_DEC
/*
 * This is similar to the CACAS lock but makes use of an atomic decrement
 * operation to check if the lock value was decremented to 0 from 1. The
 * idea is that a decrement operation is cheaper than a compare-and-swap.
 */
struct ck_spinlock_dec {
	unsigned int value;
};
typedef struct ck_spinlock_dec ck_spinlock_dec_t;

#define CK_SPINLOCK_DEC_INITIALIZER	{1}

CK_CC_INLINE static void
ck_spinlock_dec_init(struct ck_spinlock_dec *lock)
{

	lock->value = 1;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_dec_trylock(struct ck_spinlock_dec *lock)
{
	unsigned int value;

	value = ck_pr_fas_uint(&lock->value, 0);
	ck_pr_fence_lock();
	return value == 1;
}

CK_CC_INLINE static bool
ck_spinlock_dec_locked(struct ck_spinlock_dec *lock)
{
	bool r;

	r = ck_pr_load_uint(&lock->value) != 1;
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_spinlock_dec_lock(struct ck_spinlock_dec *lock)
{
	bool r;

	for (;;) {
		/*
		 * Only one thread is guaranteed to decrement lock to 0.
		 * Overflow must be protected against. No more than
		 * UINT_MAX lock requests can happen while the lock is held.
		 */
		ck_pr_dec_uint_zero(&lock->value, &r);
		if (r == true)
			break;

		/* Load value without generating write cycles. */
		while (ck_pr_load_uint(&lock->value) != 1)
			ck_pr_stall();
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_dec_lock_eb(struct ck_spinlock_dec *lock)
{
	ck_backoff_t backoff = CK_BACKOFF_INITIALIZER;
	bool r;

	for (;;) {
		ck_pr_dec_uint_zero(&lock->value, &r);
		if (r == true)
			break;

		while (ck_pr_load_uint(&lock->value) != 1)
			ck_backoff_eb(&backoff);
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_dec_unlock(struct ck_spinlock_dec *lock)
{

	ck_pr_fence_unlock();

	/*
	 * Unconditionally set lock value to 1 so someone can decrement lock
	 * to 0.
	 */
	ck_pr_store_uint(&lock->value, 1);
	return;
}

CK_ELIDE_PROTOTYPE(ck_spinlock_dec, ck_spinlock_dec_t,
    ck_spinlock_dec_locked, ck_spinlock_dec_lock,
    ck_spinlock_dec_locked, ck_spinlock_dec_unlock)

CK_ELIDE_TRYLOCK_PROTOTYPE(ck_spinlock_dec, ck_spinlock_dec_t,
    ck_spinlock_dec_locked, ck_spinlock_dec_trylock)

#endif /* CK_F_SPINLOCK_DEC */
#endif /* CK_SPINLOCK_DEC_H */
