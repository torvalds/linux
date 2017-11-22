/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2015 by Delphix. All rights reserved.
 * Copyright 2016 The MathWorks, Inc. All rights reserved.
 */

/*
 * A Zero Reference Lock (ZRL) is a reference count that can lock out new
 * references only when the count is zero and only without waiting if the count
 * is not already zero. It is similar to a read-write lock in that it allows
 * multiple readers and only a single writer, but it does not allow a writer to
 * block while waiting for readers to exit, and therefore the question of
 * reader/writer priority is moot (no WRWANT bit). Since the equivalent of
 * rw_enter(&lock, RW_WRITER) is disallowed and only tryenter() is allowed, it
 * is perfectly safe for the same reader to acquire the same lock multiple
 * times. The fact that a ZRL is reentrant for readers (through multiple calls
 * to zrl_add()) makes it convenient for determining whether something is
 * actively referenced without the fuss of flagging lock ownership across
 * function calls.
 */
#include <sys/zrlock.h>
#include <sys/trace_zrlock.h>

/*
 * A ZRL can be locked only while there are zero references, so ZRL_LOCKED is
 * treated as zero references.
 */
#define	ZRL_LOCKED	-1
#define	ZRL_DESTROYED	-2

void
zrl_init(zrlock_t *zrl)
{
	mutex_init(&zrl->zr_mtx, NULL, MUTEX_DEFAULT, NULL);
	zrl->zr_refcount = 0;
	cv_init(&zrl->zr_cv, NULL, CV_DEFAULT, NULL);
#ifdef	ZFS_DEBUG
	zrl->zr_owner = NULL;
	zrl->zr_caller = NULL;
#endif
}

void
zrl_destroy(zrlock_t *zrl)
{
	ASSERT0(zrl->zr_refcount);

	mutex_destroy(&zrl->zr_mtx);
	zrl->zr_refcount = ZRL_DESTROYED;
	cv_destroy(&zrl->zr_cv);
}

void
zrl_add_impl(zrlock_t *zrl, const char *zc)
{
	for (;;) {
		uint32_t n = (uint32_t)zrl->zr_refcount;
		while (n != ZRL_LOCKED) {
			uint32_t cas = atomic_cas_32(
			    (uint32_t *)&zrl->zr_refcount, n, n + 1);
			if (cas == n) {
				ASSERT3S((int32_t)n, >=, 0);
#ifdef	ZFS_DEBUG
				if (zrl->zr_owner == curthread) {
					DTRACE_PROBE2(zrlock__reentry,
					    zrlock_t *, zrl, uint32_t, n);
				}
				zrl->zr_owner = curthread;
				zrl->zr_caller = zc;
#endif
				return;
			}
			n = cas;
		}

		mutex_enter(&zrl->zr_mtx);
		while (zrl->zr_refcount == ZRL_LOCKED) {
			cv_wait(&zrl->zr_cv, &zrl->zr_mtx);
		}
		mutex_exit(&zrl->zr_mtx);
	}
}

void
zrl_remove(zrlock_t *zrl)
{
	uint32_t n;

#ifdef	ZFS_DEBUG
	if (zrl->zr_owner == curthread) {
		zrl->zr_owner = NULL;
		zrl->zr_caller = NULL;
	}
#endif
	n = atomic_dec_32_nv((uint32_t *)&zrl->zr_refcount);
	ASSERT3S((int32_t)n, >=, 0);
}

int
zrl_tryenter(zrlock_t *zrl)
{
	uint32_t n = (uint32_t)zrl->zr_refcount;

	if (n == 0) {
		uint32_t cas = atomic_cas_32(
		    (uint32_t *)&zrl->zr_refcount, 0, ZRL_LOCKED);
		if (cas == 0) {
#ifdef	ZFS_DEBUG
			ASSERT3P(zrl->zr_owner, ==, NULL);
			zrl->zr_owner = curthread;
#endif
			return (1);
		}
	}

	ASSERT3S((int32_t)n, >, ZRL_DESTROYED);

	return (0);
}

void
zrl_exit(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, ==, ZRL_LOCKED);

	mutex_enter(&zrl->zr_mtx);
#ifdef	ZFS_DEBUG
	ASSERT3P(zrl->zr_owner, ==, curthread);
	zrl->zr_owner = NULL;
	membar_producer();	/* make sure the owner store happens first */
#endif
	zrl->zr_refcount = 0;
	cv_broadcast(&zrl->zr_cv);
	mutex_exit(&zrl->zr_mtx);
}

int
zrl_refcount(zrlock_t *zrl)
{
	int n;

	ASSERT3S(zrl->zr_refcount, >, ZRL_DESTROYED);

	n = (int)zrl->zr_refcount;
	return (n <= 0 ? 0 : n);
}

int
zrl_is_zero(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, >, ZRL_DESTROYED);

	return (zrl->zr_refcount <= 0);
}

int
zrl_is_locked(zrlock_t *zrl)
{
	ASSERT3S(zrl->zr_refcount, >, ZRL_DESTROYED);

	return (zrl->zr_refcount == ZRL_LOCKED);
}

#ifdef	ZFS_DEBUG
kthread_t *
zrl_owner(zrlock_t *zrl)
{
	return (zrl->zr_owner);
}
#endif

#if defined(_KERNEL) && defined(HAVE_SPL)

EXPORT_SYMBOL(zrl_add_impl);
EXPORT_SYMBOL(zrl_remove);

#endif
