/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994, Henrik Vestergaard Draboel
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
 *	This product includes software developed by Henrik Vestergaard Draboel.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef _SYS_PRIORITY_H_
#define _SYS_PRIORITY_H_

/*
 * Process priority specifications.
 */

/*
 * Priority classes.
 */

#define	PRI_ITHD		1	/* Interrupt thread. */
#define	PRI_REALTIME		2	/* Real time process. */
#define	PRI_TIMESHARE		3	/* Time sharing process. */
#define	PRI_IDLE		4	/* Idle process. */

/*
 * PRI_FIFO is POSIX.1B SCHED_FIFO.
 */

#define	PRI_FIFO_BIT		8
#define	PRI_FIFO		(PRI_FIFO_BIT | PRI_REALTIME)

#define	PRI_BASE(P)		((P) & ~PRI_FIFO_BIT)
#define	PRI_IS_REALTIME(P)	(PRI_BASE(P) == PRI_REALTIME)
#define	PRI_NEED_RR(P)		((P) != PRI_FIFO)

/*
 * Priorities.  Note that with 64 run queues, differences less than 4 are
 * insignificant.
 */

/*
 * Priorities range from 0 to 255, but differences of less then 4 (RQ_PPQ)
 * are insignificant.  Ranges are as follows:
 *
 * Interrupt threads:		0 - 47
 * Realtime user threads:	48 - 79
 * Top half kernel threads:	80 - 119
 * Time sharing user threads:	120 - 223
 * Idle user threads:		224 - 255
 *
 * XXX If/When the specific interrupt thread and top half thread ranges
 * disappear, a larger range can be used for user processes.
 */

#define	PRI_MIN			(0)		/* Highest priority. */
#define	PRI_MAX			(255)		/* Lowest priority. */

#define	PRI_MIN_ITHD		(PRI_MIN)
#define	PRI_MAX_ITHD		(PRI_MIN_REALTIME - 1)

#define	PI_REALTIME		(PRI_MIN_ITHD + 0)
#define	PI_AV			(PRI_MIN_ITHD + 4)
#define	PI_NET			(PRI_MIN_ITHD + 8)
#define	PI_DISK			(PRI_MIN_ITHD + 12)
#define	PI_TTY			(PRI_MIN_ITHD + 16)
#define	PI_DULL			(PRI_MIN_ITHD + 20)
#define	PI_SOFT			(PRI_MIN_ITHD + 24)
#define	PI_SWI(x)		(PI_SOFT + (x) * RQ_PPQ)

#define	PRI_MIN_REALTIME	(48)
#define	PRI_MAX_REALTIME	(PRI_MIN_KERN - 1)

#define	PRI_MIN_KERN		(80)
#define	PRI_MAX_KERN		(PRI_MIN_TIMESHARE - 1)

#define	PSWP			(PRI_MIN_KERN + 0)
#define	PVM			(PRI_MIN_KERN + 4)
#define	PINOD			(PRI_MIN_KERN + 8)
#define	PRIBIO			(PRI_MIN_KERN + 12)
#define	PVFS			(PRI_MIN_KERN + 16)
#define	PZERO			(PRI_MIN_KERN + 20)
#define	PSOCK			(PRI_MIN_KERN + 24)
#define	PWAIT			(PRI_MIN_KERN + 28)
#define	PLOCK			(PRI_MIN_KERN + 32)
#define	PPAUSE			(PRI_MIN_KERN + 36)

#define	PRI_MIN_TIMESHARE	(120)
#define	PRI_MAX_TIMESHARE	(PRI_MIN_IDLE - 1)

#define	PUSER			(PRI_MIN_TIMESHARE)

#define	PRI_MIN_IDLE		(224)
#define	PRI_MAX_IDLE		(PRI_MAX)

#ifdef _KERNEL
/* Other arguments for kern_yield(9). */
#define	PRI_USER	-2	/* Change to current user priority. */
#define	PRI_UNCHANGED	-1	/* Do not change priority. */
#endif

struct priority {
	u_char	pri_class;	/* Scheduling class. */
	u_char	pri_level;	/* Normal priority level. */
	u_char	pri_native;	/* Priority before propagation. */
	u_char	pri_user;	/* User priority based on p_cpu and p_nice. */
};

#endif	/* !_SYS_PRIORITY_H_ */
