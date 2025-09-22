/*	$OpenBSD: lock_machdep.c,v 1.18 2025/07/15 12:28:05 claudio Exp $	*/

/*
 * Copyright (c) 2007 Artur Grabowski <art@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/witness.h>
#include <sys/_lock.h>

#include <machine/atomic.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/cpu.h>

#include <ddb/db_output.h>

static __inline int
__cpu_cas(struct __mp_lock *mpl, volatile unsigned long *addr,
    unsigned long old, unsigned long new)
{
	volatile int *lock = (int *)(((vaddr_t)mpl->mpl_lock + 0xf) & ~0xf);
	volatile register_t old_lock = 0;
	int ret = 1;

	/* Note: lock must be 16-byte aligned. */
	asm volatile (
		"ldcws      0(%2), %0"
		: "=&r" (old_lock), "+m" (lock)
		: "r" (lock)
	);

	if (old_lock == MPL_UNLOCKED) {
		if (*addr == old) {
			*addr = new;
			asm("sync" ::: "memory");
			ret = 0;
		}
		*lock = MPL_UNLOCKED;
	}

	return ret;
}

void
___mp_lock_init(struct __mp_lock *lock)
{
	lock->mpl_lock[0] = MPL_UNLOCKED;
	lock->mpl_lock[1] = MPL_UNLOCKED;
	lock->mpl_lock[2] = MPL_UNLOCKED;
	lock->mpl_lock[3] = MPL_UNLOCKED;
	lock->mpl_cpu = NULL;
	lock->mpl_count = 0;
}

#if defined(MP_LOCKDEBUG)
#ifndef DDB
#error "MP_LOCKDEBUG requires DDB"
#endif
#endif

static __inline void
__mp_lock_spin(struct __mp_lock *mpl)
{
#ifndef MP_LOCKDEBUG
	while (mpl->mpl_count != 0)
		CPU_BUSY_CYCLE();
#else
	long nticks = __mp_lock_spinout;

	while (mpl->mpl_count != 0) {
		CPU_BUSY_CYCLE();

		if (--nticks <= 0) {
			db_printf("__mp_lock(%p): lock spun out", mpl);
			db_enter();
			nticks = __mp_lock_spinout;
		}
	}
#endif
}

void
__mp_lock(struct __mp_lock *mpl)
{
	int s;

#ifdef WITNESS
	if (!__mp_lock_held(mpl, curcpu()))
		WITNESS_CHECKORDER(&mpl->mpl_lock_obj,
		    LOP_EXCLUSIVE | LOP_NEWORDER, NULL);
#endif

	/*
	 * Please notice that mpl_count gets incremented twice for the
	 * first lock. This is on purpose. The way we release the lock
	 * in mp_unlock is to decrement the mpl_count and then check if
	 * the lock should be released. Since mpl_count is what we're
	 * spinning on, decrementing it in mpl_unlock to 0 means that
	 * we can't clear mpl_cpu, because we're no longer holding the
	 * lock. In theory mpl_cpu doesn't need to be cleared, but it's
	 * safer to clear it and besides, setting mpl_count to 2 on the
	 * first lock makes most of this code much simpler.
	 */

	while (1) {
		s = hppa_intr_disable();
		if (__cpu_cas(mpl, &mpl->mpl_count, 0, 1) == 0) {
			__asm volatile("sync" ::: "memory");
			mpl->mpl_cpu = curcpu();
		}
		if (mpl->mpl_cpu == curcpu()) {
			mpl->mpl_count++;
			hppa_intr_enable(s);
			break;
		}
		hppa_intr_enable(s);

		__mp_lock_spin(mpl);
	}

	WITNESS_LOCK(&mpl->mpl_lock_obj, LOP_EXCLUSIVE);
}

void
__mp_unlock(struct __mp_lock *mpl)
{
	int s;

#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__mp_unlock(%p): lock not held - %p != %p\n",
		    mpl, mpl->mpl_cpu, curcpu());
		db_enter();
	}
#endif

	WITNESS_UNLOCK(&mpl->mpl_lock_obj, LOP_EXCLUSIVE);

	s = hppa_intr_disable();
	if (--mpl->mpl_count == 1) {
		mpl->mpl_cpu = NULL;
		__asm volatile("sync" ::: "memory");
		mpl->mpl_count = 0;
	}
	hppa_intr_enable(s);
}

int
__mp_release_all(struct __mp_lock *mpl)
{
	int rv = mpl->mpl_count - 1;
	int s;
#ifdef WITNESS
	int i;
#endif

#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__mp_release_all(%p): lock not held - %p != %p\n",
		    mpl, mpl->mpl_cpu, curcpu());
		db_enter();
	}
#endif

#ifdef WITNESS
	for (i = 0; i < rv; i++)
		WITNESS_UNLOCK(&mpl->mpl_lock_obj, LOP_EXCLUSIVE);
#endif

	s = hppa_intr_disable();
	mpl->mpl_cpu = NULL;
	__asm volatile("sync" ::: "memory");
	mpl->mpl_count = 0;
	hppa_intr_enable(s);

	return (rv);
}

void
__mp_acquire_count(struct __mp_lock *mpl, int count)
{
	while (count--)
		__mp_lock(mpl);
}

int
__mp_lock_held(struct __mp_lock *mpl, struct cpu_info *ci)
{
	return mpl->mpl_cpu == ci;
}
