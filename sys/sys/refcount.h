/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 John Baldwin <jhb@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef __SYS_REFCOUNT_H__
#define __SYS_REFCOUNT_H__

#include <sys/limits.h>
#include <machine/atomic.h>

#ifdef _KERNEL
#include <sys/systm.h>
#else
#define	KASSERT(exp, msg)	/* */
#endif

static __inline void
refcount_init(volatile u_int *count, u_int value)
{

	*count = value;
}

static __inline void
refcount_acquire(volatile u_int *count)
{

	KASSERT(*count < UINT_MAX, ("refcount %p overflowed", count));
	atomic_add_int(count, 1);
}

static __inline int
refcount_release(volatile u_int *count)
{
	u_int old;

	atomic_thread_fence_rel();
	old = atomic_fetchadd_int(count, -1);
	KASSERT(old > 0, ("refcount %p is zero", count));
	if (old > 1)
		return (0);

	/*
	 * Last reference.  Signal the user to call the destructor.
	 *
	 * Ensure that the destructor sees all updates.  The fence_rel
	 * at the start of the function synchronized with this fence.
	 */
	atomic_thread_fence_acq();
	return (1);
}

/*
 * This functions returns non-zero if the refcount was
 * incremented. Else zero is returned.
 */
static __inline __result_use_check int
refcount_acquire_if_not_zero(volatile u_int *count)
{
	u_int old;

	old = *count;
	for (;;) {
		KASSERT(old < UINT_MAX, ("refcount %p overflowed", count));
		if (old == 0)
			return (0);
		if (atomic_fcmpset_int(count, &old, old + 1))
			return (1);
	}
}

static __inline __result_use_check int
refcount_release_if_not_last(volatile u_int *count)
{
	u_int old;

	old = *count;
	for (;;) {
		KASSERT(old > 0, ("refcount %p is zero", count));
		if (old == 1)
			return (0);
		if (atomic_fcmpset_int(count, &old, old - 1))
			return (1);
	}
}

#endif	/* ! __SYS_REFCOUNT_H__ */
