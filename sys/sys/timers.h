/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 1994 by Chris Provenzano, proven@mit.edu
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
 *  This product includes software developed by Chris Provenzano.
 * 4. The name of Chris Provenzano may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Description : Basic timers header.
 */

#ifndef _SYS_TIMERS_H_
#define _SYS_TIMERS_H_

#include <sys/time.h>

#ifdef _KERNEL
/*
 * Structures used to manage POSIX timers in a process.
 */
struct itimer {
	struct mtx  		it_mtx;
	struct sigevent		it_sigev;
	struct itimerspec	it_time;
	struct proc 		*it_proc;
	int	it_flags;
	int	it_usecount;
	int	it_overrun;		/* Overruns currently accumulating */
	int	it_overrun_last;	/* Overruns associated w/ a delivery */
	int	it_clockid;
	int	it_timerid;
	ksiginfo_t	it_ksi;
	union {
		/* realtime */
		struct {
			struct callout it_callout;
		} _rt;

		/* cpu timer */
		struct {
			LIST_ENTRY(itimer)	it_link;
			TAILQ_ENTRY(itimer)	it_worklink;
			int			it_active;
			int			it_cflags;
		} _cpu;
	} _data;
};

#define it_callout	_data._rt.it_callout
#define it_link		_data._cpu.it_link
#define it_active	_data._cpu.it_active
#define	it_worklink	_data._cpu.it_worklink
#define	it_cflags	_data._cpu.it_cflags

#define	ITF_DELETING	0x01
#define	ITF_WANTED	0x02

#define	ITCF_ONWORKLIST	0x01

#define	TIMER_MAX	32

#define	ITIMER_LOCK(it)		mtx_lock(&(it)->it_mtx)
#define	ITIMER_UNLOCK(it)	mtx_unlock(&(it)->it_mtx)

LIST_HEAD(itimerlist, itimer);

struct	itimers {
	struct itimerlist	its_virtual;
	struct itimerlist	its_prof;
	TAILQ_HEAD(, itimer)	its_worklist;
	struct itimer		*its_timers[TIMER_MAX];
};

struct	kclock {
	int (*timer_create)(struct itimer *timer);
	int (*timer_settime)(struct itimer * timer, int flags,
		struct itimerspec * new_value,
		struct itimerspec * old_value);
	int (*timer_delete)(struct itimer * timer);
	int (*timer_gettime)(struct itimer * timer,
		struct itimerspec * cur_value);
	void (*event_hook)(struct proc *p, clockid_t clock_id, int event);
};

/* Event values for event_hook() */
#define	ITIMER_EV_EXEC	0
#define	ITIMER_EV_EXIT	1

int	itimer_accept(struct proc *p, int tid, ksiginfo_t *ksi);
#endif
#endif /* !_SYS_TIMERS_H_ */
