/*	$OpenBSD: mutex.c,v 1.3 2025/06/19 12:01:08 jca Exp $	*/

/*
 * Copyright (c) 2020 Miodrag Vallat
 * Copyright (c) 2017 Visa Hankala
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2004 Artur Grabowski <art@openbsd.org>
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
#include <sys/sched.h>
#include <sys/atomic.h>
#include <sys/witness.h>
#include <sys/mutex.h>

#include <ddb/db_output.h>

static inline int
atomic_swap(volatile int *lockptr, int new)
{
        int old = new;
        asm volatile
            ("xmem %0, %2, %%r0" : "+r"(old), "+m"(*lockptr) : "r"(lockptr));
	return old;
}

void
__mtx_init(struct mutex *mtx, int wantipl)
{
	mtx->mtx_lock = 0;
	mtx->mtx_owner = NULL;
	mtx->mtx_wantipl = wantipl;
	mtx->mtx_oldipl = IPL_NONE;
}

#ifdef MULTIPROCESSOR
void
mtx_enter(struct mutex *mtx)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
#ifdef MP_LOCKDEBUG
	long nticks = __mp_lock_spinout;
#endif

	WITNESS_CHECKORDER(MUTEX_LOCK_OBJECT(mtx),
	    LOP_EXCLUSIVE | LOP_NEWORDER, NULL);

	spc->spc_spinning++;
	while (mtx_enter_try(mtx) == 0) {
		CPU_BUSY_CYCLE();

#ifdef MP_LOCKDEBUG
		if (--nticks == 0) {
			db_printf("%s: %p lock spun out\n", __func__, mtx);
			db_enter();
			nticks = __mp_lock_spinout;
		}
#endif
	}
	spc->spc_spinning--;
}

int
mtx_enter_try(struct mutex *mtx)
{
	struct cpu_info *ci = curcpu();
	int s;

	/* Avoid deadlocks after panic or in DDB */
	if (panicstr || db_active)
		return (1);

	if (mtx->mtx_wantipl != IPL_NONE)
		s = splraise(mtx->mtx_wantipl);

	if (atomic_swap(&mtx->mtx_lock, 1) == 0) {
		mtx->mtx_owner = ci;
		membar_enter_after_atomic();
		if (mtx->mtx_wantipl != IPL_NONE)
			mtx->mtx_oldipl = s;
#ifdef DIAGNOSTIC
		ci->ci_mutex_level++;
#endif
		WITNESS_LOCK(MUTEX_LOCK_OBJECT(mtx), LOP_EXCLUSIVE);
		return (1);
	}

#ifdef DIAGNOSTIC
	if (__predict_false(mtx->mtx_owner == ci))
		panic("mtx %p: locking against myself", mtx);
#endif
	if (mtx->mtx_wantipl != IPL_NONE)
		splx(s);

	return (0);
}
#else
void
mtx_enter(struct mutex *mtx)
{
	struct cpu_info *ci = curcpu();

	/* Avoid deadlocks after panic or in DDB */
	if (panicstr || db_active)
		return;

	WITNESS_CHECKORDER(MUTEX_LOCK_OBJECT(mtx),
	    LOP_EXCLUSIVE | LOP_NEWORDER, NULL);

#ifdef DIAGNOSTIC
	if (__predict_false(mtx->mtx_owner == ci))
		panic("mtx %p: locking against myself", mtx);
#endif

	if (mtx->mtx_wantipl != IPL_NONE)
		mtx->mtx_oldipl = splraise(mtx->mtx_wantipl);

	mtx->mtx_owner = ci;

#ifdef DIAGNOSTIC
	ci->ci_mutex_level++;
#endif
	WITNESS_LOCK(MUTEX_LOCK_OBJECT(mtx), LOP_EXCLUSIVE);
}

int
mtx_enter_try(struct mutex *mtx)
{
	mtx_enter(mtx);
	return (1);
}
#endif

void
mtx_leave(struct mutex *mtx)
{
	int s;

	/* Avoid deadlocks after panic or in DDB */
	if (panicstr || db_active)
		return;

	MUTEX_ASSERT_LOCKED(mtx);
	WITNESS_UNLOCK(MUTEX_LOCK_OBJECT(mtx), LOP_EXCLUSIVE);

#ifdef DIAGNOSTIC
	curcpu()->ci_mutex_level--;
#endif

	s = mtx->mtx_oldipl;
#ifdef MULTIPROCESSOR
	membar_exit();
#endif
	mtx->mtx_owner = NULL;
	(void)atomic_swap(&mtx->mtx_lock, 0);
	if (mtx->mtx_wantipl != IPL_NONE)
		splx(s);
}
