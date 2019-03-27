/*	$NetBSD: linux_futex.c,v 1.7 2006/07/24 19:01:49 manu Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2009-2016 Dmitry Chagin
 * Copyright (c) 2005 Emmanuel Dreyfus
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Emmanuel Dreyfus
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#if 0
__KERNEL_RCSID(1, "$NetBSD: linux_futex.c,v 1.7 2006/07/24 19:01:49 manu Exp $");
#endif

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/umtx.h>

#include <vm/vm_extern.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_futex.h>
#include <compat/linux/linux_timer.h>
#include <compat/linux/linux_util.h>

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

/**
 * Futex part for the special DTrace module "locks".
 */
LIN_SDT_PROBE_DEFINE1(locks, futex_mtx, locked, "struct mtx *");
LIN_SDT_PROBE_DEFINE1(locks, futex_mtx, unlock, "struct mtx *");

/**
 * Per futex probes.
 */
LIN_SDT_PROBE_DEFINE1(futex, futex, create, "struct sx *");
LIN_SDT_PROBE_DEFINE1(futex, futex, destroy, "struct sx *");

/**
 * DTrace probes in this module.
 */
LIN_SDT_PROBE_DEFINE2(futex, futex_put, entry, "struct futex *",
    "struct waiting_proc *");
LIN_SDT_PROBE_DEFINE3(futex, futex_put, destroy, "uint32_t *", "uint32_t",
    "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_put, unlock, "uint32_t *", "uint32_t",
    "int");
LIN_SDT_PROBE_DEFINE0(futex, futex_put, return);
LIN_SDT_PROBE_DEFINE3(futex, futex_get0, entry, "uint32_t *", "struct futex **",
    "uint32_t");
LIN_SDT_PROBE_DEFINE1(futex, futex_get0, umtx_key_get_error, "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_get0, shared, "uint32_t *", "uint32_t",
    "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_get0, null, "uint32_t *");
LIN_SDT_PROBE_DEFINE3(futex, futex_get0, new, "uint32_t *", "uint32_t", "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_get0, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_get, entry, "uint32_t *",
    "struct waiting_proc **", "struct futex **");
LIN_SDT_PROBE_DEFINE0(futex, futex_get, error);
LIN_SDT_PROBE_DEFINE1(futex, futex_get, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_sleep, entry, "struct futex *",
    "struct waiting_proc **", "struct timespec *");
LIN_SDT_PROBE_DEFINE5(futex, futex_sleep, requeue_error, "int", "uint32_t *",
    "struct waiting_proc *", "uint32_t *", "uint32_t");
LIN_SDT_PROBE_DEFINE3(futex, futex_sleep, sleep_error, "int", "uint32_t *",
    "struct waiting_proc *");
LIN_SDT_PROBE_DEFINE1(futex, futex_sleep, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_wake, entry, "struct futex *", "int",
    "uint32_t");
LIN_SDT_PROBE_DEFINE3(futex, futex_wake, iterate, "uint32_t",
    "struct waiting_proc *", "uint32_t");
LIN_SDT_PROBE_DEFINE1(futex, futex_wake, wakeup, "struct waiting_proc *");
LIN_SDT_PROBE_DEFINE1(futex, futex_wake, return, "int");
LIN_SDT_PROBE_DEFINE4(futex, futex_requeue, entry, "struct futex *", "int",
    "struct futex *", "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_requeue, wakeup, "struct waiting_proc *");
LIN_SDT_PROBE_DEFINE3(futex, futex_requeue, requeue, "uint32_t *",
    "struct waiting_proc *", "uint32_t");
LIN_SDT_PROBE_DEFINE1(futex, futex_requeue, return, "int");
LIN_SDT_PROBE_DEFINE4(futex, futex_wait, entry, "struct futex *",
    "struct waiting_proc **", "struct timespec *", "uint32_t");
LIN_SDT_PROBE_DEFINE1(futex, futex_wait, sleep_error, "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_wait, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, futex_atomic_op, entry, "struct thread *",
    "int", "uint32_t");
LIN_SDT_PROBE_DEFINE4(futex, futex_atomic_op, decoded_op, "int", "int", "int",
    "int");
LIN_SDT_PROBE_DEFINE0(futex, futex_atomic_op, missing_access_check);
LIN_SDT_PROBE_DEFINE1(futex, futex_atomic_op, unimplemented_op, "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_atomic_op, unimplemented_cmp, "int");
LIN_SDT_PROBE_DEFINE1(futex, futex_atomic_op, return, "int");
LIN_SDT_PROBE_DEFINE2(futex, linux_sys_futex, entry, "struct thread *",
    "struct linux_sys_futex_args *");
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_clockswitch);
LIN_SDT_PROBE_DEFINE1(futex, linux_sys_futex, copyin_error, "int");
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, invalid_cmp_requeue_use);
LIN_SDT_PROBE_DEFINE3(futex, linux_sys_futex, debug_wait, "uint32_t *",
    "uint32_t", "uint32_t");
LIN_SDT_PROBE_DEFINE4(futex, linux_sys_futex, debug_wait_value_neq,
    "uint32_t *", "uint32_t", "int", "uint32_t");
LIN_SDT_PROBE_DEFINE3(futex, linux_sys_futex, debug_wake, "uint32_t *",
    "uint32_t", "uint32_t");
LIN_SDT_PROBE_DEFINE5(futex, linux_sys_futex, debug_cmp_requeue, "uint32_t *",
    "uint32_t", "uint32_t", "uint32_t *", "struct l_timespec *");
LIN_SDT_PROBE_DEFINE2(futex, linux_sys_futex, debug_cmp_requeue_value_neq,
    "uint32_t", "int");
LIN_SDT_PROBE_DEFINE5(futex, linux_sys_futex, debug_wake_op, "uint32_t *",
    "int", "uint32_t", "uint32_t *", "uint32_t");
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unhandled_efault);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_lock_pi);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_unlock_pi);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_trylock_pi);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, deprecated_requeue);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_wait_requeue_pi);
LIN_SDT_PROBE_DEFINE0(futex, linux_sys_futex, unimplemented_cmp_requeue_pi);
LIN_SDT_PROBE_DEFINE1(futex, linux_sys_futex, unknown_operation, "int");
LIN_SDT_PROBE_DEFINE1(futex, linux_sys_futex, return, "int");
LIN_SDT_PROBE_DEFINE2(futex, linux_set_robust_list, entry, "struct thread *",
    "struct linux_set_robust_list_args *");
