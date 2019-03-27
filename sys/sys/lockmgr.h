/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Attilio Rao <attilio@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible 
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_LOCKMGR_H_
#define	_SYS_LOCKMGR_H_

#include <sys/_lock.h>
#include <sys/_lockmgr.h>
#include <sys/_mutex.h>
#include <sys/_rwlock.h>

#define	LK_SHARE			0x01
#define	LK_SHARED_WAITERS		0x02
#define	LK_EXCLUSIVE_WAITERS		0x04
#define	LK_EXCLUSIVE_SPINNERS		0x08
#define	LK_ALL_WAITERS							\
	(LK_SHARED_WAITERS | LK_EXCLUSIVE_WAITERS)
#define	LK_FLAGMASK							\
	(LK_SHARE | LK_ALL_WAITERS | LK_EXCLUSIVE_SPINNERS)

#define	LK_HOLDER(x)			((x) & ~LK_FLAGMASK)
#define	LK_SHARERS_SHIFT		4
#define	LK_SHARERS(x)			(LK_HOLDER(x) >> LK_SHARERS_SHIFT)
#define	LK_SHARERS_LOCK(x)		((x) << LK_SHARERS_SHIFT | LK_SHARE)
#define	LK_ONE_SHARER			(1 << LK_SHARERS_SHIFT)
#define	LK_UNLOCKED			LK_SHARERS_LOCK(0)
#define	LK_KERNPROC			((uintptr_t)(-1) & ~LK_FLAGMASK)

#ifdef _KERNEL

#if !defined(LOCK_FILE) || !defined(LOCK_LINE)
#error	"LOCK_FILE and LOCK_LINE not defined, include <sys/lock.h> before"
#endif

struct thread;
#define	lk_recurse	lock_object.lo_data

/*
 * Function prototipes.  Routines that start with an underscore are not part
 * of the public interface and might be wrappered with a macro.
 */
int	 __lockmgr_args(struct lock *lk, u_int flags, struct lock_object *ilk,
	    const char *wmesg, int prio, int timo, const char *file, int line);
int	 lockmgr_lock_fast_path(struct lock *lk, u_int flags,
	    struct lock_object *ilk, const char *file, int line);
int	 lockmgr_unlock_fast_path(struct lock *lk, u_int flags,
	    struct lock_object *ilk);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	 _lockmgr_assert(const struct lock *lk, int what, const char *file, int line);
#endif
void	 _lockmgr_disown(struct lock *lk, const char *file, int line);

void	 lockallowrecurse(struct lock *lk);
void	 lockallowshare(struct lock *lk);
void	 lockdestroy(struct lock *lk);
void	 lockdisablerecurse(struct lock *lk);
void	 lockdisableshare(struct lock *lk);
void	 lockinit(struct lock *lk, int prio, const char *wmesg, int timo,
	    int flags);
#ifdef DDB
int	 lockmgr_chain(struct thread *td, struct thread **ownerp);
#endif
void	 lockmgr_printinfo(const struct lock *lk);
int	 lockstatus(const struct lock *lk);

/*
 * As far as the ilk can be a static NULL pointer these functions need a
 * strict prototype in order to safely use the lock_object member.
 */
static __inline int
_lockmgr_args(struct lock *lk, u_int flags, struct mtx *ilk, const char *wmesg,
    int prio, int timo, const char *file, int line)
{

	return (__lockmgr_args(lk, flags, (ilk != NULL) ? &ilk->lock_object :
	    NULL, wmesg, prio, timo, file, line));
}

static __inline int
_lockmgr_args_rw(struct lock *lk, u_int flags, struct rwlock *ilk,
    const char *wmesg, int prio, int timo, const char *file, int line)
{

	return (__lockmgr_args(lk, flags, (ilk != NULL) ? &ilk->lock_object :
	    NULL, wmesg, prio, timo, file, line));
}

/*
 * Define aliases in order to complete lockmgr KPI.
 */
