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

#ifndef _SYS_RTPRIO_H_
#define _SYS_RTPRIO_H_

#include <sys/priority.h>

/*
 * Process realtime-priority specifications to rtprio.
 */

/* priority types.  Start at 1 to catch uninitialized fields. */

#define RTP_PRIO_REALTIME	PRI_REALTIME	/* real time process */
#define RTP_PRIO_NORMAL		PRI_TIMESHARE	/* time sharing process */
#define RTP_PRIO_IDLE		PRI_IDLE	/* idle process */

/* RTP_PRIO_FIFO is POSIX.1B SCHED_FIFO.
 */

#define RTP_PRIO_FIFO_BIT	PRI_FIFO_BIT
#define RTP_PRIO_FIFO		PRI_FIFO
#define RTP_PRIO_BASE(P)	PRI_BASE(P)
#define RTP_PRIO_IS_REALTIME(P) PRI_IS_REALTIME(P)
#define RTP_PRIO_NEED_RR(P)	PRI_NEED_RR(P)

/* priority range */
#define RTP_PRIO_MIN		0	/* Highest priority */
#define RTP_PRIO_MAX		31	/* Lowest priority */

/*
 * rtprio() syscall functions
 */
#define RTP_LOOKUP		0
#define RTP_SET			1

#ifndef LOCORE
/*
 * Scheduling class information.
 */
struct rtprio {
	u_short type;			/* scheduling class */
	u_short prio;
};

#ifdef _KERNEL
struct thread;
int	rtp_to_pri(struct rtprio *, struct thread *);
void	pri_to_rtp(struct thread *, struct rtprio *);
#endif
#endif

#ifndef _KERNEL
#include <sys/cdefs.h>

__BEGIN_DECLS
int	rtprio(int, pid_t, struct rtprio *);
int	rtprio_thread(int, lwpid_t, struct rtprio *);
__END_DECLS
#endif	/* !_KERNEL */
#endif	/* !_SYS_RTPRIO_H_ */