LIN_SDT_PROBE_DEFINE0(futex, linux_set_robust_list, size_error);
LIN_SDT_PROBE_DEFINE1(futex, linux_set_robust_list, return, "int");
LIN_SDT_PROBE_DEFINE2(futex, linux_get_robust_list, entry, "struct thread *",
    "struct linux_get_robust_list_args *");
LIN_SDT_PROBE_DEFINE1(futex, linux_get_robust_list, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(futex, linux_get_robust_list, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, handle_futex_death, entry,
    "struct linux_emuldata *", "uint32_t *", "unsigned int");
LIN_SDT_PROBE_DEFINE1(futex, handle_futex_death, copyin_error, "int");
LIN_SDT_PROBE_DEFINE1(futex, handle_futex_death, return, "int");
LIN_SDT_PROBE_DEFINE3(futex, fetch_robust_entry, entry,
    "struct linux_robust_list **", "struct linux_robust_list **",
    "unsigned int *");
LIN_SDT_PROBE_DEFINE1(futex, fetch_robust_entry, copyin_error, "int");
LIN_SDT_PROBE_DEFINE1(futex, fetch_robust_entry, return, "int");
LIN_SDT_PROBE_DEFINE2(futex, release_futexes, entry, "struct thread *",
    "struct linux_emuldata *");
LIN_SDT_PROBE_DEFINE1(futex, release_futexes, copyin_error, "int");
LIN_SDT_PROBE_DEFINE0(futex, release_futexes, return);

struct futex;

struct waiting_proc {
	uint32_t	wp_flags;
	struct futex	*wp_futex;
	TAILQ_ENTRY(waiting_proc) wp_list;
};

struct futex {
	struct mtx	f_lck;
	uint32_t	*f_uaddr;	/* user-supplied value, for debug */
	struct umtx_key	f_key;
	uint32_t	f_refcount;
	uint32_t	f_bitset;
	LIST_ENTRY(futex) f_list;
	TAILQ_HEAD(lf_waiting_proc, waiting_proc) f_waiting_proc;
};

struct futex_list futex_list;

#define FUTEX_LOCK(f)		mtx_lock(&(f)->f_lck)
#define FUTEX_LOCKED(f)		mtx_owned(&(f)->f_lck)
#define FUTEX_UNLOCK(f)		mtx_unlock(&(f)->f_lck)
#define FUTEX_INIT(f)		do { \
				    mtx_init(&(f)->f_lck, "ftlk", NULL, \
					MTX_DUPOK); \
				    LIN_SDT_PROBE1(futex, futex, create, \
					&(f)->f_lck); \
				} while (0)
#define FUTEX_DESTROY(f)	do { \
				    LIN_SDT_PROBE1(futex, futex, destroy, \
					&(f)->f_lck); \
				    mtx_destroy(&(f)->f_lck); \
				} while (0)
#define FUTEX_ASSERT_LOCKED(f)	mtx_assert(&(f)->f_lck, MA_OWNED)
#define FUTEX_ASSERT_UNLOCKED(f) mtx_assert(&(f)->f_lck, MA_NOTOWNED)

struct mtx futex_mtx;			/* protects the futex list */
#define FUTEXES_LOCK		do { \
				    mtx_lock(&futex_mtx); \
				    LIN_SDT_PROBE1(locks, futex_mtx, \
					locked, &futex_mtx); \
				} while (0)
#define FUTEXES_UNLOCK		do { \
				    LIN_SDT_PROBE1(locks, futex_mtx, \
					unlock, &futex_mtx); \
				    mtx_unlock(&futex_mtx); \
				} while (0)

/* flags for futex_get() */
#define FUTEX_CREATE_WP		0x1	/* create waiting_proc */
#define FUTEX_DONTCREATE	0x2	/* don't create futex if not exists */
#define FUTEX_DONTEXISTS	0x4	/* return EINVAL if futex exists */
#define	FUTEX_SHARED		0x8	/* shared futex */
#define	FUTEX_DONTLOCK		0x10	/* don't lock futex */

/* wp_flags */
#define FUTEX_WP_REQUEUED	0x1	/* wp requeued - wp moved from wp_list
					 * of futex where thread sleep to wp_list
					 * of another futex.
					 */
#define FUTEX_WP_REMOVED	0x2	/* wp is woken up and removed from futex
					 * wp_list to prevent double wakeup.
					 */

static void futex_put(struct futex *, struct waiting_proc *);
static int futex_get0(uint32_t *, struct futex **f, uint32_t);
static int futex_get(uint32_t *, struct waiting_proc **, struct futex **,
    uint32_t);
static int futex_sleep(struct futex *, struct waiting_proc *, struct timespec *);
static int futex_wake(struct futex *, int, uint32_t);
static int futex_requeue(struct futex *, int, struct futex *, int);
static int futex_copyin_timeout(int, struct l_timespec *, int,
    struct timespec *);
static int futex_wait(struct futex *, struct waiting_proc *, struct timespec *,
    uint32_t);
static void futex_lock(struct futex *);
static void futex_unlock(struct futex *);
static int futex_atomic_op(struct thread *, int, uint32_t *);
static int handle_futex_death(struct linux_emuldata *, uint32_t *,
    unsigned int);
