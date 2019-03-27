/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Stephan Uphoff <ups@FreeBSD.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef _SYS_RMLOCK_H_
#define _SYS_RMLOCK_H_

#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/_lock.h>
#include <sys/_rmlock.h>

#ifdef _KERNEL

/*
 * Flags passed to rm_init_flags(9).
 */
#define	RM_NOWITNESS	0x00000001
#define	RM_RECURSE	0x00000002
#define	RM_SLEEPABLE	0x00000004
#define	RM_NEW		0x00000008

void	rm_init(struct rmlock *rm, const char *name);
void	rm_init_flags(struct rmlock *rm, const char *name, int opts);
void	rm_destroy(struct rmlock *rm);
int	rm_wowned(const struct rmlock *rm);
void	rm_sysinit(void *arg);

void	_rm_wlock_debug(struct rmlock *rm, const char *file, int line);
void	_rm_wunlock_debug(struct rmlock *rm, const char *file, int line);
int	_rm_rlock_debug(struct rmlock *rm, struct rm_priotracker *tracker,
	    int trylock, const char *file, int line);
void	_rm_runlock_debug(struct rmlock *rm,  struct rm_priotracker *tracker,
	    const char *file, int line);

void	_rm_wlock(struct rmlock *rm);
void	_rm_wunlock(struct rmlock *rm);
int	_rm_rlock(struct rmlock *rm, struct rm_priotracker *tracker,
	    int trylock);
void	_rm_runlock(struct rmlock *rm,  struct rm_priotracker *tracker);
#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void	_rm_assert(const struct rmlock *rm, int what, const char *file,
	    int line);
#endif

/*
 * Public interface for lock operations.
 */
#ifndef LOCK_DEBUG
#error LOCK_DEBUG not defined, include <sys/lock.h> before <sys/rmlock.h>
#endif

#if LOCK_DEBUG > 0
#define	rm_wlock(rm)	_rm_wlock_debug((rm), LOCK_FILE, LOCK_LINE)
#define	rm_wunlock(rm)	_rm_wunlock_debug((rm), LOCK_FILE, LOCK_LINE)
#define	rm_rlock(rm,tracker)  \
    ((void)_rm_rlock_debug((rm),(tracker), 0, LOCK_FILE, LOCK_LINE ))
#define	rm_try_rlock(rm,tracker)  \
    _rm_rlock_debug((rm),(tracker), 1, LOCK_FILE, LOCK_LINE )
#define	rm_runlock(rm,tracker)	\
    _rm_runlock_debug((rm), (tracker), LOCK_FILE, LOCK_LINE )
#else
#define	rm_wlock(rm)			_rm_wlock((rm))
#define	rm_wunlock(rm)			_rm_wunlock((rm))
#define	rm_rlock(rm,tracker)		((void)_rm_rlock((rm),(tracker), 0))
#define	rm_try_rlock(rm,tracker)	_rm_rlock((rm),(tracker), 1)
#define	rm_runlock(rm,tracker)		_rm_runlock((rm), (tracker))
#endif
#define	rm_sleep(chan, rm, pri, wmesg, timo)				\
	_sleep((chan), &(rm)->lock_object, (pri), (wmesg),		\
	    tick_sbt * (timo), 0, C_HARDCLOCK)

struct rm_args {
	struct rmlock	*ra_rm;
	const char 	*ra_desc;
	int		ra_flags;
};

#define	RM_SYSINIT_FLAGS(name, rm, desc, flags)				\
	static struct rm_args name##_args = {				\
		(rm),							\
		(desc),							\
		(flags),						\
	};								\
	SYSINIT(name##_rm_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rm_sysinit, &name##_args);					\
	SYSUNINIT(name##_rm_sysuninit, SI_SUB_LOCK, SI_ORDER_MIDDLE,	\
	    rm_destroy, (rm))

#define	RM_SYSINIT(name, rm, desc)	RM_SYSINIT_FLAGS(name, rm, desc, 0)

#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
#define	RA_LOCKED		LA_LOCKED
#define	RA_RLOCKED		LA_SLOCKED
#define	RA_WLOCKED		LA_XLOCKED
#define	RA_UNLOCKED		LA_UNLOCKED
#define	RA_RECURSED		LA_RECURSED
#define	RA_NOTRECURSED		LA_NOTRECURSED
#endif

#ifdef INVARIANTS
#define	rm_assert(rm, what)	_rm_assert((rm), (what), LOCK_FILE, LOCK_LINE)
#else
#define	rm_assert(rm, what)
#endif

#endif /* _KERNEL */
#endif /* !_SYS_RMLOCK_H_ */
