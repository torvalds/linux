/*	$OpenBSD: lock_machdep.c,v 1.15 2025/07/15 12:28:05 claudio Exp $	*/

/*
 * Copyright (c) 2021 George Koehler <gkoehler@openbsd.org>
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
#include <sys/atomic.h>

#include <machine/cpu.h>

#include <ddb/db_output.h>

/*
 * If __ppc_lock() crosses a page boundary in the kernel text, then it
 * may cause a page fault (on G5 with ppc_nobat), and pte_spill_r()
 * would recursively call __ppc_lock().  The lock must be in a valid
 * state when the page fault happens.  We acquire or release the lock
 * with a 32-bit atomic write to mpl_owner, so the lock is always in a
 * valid state, before or after the write.
 *
 * Acquired the lock:	mpl->mpl_cpu == curcpu()
 * Released the lock:	mpl->mpl_cpu == NULL
 */

#if defined(MP_LOCKDEBUG)
#ifndef DDB
#error "MP_LOCKDEBUG requires DDB"
#endif
#endif

static __inline void
__ppc_lock_spin(struct __ppc_lock *mpl)
{
#ifndef MP_LOCKDEBUG
	while (mpl->mpl_cpu != NULL)
		CPU_BUSY_CYCLE();
#else
	long nticks = __mp_lock_spinout;

	while (mpl->mpl_cpu != NULL) {
		CPU_BUSY_CYCLE();

		if (nticks-- <= 0) {
			db_printf("__ppc_lock(%p): lock spun out\n", mpl);
			db_enter();
			nticks = __mp_lock_spinout;
		}
	}
#endif
}

void
__ppc_lock(struct __ppc_lock *mpl)
{
	/*
	 * Please notice that mpl_count stays at 0 for the first lock.
	 * A page fault might recursively call __ppc_lock() after we
	 * set mpl_cpu, but before we can increase mpl_count.
	 *
	 * After we acquire the lock, we need a "bc; isync" memory
	 * barrier, but we might not reach the barrier before the next
	 * page fault.  Then the fault's recursive __ppc_lock() must
	 * have a barrier.  membar_enter() is just "isync" and must
	 * come after a conditional branch for holding the lock.
	 */

	while (1) {
		struct cpu_info *owner = mpl->mpl_cpu;
		struct cpu_info *ci = curcpu();

		if (owner == NULL) {
			/* Try to acquire the lock. */
			if (atomic_cas_ptr(&mpl->mpl_cpu, NULL, ci) == NULL) {
				membar_enter();
				break;
			}
		} else if (owner == ci) {
			/* We hold the lock, but might need a barrier. */
			membar_enter();
			mpl->mpl_count++;
			break;
		}

		__ppc_lock_spin(mpl);
	}
}

void
__ppc_unlock(struct __ppc_lock *mpl)
{
#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__ppc_unlock(%p): not held lock\n", mpl);
		db_enter();
	}
#endif

	/*
	 * If we get a page fault after membar_exit() and before
	 * releasing the lock, then the recursive call to
	 * __ppc_unlock() must also membar_exit().
	 */
	if (mpl->mpl_count == 0) {
		membar_exit();
		mpl->mpl_cpu = NULL;	/* Release the lock. */
	} else {
		membar_exit();
		mpl->mpl_count--;
	}
}