static int fetch_robust_entry(struct linux_robust_list **,
    struct linux_robust_list **, unsigned int *);

static int
futex_copyin_timeout(int op, struct l_timespec *luts, int clockrt,
    struct timespec *ts)
{
	struct l_timespec lts;
	struct timespec kts;
	int error;

	error = copyin(luts, &lts, sizeof(lts));
	if (error)
		return (error);

	error = linux_to_native_timespec(ts, &lts);
	if (error)
		return (error);
	if (clockrt) {
		nanotime(&kts);
		timespecsub(ts, &kts, ts);
	} else if (op == LINUX_FUTEX_WAIT_BITSET) {
		nanouptime(&kts);
		timespecsub(ts, &kts, ts);
	}
	return (error);
}

static void
futex_put(struct futex *f, struct waiting_proc *wp)
{
	LIN_SDT_PROBE2(futex, futex_put, entry, f, wp);

	if (wp != NULL) {
		if ((wp->wp_flags & FUTEX_WP_REMOVED) == 0)
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
		free(wp, M_FUTEX_WP);
	}

	FUTEXES_LOCK;
	if (--f->f_refcount == 0) {
		LIST_REMOVE(f, f_list);
		FUTEXES_UNLOCK;
		if (FUTEX_LOCKED(f))
			futex_unlock(f);

		LIN_SDT_PROBE3(futex, futex_put, destroy, f->f_uaddr,
		    f->f_refcount, f->f_key.shared);
		LINUX_CTR3(sys_futex, "futex_put destroy uaddr %p ref %d "
		    "shared %d", f->f_uaddr, f->f_refcount, f->f_key.shared);
		umtx_key_release(&f->f_key);
		FUTEX_DESTROY(f);
		free(f, M_FUTEX);

		LIN_SDT_PROBE0(futex, futex_put, return);
		return;
	}

	LIN_SDT_PROBE3(futex, futex_put, unlock, f->f_uaddr, f->f_refcount,
	    f->f_key.shared);
	LINUX_CTR3(sys_futex, "futex_put uaddr %p ref %d shared %d",
	    f->f_uaddr, f->f_refcount, f->f_key.shared);
	FUTEXES_UNLOCK;
	if (FUTEX_LOCKED(f))
		futex_unlock(f);

	LIN_SDT_PROBE0(futex, futex_put, return);
}

static int
futex_get0(uint32_t *uaddr, struct futex **newf, uint32_t flags)
{
	struct futex *f, *tmpf;
	struct umtx_key key;
	int error;

	LIN_SDT_PROBE3(futex, futex_get0, entry, uaddr, newf, flags);

	*newf = tmpf = NULL;

	error = umtx_key_get(uaddr, TYPE_FUTEX, (flags & FUTEX_SHARED) ?
	    AUTO_SHARE : THREAD_SHARE, &key);
	if (error) {
		LIN_SDT_PROBE1(futex, futex_get0, umtx_key_get_error, error);
		LIN_SDT_PROBE1(futex, futex_get0, return, error);
		return (error);
	}
retry:
	FUTEXES_LOCK;
	LIST_FOREACH(f, &futex_list, f_list) {
		if (umtx_key_match(&f->f_key, &key)) {
			if (tmpf != NULL) {
				if (FUTEX_LOCKED(tmpf))
					futex_unlock(tmpf);
				FUTEX_DESTROY(tmpf);
				free(tmpf, M_FUTEX);
			}
			if (flags & FUTEX_DONTEXISTS) {
				FUTEXES_UNLOCK;
				umtx_key_release(&key);

				LIN_SDT_PROBE1(futex, futex_get0, return,
				    EINVAL);
				return (EINVAL);
			}

			/*
			 * Increment refcount of the found futex to
			 * prevent it from deallocation before FUTEX_LOCK()
			 */
			++f->f_refcount;
			FUTEXES_UNLOCK;
			umtx_key_release(&key);

			if ((flags & FUTEX_DONTLOCK) == 0)
				futex_lock(f);
			*newf = f;
			LIN_SDT_PROBE3(futex, futex_get0, shared, uaddr,
			    f->f_refcount, f->f_key.shared);
			LINUX_CTR3(sys_futex, "futex_get uaddr %p ref %d shared %d",
			    uaddr, f->f_refcount, f->f_key.shared);

			LIN_SDT_PROBE1(futex, futex_get0, return, 0);
			return (0);
		}
	}

	if (flags & FUTEX_DONTCREATE) {
		FUTEXES_UNLOCK;
		umtx_key_release(&key);
		LIN_SDT_PROBE1(futex, futex_get0, null, uaddr);
		LINUX_CTR1(sys_futex, "futex_get uaddr %p null", uaddr);

		LIN_SDT_PROBE1(futex, futex_get0, return, 0);
		return (0);
	}

	if (tmpf == NULL) {
		FUTEXES_UNLOCK;
		tmpf = malloc(sizeof(*tmpf), M_FUTEX, M_WAITOK | M_ZERO);
		tmpf->f_uaddr = uaddr;
		tmpf->f_key = key;
		tmpf->f_refcount = 1;
		tmpf->f_bitset = FUTEX_BITSET_MATCH_ANY;
		FUTEX_INIT(tmpf);
		TAILQ_INIT(&tmpf->f_waiting_proc);

		/*
		 * Lock the new futex before an insert into the futex_list
		 * to prevent futex usage by other.
		 */
		if ((flags & FUTEX_DONTLOCK) == 0)
			futex_lock(tmpf);
		goto retry;
	}

	LIST_INSERT_HEAD(&futex_list, tmpf, f_list);
	FUTEXES_UNLOCK;

	LIN_SDT_PROBE3(futex, futex_get0, new, uaddr, tmpf->f_refcount,
	    tmpf->f_key.shared);
	LINUX_CTR3(sys_futex, "futex_get uaddr %p ref %d shared %d new",
	    uaddr, tmpf->f_refcount, tmpf->f_key.shared);
	*newf = tmpf;

	LIN_SDT_PROBE1(futex, futex_get0, return, 0);
	return (0);
}

