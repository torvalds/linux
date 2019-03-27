/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996, 1997
 *	HD Associates, Inc.  All rights reserved.
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
 *	This product includes software developed by HD Associates, Inc
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* ksched: Soft real time scheduling based on "rtprio". */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_posix.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/posix4.h>
#include <sys/resource.h>
#include <sys/sched.h>

FEATURE(kposix_priority_scheduling, "POSIX P1003.1B realtime extensions");

/* ksched: Real-time extension to support POSIX priority scheduling. */

struct ksched {
	struct timespec rr_interval;
};

int
ksched_attach(struct ksched **p)
{
	struct ksched *ksched;

	ksched = malloc(sizeof(*ksched), M_P31B, M_WAITOK);
	ksched->rr_interval.tv_sec = 0;
	ksched->rr_interval.tv_nsec = 1000000000L / hz * sched_rr_interval();
	*p = ksched;
	return (0);
}

int
ksched_detach(struct ksched *ks)
{

	free(ks, M_P31B);
	return (0);
}

/*
 * XXX About priorities
 *
 *	POSIX 1003.1b requires that numerically higher priorities be of
 *	higher priority.  It also permits sched_setparam to be
 *	implementation defined for SCHED_OTHER.  I don't like
 *	the notion of inverted priorites for normal processes when
 *      you can use "setpriority" for that.
 *
 */

/* Macros to convert between the unix (lower numerically is higher priority)
 * and POSIX 1003.1b (higher numerically is higher priority)
 */

#define p4prio_to_rtpprio(P) (RTP_PRIO_MAX - (P))
#define rtpprio_to_p4prio(P) (RTP_PRIO_MAX - (P))

#define p4prio_to_tsprio(P) ((PRI_MAX_TIMESHARE - PRI_MIN_TIMESHARE) - (P))
#define tsprio_to_p4prio(P) ((PRI_MAX_TIMESHARE - PRI_MIN_TIMESHARE) - (P))

/* These improve readability a bit for me:
 */
#define P1B_PRIO_MIN rtpprio_to_p4prio(RTP_PRIO_MAX)
#define P1B_PRIO_MAX rtpprio_to_p4prio(RTP_PRIO_MIN)

static __inline int
getscheduler(struct ksched *ksched, struct thread *td, int *policy)
{
	struct rtprio rtp;
	int e;

	e = 0;
	pri_to_rtp(td, &rtp);
	switch (rtp.type) {
	case RTP_PRIO_FIFO:
		*policy = SCHED_FIFO;
		break;
	case RTP_PRIO_REALTIME:
		*policy = SCHED_RR;
		break;
	default:
		*policy = SCHED_OTHER;
		break;
	}
	return (e);
}

int
ksched_setparam(struct ksched *ksched,
    struct thread *td, const struct sched_param *param)
{
	int e, policy;

	e = getscheduler(ksched, td, &policy);
	if (e == 0)
		e = ksched_setscheduler(ksched, td, policy, param);
	return (e);
}

int
ksched_getparam(struct ksched *ksched, struct thread *td,
    struct sched_param *param)
{
	struct rtprio rtp;

	pri_to_rtp(td, &rtp);
	if (RTP_PRIO_IS_REALTIME(rtp.type))
		param->sched_priority = rtpprio_to_p4prio(rtp.prio);
	else {
		if (PRI_MIN_TIMESHARE < rtp.prio) 
			/*
		 	 * The interactive score has it to min realtime
			 * so we must show max (64 most likely).
			 */ 
			param->sched_priority = PRI_MAX_TIMESHARE -
			    PRI_MIN_TIMESHARE;
		else
			param->sched_priority = tsprio_to_p4prio(rtp.prio);
	}
	return (0);
}

/*
 * XXX The priority and scheduler modifications should
 *     be moved into published interfaces in kern/kern_sync.
 *
 * The permissions to modify process p were checked in "p31b_proc()".
 *
 */
int
ksched_setscheduler(struct ksched *ksched, struct thread *td, int policy,
    const struct sched_param *param)
{
	struct rtprio rtp;
	int e;

	e = 0;
	switch(policy) {
	case SCHED_RR:
	case SCHED_FIFO:
		if (param->sched_priority >= P1B_PRIO_MIN &&
		    param->sched_priority <= P1B_PRIO_MAX) {
			rtp.prio = p4prio_to_rtpprio(param->sched_priority);
			rtp.type = (policy == SCHED_FIFO) ? RTP_PRIO_FIFO :
			    RTP_PRIO_REALTIME;
			rtp_to_pri(&rtp, td);
		} else {
			e = EPERM;
		}
		break;
	case SCHED_OTHER:
		if (param->sched_priority >= 0 && param->sched_priority <=
		    (PRI_MAX_TIMESHARE - PRI_MIN_TIMESHARE)) {
			rtp.type = RTP_PRIO_NORMAL;
			rtp.prio = p4prio_to_tsprio(param->sched_priority);
			rtp_to_pri(&rtp, td);
		} else {
			e = EINVAL;
		}
		break;
	default:
		e = EINVAL;
		break;
	}
	return (e);
}

int
ksched_getscheduler(struct ksched *ksched, struct thread *td, int *policy)
{

	return (getscheduler(ksched, td, policy));
}

/* ksched_yield: Yield the CPU. */
int
ksched_yield(struct ksched *ksched)
{

	sched_relinquish(curthread);
	return (0);
}

int
ksched_get_priority_max(struct ksched *ksched, int policy, int *prio)
{
	int e;

	e = 0;
	switch (policy)	{
	case SCHED_FIFO:
	case SCHED_RR:
		*prio = P1B_PRIO_MAX;
		break;
	case SCHED_OTHER:
		*prio = PRI_MAX_TIMESHARE - PRI_MIN_TIMESHARE;
		break;
	default:
		e = EINVAL;
		break;
	}
	return (e);
}

int
ksched_get_priority_min(struct ksched *ksched, int policy, int *prio)
{
	int e;

	e = 0;
	switch (policy)	{
	case SCHED_FIFO:
	case SCHED_RR:
		*prio = P1B_PRIO_MIN;
		break;
	case SCHED_OTHER:
		*prio = 0;
		break;
	default:
		e = EINVAL;
		break;
	}
	return (e);
}

int
ksched_rr_get_interval(struct ksched *ksched, struct thread *td,
    struct timespec *timespec)
{

	*timespec = ksched->rr_interval;
	return (0);
}
