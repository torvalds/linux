/*	$OpenBSD: mutex.c,v 1.17 2019/04/23 13:35:12 visa Exp $	*/

/*
 * Copyright (c) 2004 Artur Grabowski <art@openbsd.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include <sys/param.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/atomic.h>

#include <machine/intr.h>

#include <ddb/db_output.h>

#ifdef MULTIPROCESSOR
/* Note: lock must be 16-byte aligned. */
#define __mtx_lock(mtx) ((int *)(((vaddr_t)mtx->mtx_lock + 0xf) & ~0xf))
#endif

void
__mtx_init(struct mutex *mtx, int wantipl)
{
#ifdef MULTIPROCESSOR
	mtx->mtx_lock[0] = 1;
	mtx->mtx_lock[1] = 1;
	mtx->mtx_lock[2] = 1;
	mtx->mtx_lock[3] = 1;
#endif
	mtx->mtx_wantipl = wantipl;
	mtx->mtx_oldipl = IPL_NONE;
	mtx->mtx_owner = NULL;
}

#ifdef MULTIPROCESSOR
void
mtx_enter(struct mutex *mtx)
{
	while (mtx_enter_try(mtx) == 0)
		;
}

int
mtx_enter_try(struct mutex *mtx)
{
	struct cpu_info *ci = curcpu();
	volatile int *lock = __mtx_lock(mtx);
	int ret;
	int s;

 	if (mtx->mtx_wantipl != IPL_NONE)
		s = splraise(mtx->mtx_wantipl);

#ifdef DIAGNOSTIC
	if (__predict_false(mtx->mtx_owner == ci))
		panic("mtx %p: locking against myself", mtx);
#endif

	asm volatile (
		"ldcws      0(%2), %0"
		: "=&r" (ret), "+m" (lock)
		: "r" (lock)
	);

	if (ret) {
		membar_enter();
		mtx->mtx_owner = ci;
		if (mtx->mtx_wantipl != IPL_NONE)
			mtx->mtx_oldipl = s;
#ifdef DIAGNOSTIC
		ci->ci_mutex_level++;
#endif

		return (1);
	}

	if (mtx->mtx_wantipl != IPL_NONE)
		splx(s);

	return (0);
}
#else
void
mtx_enter(struct mutex *mtx)
{
	struct cpu_info *ci = curcpu();

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
#ifdef MULTIPROCESSOR
	volatile int *lock = __mtx_lock(mtx);
#endif
	int s;

	MUTEX_ASSERT_LOCKED(mtx);

#ifdef DIAGNOSTIC
	curcpu()->ci_mutex_level--;
#endif
	s = mtx->mtx_oldipl;
	mtx->mtx_owner = NULL;
#ifdef MULTIPROCESSOR
	membar_exit();
	*lock = 1;
#endif

	if (mtx->mtx_wantipl != IPL_NONE)
		splx(s);
}