static int
futex_get(uint32_t *uaddr, struct waiting_proc **wp, struct futex **f,
    uint32_t flags)
{
	int error;

	LIN_SDT_PROBE3(futex, futex_get, entry, uaddr, wp, f);

	if (flags & FUTEX_CREATE_WP) {
		*wp = malloc(sizeof(struct waiting_proc), M_FUTEX_WP, M_WAITOK);
		(*wp)->wp_flags = 0;
	}
	error = futex_get0(uaddr, f, flags);
	if (error) {
		LIN_SDT_PROBE0(futex, futex_get, error);

		if (flags & FUTEX_CREATE_WP)
			free(*wp, M_FUTEX_WP);

		LIN_SDT_PROBE1(futex, futex_get, return, error);
		return (error);
	}
	if (flags & FUTEX_CREATE_WP) {
		TAILQ_INSERT_HEAD(&(*f)->f_waiting_proc, *wp, wp_list);
		(*wp)->wp_futex = *f;
	}

	LIN_SDT_PROBE1(futex, futex_get, return, error);
	return (error);
}

static inline void
futex_lock(struct futex *f)
{

	LINUX_CTR3(sys_futex, "futex_lock uaddr %p ref %d shared %d",
	    f->f_uaddr, f->f_refcount, f->f_key.shared);
	FUTEX_ASSERT_UNLOCKED(f);
	FUTEX_LOCK(f);
}

static inline void
futex_unlock(struct futex *f)
{

	LINUX_CTR3(sys_futex, "futex_unlock uaddr %p ref %d shared %d",
	    f->f_uaddr, f->f_refcount, f->f_key.shared);
	FUTEX_ASSERT_LOCKED(f);
	FUTEX_UNLOCK(f);
}

static int
futex_sleep(struct futex *f, struct waiting_proc *wp, struct timespec *ts)
{
	struct timespec uts;
	sbintime_t sbt, prec, tmp;
	time_t over;
	int error;

	FUTEX_ASSERT_LOCKED(f);
	if (ts != NULL) {
		uts = *ts;
		if (uts.tv_sec > INT32_MAX / 2) {
			over = uts.tv_sec - INT32_MAX / 2;
			uts.tv_sec -= over;
		}
		tmp = tstosbt(uts);
		if (TIMESEL(&sbt, tmp))
			sbt += tc_tick_sbt;
		sbt += tmp;
		prec = tmp;
		prec >>= tc_precexp;
	} else {
		sbt = 0;
		prec = 0;
	}
	LIN_SDT_PROBE3(futex, futex_sleep, entry, f, wp, sbt);
	LINUX_CTR4(sys_futex, "futex_sleep enter uaddr %p wp %p timo %ld ref %d",
	    f->f_uaddr, wp, sbt, f->f_refcount);

	error = msleep_sbt(wp, &f->f_lck, PCATCH, "futex", sbt, prec, C_ABSOLUTE);
	if (wp->wp_flags & FUTEX_WP_REQUEUED) {
		KASSERT(f != wp->wp_futex, ("futex != wp_futex"));

		if (error) {
			LIN_SDT_PROBE5(futex, futex_sleep, requeue_error, error,
			    f->f_uaddr, wp, wp->wp_futex->f_uaddr,
			    wp->wp_futex->f_refcount);
		}

		LINUX_CTR5(sys_futex, "futex_sleep out error %d uaddr %p wp"
		    " %p requeued uaddr %p ref %d",
		    error, f->f_uaddr, wp, wp->wp_futex->f_uaddr,
		    wp->wp_futex->f_refcount);
		futex_put(f, NULL);
		f = wp->wp_futex;
		futex_lock(f);
	} else {
		if (error) {
			LIN_SDT_PROBE3(futex, futex_sleep, sleep_error, error,
			    f->f_uaddr, wp);
		}
		LINUX_CTR3(sys_futex, "futex_sleep out error %d uaddr %p wp %p",
		    error, f->f_uaddr, wp);
	}

	futex_put(f, wp);

	LIN_SDT_PROBE1(futex, futex_sleep, return, error);
	return (error);
}

static int
futex_wake(struct futex *f, int n, uint32_t bitset)
{
	struct waiting_proc *wp, *wpt;
	int count = 0;

	LIN_SDT_PROBE3(futex, futex_wake, entry, f, n, bitset);

	if (bitset == 0) {
		LIN_SDT_PROBE1(futex, futex_wake, return, EINVAL);
		return (EINVAL);
	}

	FUTEX_ASSERT_LOCKED(f);
	TAILQ_FOREACH_SAFE(wp, &f->f_waiting_proc, wp_list, wpt) {
		LIN_SDT_PROBE3(futex, futex_wake, iterate, f->f_uaddr, wp,
		    f->f_refcount);
		LINUX_CTR3(sys_futex, "futex_wake uaddr %p wp %p ref %d",
		    f->f_uaddr, wp, f->f_refcount);
		/*
		 * Unless we find a matching bit in
		 * the bitset, continue searching.
		 */
		if (!(wp->wp_futex->f_bitset & bitset))
			continue;

		wp->wp_flags |= FUTEX_WP_REMOVED;
		TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
		LIN_SDT_PROBE1(futex, futex_wake, wakeup, wp);
		wakeup_one(wp);
		if (++count == n)
			break;
	}

	LIN_SDT_PROBE1(futex, futex_wake, return, count);
	return (count);
}