#define	lockmgr(lk, flags, ilk)						\
	_lockmgr_args((lk), (flags), (ilk), LK_WMESG_DEFAULT,		\
	    LK_PRIO_DEFAULT, LK_TIMO_DEFAULT, LOCK_FILE, LOCK_LINE)
#define	lockmgr_args(lk, flags, ilk, wmesg, prio, timo)			\
	_lockmgr_args((lk), (flags), (ilk), (wmesg), (prio), (timo),	\
	    LOCK_FILE, LOCK_LINE)
#define	lockmgr_args_rw(lk, flags, ilk, wmesg, prio, timo)		\
	_lockmgr_args_rw((lk), (flags), (ilk), (wmesg), (prio), (timo),	\
	    LOCK_FILE, LOCK_LINE)
#define	lockmgr_disown(lk)						\
	_lockmgr_disown((lk), LOCK_FILE, LOCK_LINE)
#define	lockmgr_recursed(lk)						\
	((lk)->lk_recurse != 0)
#define	lockmgr_rw(lk, flags, ilk)					\
	_lockmgr_args_rw((lk), (flags), (ilk), LK_WMESG_DEFAULT,	\
	    LK_PRIO_DEFAULT, LK_TIMO_DEFAULT, LOCK_FILE, LOCK_LINE)
#ifdef INVARIANTS
#define	lockmgr_assert(lk, what)					\
	_lockmgr_assert((lk), (what), LOCK_FILE, LOCK_LINE)
#else
#define	lockmgr_assert(lk, what)
#endif

/*
 * Flags for lockinit().
 */
#define	LK_INIT_MASK	0x0001FF
#define	LK_CANRECURSE	0x000001
#define	LK_NODUP	0x000002
#define	LK_NOPROFILE	0x000004
#define	LK_NOSHARE	0x000008
#define	LK_NOWITNESS	0x000010
#define	LK_QUIET	0x000020
#define	LK_ADAPTIVE	0x000040
#define	LK_IS_VNODE	0x000080	/* Tell WITNESS about a VNODE lock */
#define	LK_NEW		0x000100

/*
 * Additional attributes to be used in lockmgr().
 */
#define	LK_EATTR_MASK	0x00FF00
#define	LK_INTERLOCK	0x000100
#define	LK_NOWAIT	0x000200
#define	LK_RETRY	0x000400
#define	LK_SLEEPFAIL	0x000800
#define	LK_TIMELOCK	0x001000
#define	LK_NODDLKTREAT	0x002000
#define	LK_VNHELD	0x004000

/*
 * Operations for lockmgr().
 */
#define	LK_TYPE_MASK	0xFF0000
#define	LK_DOWNGRADE	0x010000
#define	LK_DRAIN	0x020000
#define	LK_EXCLOTHER	0x040000
#define	LK_EXCLUSIVE	0x080000
#define	LK_RELEASE	0x100000
#define	LK_SHARED	0x200000
#define	LK_UPGRADE	0x400000
#define	LK_TRYUPGRADE	0x800000

#define	LK_TOTAL_MASK	(LK_INIT_MASK | LK_EATTR_MASK | LK_TYPE_MASK)

/*
 * Default values for lockmgr_args().
 */
#define	LK_WMESG_DEFAULT	(NULL)
#define	LK_PRIO_DEFAULT		(0)
#define	LK_TIMO_DEFAULT		(0)

/*
 * Assertion flags.
 */
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	KA_LOCKED	LA_LOCKED
#define	KA_SLOCKED	LA_SLOCKED
#define	KA_XLOCKED	LA_XLOCKED
#define	KA_UNLOCKED	LA_UNLOCKED
#define	KA_RECURSED	LA_RECURSED
#define	KA_NOTRECURSED	LA_NOTRECURSED
#endif

#endif /* _KERNEL */

#endif /* !_SYS_LOCKMGR_H_ */
