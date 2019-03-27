/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)callout.h	8.2 (Berkeley) 1/21/94
 * $FreeBSD$
 */

#ifndef _SYS_CALLOUT_H_
#define _SYS_CALLOUT_H_

#include <sys/_callout.h>

#define	CALLOUT_LOCAL_ALLOC	0x0001 /* was allocated from callfree */
#define	CALLOUT_ACTIVE		0x0002 /* callout is currently active */
#define	CALLOUT_PENDING		0x0004 /* callout is waiting for timeout */
#define	CALLOUT_MPSAFE		0x0008 /* deprecated */
#define	CALLOUT_RETURNUNLOCKED	0x0010 /* handler returns with mtx unlocked */
#define	CALLOUT_SHAREDLOCK	0x0020 /* callout lock held in shared mode */
#define	CALLOUT_DFRMIGRATION	0x0040 /* callout in deferred migration mode */
#define	CALLOUT_PROCESSED	0x0080 /* callout in wheel or processing list? */
#define	CALLOUT_DIRECT 		0x0100 /* allow exec from hw int context */

#define	C_DIRECT_EXEC		0x0001 /* direct execution of callout */
#define	C_PRELBITS		7
#define	C_PRELRANGE		((1 << C_PRELBITS) - 1)
#define	C_PREL(x)		(((x) + 1) << 1)
#define	C_PRELGET(x)		(int)((((x) >> 1) & C_PRELRANGE) - 1)
#define	C_HARDCLOCK		0x0100 /* align to hardclock() calls */
#define	C_ABSOLUTE		0x0200 /* event time is absolute. */
#define	C_PRECALC		0x0400 /* event time is pre-calculated. */
#define	C_CATCH			0x0800 /* catch signals, used by pause_sbt(9) */

struct callout_handle {
	struct callout *callout;
};

/* Flags for callout_stop_safe() */
#define	CS_DRAIN		0x0001 /* callout_drain(), wait allowed */
#define	CS_EXECUTING		0x0002 /* Positive return value indicates that
					  the callout was executing */

#ifdef _KERNEL
/* 
 * Note the flags field is actually *two* fields. The c_flags
 * field is the one that caller operations that may, or may not have
 * a lock touches i.e. callout_deactivate(). The other, the c_iflags,
 * is the internal flags that *must* be kept correct on which the
 * callout system depend on e.g. callout_pending().
 * The c_iflag is used internally by the callout system to determine which
 * list the callout is on and track internal state. Callers *should not* 
 * use the c_flags field directly but should use the macros provided.
 *  
 * The c_iflags field holds internal flags that are protected by internal
 * locks of the callout subsystem.  The c_flags field holds external flags.
 * The caller must hold its own lock while manipulating or reading external
 * flags via callout_active(), callout_deactivate(), callout_reset*(), or
 * callout_stop() to avoid races.
 */
#define	callout_active(c)	((c)->c_flags & CALLOUT_ACTIVE)
#define	callout_deactivate(c)	((c)->c_flags &= ~CALLOUT_ACTIVE)
#define	callout_drain(c)	_callout_stop_safe(c, CS_DRAIN, NULL)
void	callout_init(struct callout *, int);
void	_callout_init_lock(struct callout *, struct lock_object *, int);
#define	callout_init_mtx(c, mtx, flags)					\
	_callout_init_lock((c), ((mtx) != NULL) ? &(mtx)->lock_object :	\
	    NULL, (flags))
#define	callout_init_rm(c, rm, flags)					\
	_callout_init_lock((c), ((rm) != NULL) ? &(rm)->lock_object : 	\
	    NULL, (flags))
#define	callout_init_rw(c, rw, flags)					\
	_callout_init_lock((c), ((rw) != NULL) ? &(rw)->lock_object :	\
	   NULL, (flags))
#define	callout_pending(c)	((c)->c_iflags & CALLOUT_PENDING)
int	callout_reset_sbt_on(struct callout *, sbintime_t, sbintime_t,
	    void (*)(void *), void *, int, int);
#define	callout_reset_sbt(c, sbt, pr, fn, arg, flags)			\
    callout_reset_sbt_on((c), (sbt), (pr), (fn), (arg), -1, (flags))
#define	callout_reset_sbt_curcpu(c, sbt, pr, fn, arg, flags)		\
    callout_reset_sbt_on((c), (sbt), (pr), (fn), (arg), PCPU_GET(cpuid),\
        (flags))
#define	callout_reset_on(c, to_ticks, fn, arg, cpu)			\
    callout_reset_sbt_on((c), tick_sbt * (to_ticks), 0, (fn), (arg),	\
        (cpu), C_HARDCLOCK)
#define	callout_reset(c, on_tick, fn, arg)				\
    callout_reset_on((c), (on_tick), (fn), (arg), -1)
#define	callout_reset_curcpu(c, on_tick, fn, arg)			\
    callout_reset_on((c), (on_tick), (fn), (arg), PCPU_GET(cpuid))
#define	callout_schedule_sbt_on(c, sbt, pr, cpu, flags)			\
    callout_reset_sbt_on((c), (sbt), (pr), (c)->c_func, (c)->c_arg,	\
        (cpu), (flags))
#define	callout_schedule_sbt(c, sbt, pr, flags)				\
    callout_schedule_sbt_on((c), (sbt), (pr), -1, (flags))
#define	callout_schedule_sbt_curcpu(c, sbt, pr, flags)			\
    callout_schedule_sbt_on((c), (sbt), (pr), PCPU_GET(cpuid), (flags))
int	callout_schedule(struct callout *, int);
int	callout_schedule_on(struct callout *, int, int);
#define	callout_schedule_curcpu(c, on_tick)				\
    callout_schedule_on((c), (on_tick), PCPU_GET(cpuid))
#define	callout_stop(c)		_callout_stop_safe(c, 0, NULL)
int	_callout_stop_safe(struct callout *, int, void (*)(void *));
void	callout_process(sbintime_t now);
#define callout_async_drain(c, d)					\
    _callout_stop_safe(c, 0, d)
void callout_when(sbintime_t sbt, sbintime_t precision, int flags,
    sbintime_t *sbt_res, sbintime_t *prec_res);
#endif

#endif /* _SYS_CALLOUT_H_ */