static int
futex_requeue(struct futex *f, int n, struct futex *f2, int n2)
{
	struct waiting_proc *wp, *wpt;
	int count = 0;

	LIN_SDT_PROBE4(futex, futex_requeue, entry, f, n, f2, n2);

	FUTEX_ASSERT_LOCKED(f);
	FUTEX_ASSERT_LOCKED(f2);

	TAILQ_FOREACH_SAFE(wp, &f->f_waiting_proc, wp_list, wpt) {
		if (++count <= n) {
			LINUX_CTR2(sys_futex, "futex_req_wake uaddr %p wp %p",
			    f->f_uaddr, wp);
			wp->wp_flags |= FUTEX_WP_REMOVED;
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
			LIN_SDT_PROBE1(futex, futex_requeue, wakeup, wp);
			wakeup_one(wp);
		} else {
			LIN_SDT_PROBE3(futex, futex_requeue, requeue,
			    f->f_uaddr, wp, f2->f_uaddr);
			LINUX_CTR3(sys_futex, "futex_requeue uaddr %p wp %p to %p",
			    f->f_uaddr, wp, f2->f_uaddr);
			wp->wp_flags |= FUTEX_WP_REQUEUED;
			/* Move wp to wp_list of f2 futex */
			TAILQ_REMOVE(&f->f_waiting_proc, wp, wp_list);
			TAILQ_INSERT_HEAD(&f2->f_waiting_proc, wp, wp_list);

			/*
			 * Thread which sleeps on wp after waking should
			 * acquire f2 lock, so increment refcount of f2 to
			 * prevent it from premature deallocation.
			 */
			wp->wp_futex = f2;
			FUTEXES_LOCK;
			++f2->f_refcount;
			FUTEXES_UNLOCK;
			if (count - n >= n2)
				break;
		}
	}

	LIN_SDT_PROBE1(futex, futex_requeue, return, count);
	return (count);
}

static int
futex_wait(struct futex *f, struct waiting_proc *wp, struct timespec *ts,
    uint32_t bitset)
{
	int error;

	LIN_SDT_PROBE4(futex, futex_wait, entry, f, wp, ts, bitset);

	if (bitset == 0) {
		LIN_SDT_PROBE1(futex, futex_wait, return, EINVAL);
		return (EINVAL);
	}

	f->f_bitset = bitset;
	error = futex_sleep(f, wp, ts);
	if (error)
		LIN_SDT_PROBE1(futex, futex_wait, sleep_error, error);
	if (error == EWOULDBLOCK)
		error = ETIMEDOUT;

	LIN_SDT_PROBE1(futex, futex_wait, return, error);
	return (error);
}

static int
futex_atomic_op(struct thread *td, int encoded_op, uint32_t *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;

	LIN_SDT_PROBE3(futex, futex_atomic_op, entry, td, encoded_op, uaddr);

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	LIN_SDT_PROBE4(futex, futex_atomic_op, decoded_op, op, cmp, oparg,
	    cmparg);

	/* XXX: Linux verifies access here and returns EFAULT */
	LIN_SDT_PROBE0(futex, futex_atomic_op, missing_access_check);

	switch (op) {
	case FUTEX_OP_SET:
		ret = futex_xchgl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ADD:
		ret = futex_addl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_OR:
		ret = futex_orl(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ANDN:
		ret = futex_andl(~oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_XOR:
		ret = futex_xorl(oparg, uaddr, &oldval);
		break;
	default:
		LIN_SDT_PROBE1(futex, futex_atomic_op, unimplemented_op, op);
		ret = -ENOSYS;
		break;
	}

	if (ret) {
		LIN_SDT_PROBE1(futex, futex_atomic_op, return, ret);
		return (ret);
	}

	switch (cmp) {
	case FUTEX_OP_CMP_EQ:
		ret = (oldval == cmparg);
		break;
	case FUTEX_OP_CMP_NE:
		ret = (oldval != cmparg);
		break;
	case FUTEX_OP_CMP_LT:
		ret = (oldval < cmparg);
		break;
	case FUTEX_OP_CMP_GE:
		ret = (oldval >= cmparg);
		break;
	case FUTEX_OP_CMP_LE:
		ret = (oldval <= cmparg);
		break;
	case FUTEX_OP_CMP_GT:
		ret = (oldval > cmparg);
		break;
	default:
		LIN_SDT_PROBE1(futex, futex_atomic_op, unimplemented_cmp, cmp);
		ret = -ENOSYS;
	}

	LIN_SDT_PROBE1(futex, futex_atomic_op, return, ret);
	return (ret);
}

int
linux_sys_futex(struct thread *td, struct linux_sys_futex_args *args)
{
	int clockrt, nrwake, op_ret, ret;
	struct linux_pemuldata *pem;
	struct waiting_proc *wp;
	struct futex *f, *f2;
	struct timespec uts, *ts;
	int error, save;
	uint32_t flags, val;

	LIN_SDT_PROBE2(futex, linux_sys_futex, entry, td, args);

	if (args->op & LINUX_FUTEX_PRIVATE_FLAG) {
		flags = 0;
		args->op &= ~LINUX_FUTEX_PRIVATE_FLAG;
	} else
		flags = FUTEX_SHARED;

	/*
	 * Currently support for switching between CLOCK_MONOTONIC and
	 * CLOCK_REALTIME is not present. However Linux forbids the use of
	 * FUTEX_CLOCK_REALTIME with any op except FUTEX_WAIT_BITSET and
	 * FUTEX_WAIT_REQUEUE_PI.
	 */
	clockrt = args->op & LINUX_FUTEX_CLOCK_REALTIME;
	args->op = args->op & ~LINUX_FUTEX_CLOCK_REALTIME;
	if (clockrt && args->op != LINUX_FUTEX_WAIT_BITSET &&
		args->op != LINUX_FUTEX_WAIT_REQUEUE_PI) {
		LIN_SDT_PROBE0(futex, linux_sys_futex,
		    unimplemented_clockswitch);
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);
	}

	error = 0;
	f = f2 = NULL;

	switch (args->op) {
	case LINUX_FUTEX_WAIT:
		args->val3 = FUTEX_BITSET_MATCH_ANY;
		/* FALLTHROUGH */

	case LINUX_FUTEX_WAIT_BITSET:
		LIN_SDT_PROBE3(futex, linux_sys_futex, debug_wait, args->uaddr,
		    args->val, args->val3);
		LINUX_CTR3(sys_futex, "WAIT uaddr %p val 0x%x bitset 0x%x",
		    args->uaddr, args->val, args->val3);

		if (args->timeout != NULL) {
			error = futex_copyin_timeout(args->op, args->timeout,
			    clockrt, &uts);
			if (error) {
				LIN_SDT_PROBE1(futex, linux_sys_futex, copyin_error,
				    error);
				LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
				return (error);
			}
			ts = &uts;
		} else
			ts = NULL;

retry0:
		error = futex_get(args->uaddr, &wp, &f,
		    flags | FUTEX_CREATE_WP);
		if (error) {
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}

		error = copyin_nofault(args->uaddr, &val, sizeof(val));
		if (error) {
			futex_put(f, wp);
			error = copyin(args->uaddr, &val, sizeof(val));
			if (error == 0)
				goto retry0;
			LIN_SDT_PROBE1(futex, linux_sys_futex, copyin_error,
			    error);
			LINUX_CTR1(sys_futex, "WAIT copyin failed %d",
			    error);
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}
		if (val != args->val) {
			LIN_SDT_PROBE4(futex, linux_sys_futex,
			    debug_wait_value_neq, args->uaddr, args->val, val,
			    args->val3);
			LINUX_CTR3(sys_futex,
			    "WAIT uaddr %p val 0x%x != uval 0x%x",
			    args->uaddr, args->val, val);
			futex_put(f, wp);

			LIN_SDT_PROBE1(futex, linux_sys_futex, return,
			    EWOULDBLOCK);
			return (EWOULDBLOCK);
		}

		error = futex_wait(f, wp, ts, args->val3);
		break;

	case LINUX_FUTEX_WAKE:
		args->val3 = FUTEX_BITSET_MATCH_ANY;
		/* FALLTHROUGH */

	case LINUX_FUTEX_WAKE_BITSET:
		LIN_SDT_PROBE3(futex, linux_sys_futex, debug_wake, args->uaddr,
		    args->val, args->val3);
		LINUX_CTR3(sys_futex, "WAKE uaddr %p nrwake 0x%x bitset 0x%x",
		    args->uaddr, args->val, args->val3);

		error = futex_get(args->uaddr, NULL, &f,
		    flags | FUTEX_DONTCREATE);
		if (error) {
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}

		if (f == NULL) {
			td->td_retval[0] = 0;

			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}
		td->td_retval[0] = futex_wake(f, args->val, args->val3);
		futex_put(f, NULL);
		break;

	case LINUX_FUTEX_CMP_REQUEUE:
		LIN_SDT_PROBE5(futex, linux_sys_futex, debug_cmp_requeue,
		    args->uaddr, args->val, args->val3, args->uaddr2,
		    args->timeout);
		LINUX_CTR5(sys_futex, "CMP_REQUEUE uaddr %p "
		    "nrwake 0x%x uval 0x%x uaddr2 %p nrequeue 0x%x",
		    args->uaddr, args->val, args->val3, args->uaddr2,
		    args->timeout);

		/*
		 * Linux allows this, we would not, it is an incorrect
		 * usage of declared ABI, so return EINVAL.
		 */
		if (args->uaddr == args->uaddr2) {
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    invalid_cmp_requeue_use);
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, EINVAL);
			return (EINVAL);
		}

retry1:
		error = futex_get(args->uaddr, NULL, &f, flags | FUTEX_DONTLOCK);
		if (error) {
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}

		/*
		 * To avoid deadlocks return EINVAL if second futex
		 * exists at this time.
		 *
		 * Glibc fall back to FUTEX_WAKE in case of any error
		 * returned by FUTEX_CMP_REQUEUE.
		 */
		error = futex_get(args->uaddr2, NULL, &f2,
		    flags | FUTEX_DONTEXISTS | FUTEX_DONTLOCK);
		if (error) {
			futex_put(f, NULL);

			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}
		futex_lock(f);
		futex_lock(f2);
		error = copyin_nofault(args->uaddr, &val, sizeof(val));
		if (error) {
			futex_put(f2, NULL);
			futex_put(f, NULL);
			error = copyin(args->uaddr, &val, sizeof(val));
			if (error == 0)
				goto retry1;
			LIN_SDT_PROBE1(futex, linux_sys_futex, copyin_error,
			    error);
			LINUX_CTR1(sys_futex, "CMP_REQUEUE copyin failed %d",
			    error);
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}
		if (val != args->val3) {
			LIN_SDT_PROBE2(futex, linux_sys_futex,
			    debug_cmp_requeue_value_neq, args->val, val);
			LINUX_CTR2(sys_futex, "CMP_REQUEUE val 0x%x != uval 0x%x",
			    args->val, val);
			futex_put(f2, NULL);
			futex_put(f, NULL);

			LIN_SDT_PROBE1(futex, linux_sys_futex, return, EAGAIN);
			return (EAGAIN);
		}

		nrwake = (int)(unsigned long)args->timeout;
		td->td_retval[0] = futex_requeue(f, args->val, f2, nrwake);
		futex_put(f2, NULL);
		futex_put(f, NULL);
		break;

	case LINUX_FUTEX_WAKE_OP:
		LIN_SDT_PROBE5(futex, linux_sys_futex, debug_wake_op,
		    args->uaddr, args->op, args->val, args->uaddr2, args->val3);
		LINUX_CTR5(sys_futex, "WAKE_OP "
		    "uaddr %p nrwake 0x%x uaddr2 %p op 0x%x nrwake2 0x%x",
		    args->uaddr, args->val, args->uaddr2, args->val3,
		    args->timeout);

		if (args->uaddr == args->uaddr2) {
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, EINVAL);
			return (EINVAL);
		}

retry2:
		error = futex_get(args->uaddr, NULL, &f, flags | FUTEX_DONTLOCK);
		if (error) {
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}

		error = futex_get(args->uaddr2, NULL, &f2, flags | FUTEX_DONTLOCK);
		if (error) {
			futex_put(f, NULL);

			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}
		futex_lock(f);
		futex_lock(f2);

		/*
		 * This function returns positive number as results and
		 * negative as errors
		 */
		save = vm_fault_disable_pagefaults();
		op_ret = futex_atomic_op(td, args->val3, args->uaddr2);
		vm_fault_enable_pagefaults(save);

		LINUX_CTR2(sys_futex, "WAKE_OP atomic_op uaddr %p ret 0x%x",
		    args->uaddr, op_ret);

		if (op_ret < 0) {
			if (f2 != NULL)
				futex_put(f2, NULL);
			futex_put(f, NULL);
			error = copyin(args->uaddr2, &val, sizeof(val));
			if (error == 0)
				goto retry2;
			LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
			return (error);
		}

		ret = futex_wake(f, args->val, args->val3);

		if (op_ret > 0) {
			op_ret = 0;
			nrwake = (int)(unsigned long)args->timeout;

			if (f2 != NULL)
				op_ret += futex_wake(f2, nrwake, args->val3);
			else
				op_ret += futex_wake(f, nrwake, args->val3);
			ret += op_ret;

		}
		if (f2 != NULL)
			futex_put(f2, NULL);
		futex_put(f, NULL);
		td->td_retval[0] = ret;
		break;

	case LINUX_FUTEX_LOCK_PI:
		/* not yet implemented */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XUNSUP_FUTEXPIOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_pi op\n");
			pem->flags |= LINUX_XUNSUP_FUTEXPIOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    unimplemented_lock_pi);
		}
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);

	case LINUX_FUTEX_UNLOCK_PI:
		/* not yet implemented */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XUNSUP_FUTEXPIOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_pi op\n");
			pem->flags |= LINUX_XUNSUP_FUTEXPIOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    unimplemented_unlock_pi);
		}
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);

	case LINUX_FUTEX_TRYLOCK_PI:
		/* not yet implemented */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XUNSUP_FUTEXPIOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_pi op\n");
			pem->flags |= LINUX_XUNSUP_FUTEXPIOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    unimplemented_trylock_pi);
		}
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);

	case LINUX_FUTEX_REQUEUE:
		/*
		 * Glibc does not use this operation since version 2.3.3,
		 * as it is racy and replaced by FUTEX_CMP_REQUEUE operation.
		 * Glibc versions prior to 2.3.3 fall back to FUTEX_WAKE when
		 * FUTEX_REQUEUE returned EINVAL.
		 */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XDEPR_REQUEUEOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_requeue op\n");
			pem->flags |= LINUX_XDEPR_REQUEUEOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    deprecated_requeue);
		}

		LIN_SDT_PROBE1(futex, linux_sys_futex, return, EINVAL);
		return (EINVAL);

	case LINUX_FUTEX_WAIT_REQUEUE_PI:
		/* not yet implemented */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XUNSUP_FUTEXPIOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_pi op\n");
			pem->flags |= LINUX_XUNSUP_FUTEXPIOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    unimplemented_wait_requeue_pi);
		}
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);

	case LINUX_FUTEX_CMP_REQUEUE_PI:
		/* not yet implemented */
		pem = pem_find(td->td_proc);
		if ((pem->flags & LINUX_XUNSUP_FUTEXPIOP) == 0) {
			linux_msg(td,
				  "linux_sys_futex: "
				  "unsupported futex_pi op\n");
			pem->flags |= LINUX_XUNSUP_FUTEXPIOP;
			LIN_SDT_PROBE0(futex, linux_sys_futex,
			    unimplemented_cmp_requeue_pi);
		}
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);

	default:
		linux_msg(td,
			  "linux_sys_futex: unknown op %d\n", args->op);
		LIN_SDT_PROBE1(futex, linux_sys_futex, unknown_operation,
		    args->op);
		LIN_SDT_PROBE1(futex, linux_sys_futex, return, ENOSYS);
		return (ENOSYS);
	}

	LIN_SDT_PROBE1(futex, linux_sys_futex, return, error);
	return (error);
}

int
linux_set_robust_list(struct thread *td, struct linux_set_robust_list_args *args)
{
	struct linux_emuldata *em;

	LIN_SDT_PROBE2(futex, linux_set_robust_list, entry, td, args);

	if (args->len != sizeof(struct linux_robust_list_head)) {
		LIN_SDT_PROBE0(futex, linux_set_robust_list, size_error);
		LIN_SDT_PROBE1(futex, linux_set_robust_list, return, EINVAL);
		return (EINVAL);
	}

	em = em_find(td);
	em->robust_futexes = args->head;

	LIN_SDT_PROBE1(futex, linux_set_robust_list, return, 0);
	return (0);
}

int
linux_get_robust_list(struct thread *td, struct linux_get_robust_list_args *args)
{
	struct linux_emuldata *em;
	struct linux_robust_list_head *head;
	l_size_t len = sizeof(struct linux_robust_list_head);
	struct thread *td2;
	int error = 0;

	LIN_SDT_PROBE2(futex, linux_get_robust_list, entry, td, args);

	if (!args->pid) {
		em = em_find(td);
		KASSERT(em != NULL, ("get_robust_list: emuldata notfound.\n"));
		head = em->robust_futexes;
	} else {
		td2 = tdfind(args->pid, -1);
		if (td2 == NULL) {
			LIN_SDT_PROBE1(futex, linux_get_robust_list, return,
			    ESRCH);
			return (ESRCH);
		}
		if (SV_PROC_ABI(td2->td_proc) != SV_ABI_LINUX) {
			LIN_SDT_PROBE1(futex, linux_get_robust_list, return,
			    EPERM);
			PROC_UNLOCK(td2->td_proc);
			return (EPERM);
		}

		em = em_find(td2);
		KASSERT(em != NULL, ("get_robust_list: emuldata notfound.\n"));
		/* XXX: ptrace? */
		if (priv_check(td, PRIV_CRED_SETUID) ||
		    priv_check(td, PRIV_CRED_SETEUID) ||
		    p_candebug(td, td2->td_proc)) {
			PROC_UNLOCK(td2->td_proc);

			LIN_SDT_PROBE1(futex, linux_get_robust_list, return,
			    EPERM);
			return (EPERM);
		}
		head = em->robust_futexes;

		PROC_UNLOCK(td2->td_proc);
	}

	error = copyout(&len, args->len, sizeof(l_size_t));
	if (error) {
		LIN_SDT_PROBE1(futex, linux_get_robust_list, copyout_error,
		    error);
		LIN_SDT_PROBE1(futex, linux_get_robust_list, return, EFAULT);
		return (EFAULT);
	}

	error = copyout(&head, args->head, sizeof(head));
	if (error) {
		LIN_SDT_PROBE1(futex, linux_get_robust_list, copyout_error,
		    error);
	}

	LIN_SDT_PROBE1(futex, linux_get_robust_list, return, error);
	return (error);
}

static int
handle_futex_death(struct linux_emuldata *em, uint32_t *uaddr,
    unsigned int pi)
{
	uint32_t uval, nval, mval;
	struct futex *f;
	int error;

	LIN_SDT_PROBE3(futex, handle_futex_death, entry, em, uaddr, pi);

retry:
	error = copyin(uaddr, &uval, 4);
	if (error) {
		LIN_SDT_PROBE1(futex, handle_futex_death, copyin_error, error);
		LIN_SDT_PROBE1(futex, handle_futex_death, return, EFAULT);
		return (EFAULT);
	}
	if ((uval & FUTEX_TID_MASK) == em->em_tid) {
		mval = (uval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
		nval = casuword32(uaddr, uval, mval);

		if (nval == -1) {
			LIN_SDT_PROBE1(futex, handle_futex_death, return,
			    EFAULT);
			return (EFAULT);
		}

		if (nval != uval)
			goto retry;

		if (!pi && (uval & FUTEX_WAITERS)) {
			error = futex_get(uaddr, NULL, &f,
			    FUTEX_DONTCREATE | FUTEX_SHARED);
			if (error) {
				LIN_SDT_PROBE1(futex, handle_futex_death,
				    return, error);
				return (error);
			}
			if (f != NULL) {
				futex_wake(f, 1, FUTEX_BITSET_MATCH_ANY);
				futex_put(f, NULL);
			}
		}
	}

	LIN_SDT_PROBE1(futex, handle_futex_death, return, 0);
	return (0);
}

static int
fetch_robust_entry(struct linux_robust_list **entry,
    struct linux_robust_list **head, unsigned int *pi)
{
	l_ulong uentry;
	int error;

	LIN_SDT_PROBE3(futex, fetch_robust_entry, entry, entry, head, pi);

	error = copyin((const void *)head, &uentry, sizeof(l_ulong));
	if (error) {
		LIN_SDT_PROBE1(futex, fetch_robust_entry, copyin_error, error);
		LIN_SDT_PROBE1(futex, fetch_robust_entry, return, EFAULT);
		return (EFAULT);
	}

	*entry = (void *)(uentry & ~1UL);
	*pi = uentry & 1;

	LIN_SDT_PROBE1(futex, fetch_robust_entry, return, 0);
	return (0);
}

/* This walks the list of robust futexes releasing them. */
void
release_futexes(struct thread *td, struct linux_emuldata *em)
{
	struct linux_robust_list_head *head = NULL;
	struct linux_robust_list *entry, *next_entry, *pending;
	unsigned int limit = 2048, pi, next_pi, pip;
	l_long futex_offset;
	int rc, error;

	LIN_SDT_PROBE2(futex, release_futexes, entry, td, em);

	head = em->robust_futexes;

	if (head == NULL) {
		LIN_SDT_PROBE0(futex, release_futexes, return);
		return;
	}

	if (fetch_robust_entry(&entry, PTRIN(&head->list.next), &pi)) {
		LIN_SDT_PROBE0(futex, release_futexes, return);
		return;
	}

	error = copyin(&head->futex_offset, &futex_offset,
	    sizeof(futex_offset));
	if (error) {
		LIN_SDT_PROBE1(futex, release_futexes, copyin_error, error);
		LIN_SDT_PROBE0(futex, release_futexes, return);
		return;
	}

	if (fetch_robust_entry(&pending, PTRIN(&head->pending_list), &pip)) {
		LIN_SDT_PROBE0(futex, release_futexes, return);
		return;
	}

	while (entry != &head->list) {
		rc = fetch_robust_entry(&next_entry, PTRIN(&entry->next), &next_pi);

		if (entry != pending)
			if (handle_futex_death(em,
			    (uint32_t *)((caddr_t)entry + futex_offset), pi)) {
				LIN_SDT_PROBE0(futex, release_futexes, return);
				return;
			}
		if (rc) {
			LIN_SDT_PROBE0(futex, release_futexes, return);
			return;
		}

		entry = next_entry;
		pi = next_pi;

		if (!--limit)
			break;

		sched_relinquish(curthread);
	}

	if (pending)
		handle_futex_death(em, (uint32_t *)((caddr_t)pending + futex_offset), pip);

	LIN_SDT_PROBE0(futex, release_futexes, return);
}
