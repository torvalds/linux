/*	$OpenBSD: igmp_var.h,v 1.16 2025/03/02 21:28:32 bluhm Exp $	*/
/*	$NetBSD: igmp_var.h,v 1.9 1996/02/13 23:41:31 christos Exp $	*/

/*
 * Copyright (c) 1988 Stephen Deering.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
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
 *	@(#)igmp_var.h	8.1 (Berkeley) 7/19/93
 */

#ifndef _NETINET_IGMP_VAR_H_
#define _NETINET_IGMP_VAR_H_

/*
 * Internet Group Management Protocol (IGMP),
 * implementation-specific definitions.
 *
 * Written by Steve Deering, Stanford, May 1988.
 * Modified by Rosen Sharma, Stanford, Aug 1994.
 * Modified by Bill Fenner, Xerox PARC, Feb 1995.
 *
 * MULTICAST 1.3
 */

struct igmpstat {
	u_long	igps_rcv_total;		/* total IGMP messages received */
	u_long	igps_rcv_tooshort;	/* received with too few bytes */
	u_long	igps_rcv_badsum;	/* received with bad checksum */
	u_long	igps_rcv_queries;	/* received membership queries */
	u_long	igps_rcv_badqueries;	/* received invalid queries */
	u_long	igps_rcv_reports;	/* received membership reports */
	u_long	igps_rcv_badreports;	/* received invalid reports */
	u_long	igps_rcv_ourreports;	/* received reports for our groups */
	u_long	igps_snd_reports;	/* sent membership reports */
};

/*
 * Names for IGMP sysctl objects
 */
#define IGMPCTL_STATS		1	/* IGMP statistics */
#define IGMPCTL_MAXID		2

#define IGMPCTL_NAMES { \
	{ 0, 0 }, \
	{ "stats",	CTLTYPE_STRUCT } \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum igmpstat_counters {
	igps_rcv_total,		/* total IGMP messages received */
	igps_rcv_tooshort,	/* received with too few bytes */
	igps_rcv_badsum,	/* received with bad checksum */
	igps_rcv_queries,	/* received membership queries */
	igps_rcv_badqueries,	/* received invalid queries */
	igps_rcv_reports,	/* received membership reports */
	igps_rcv_badreports,	/* received invalid reports */
	igps_rcv_ourreports,	/* received reports for our groups */
	igps_snd_reports,	/* sent membership reports */
	igps_ncounters
};

extern struct cpumem *igmpcounters;

static inline void
igmpstat_inc(enum igmpstat_counters c)
{
	counters_inc(igmpcounters, c);
}

/*
 * Macro to compute a random timer value between 1 and (IGMP_MAX_REPORTING_
 * DELAY * countdown frequency).  We assume that the routine random()
 * is defined somewhere (and that it returns a positive number).
 */
#define	IGMP_RANDOM_DELAY(X)	(arc4random_uniform(X) + 1)

void	igmp_init(void);
int	igmp_input(struct mbuf **, int *, int, int, struct netstack *);
void	igmp_joingroup(struct in_multi *, struct ifnet *);
void	igmp_leavegroup(struct in_multi *, struct ifnet *);
void	igmp_fasttimo(void);
void	igmp_slowtimo(void);
int	igmp_sysctl(int *, u_int, void *, size_t *, void *, size_t);
#endif /* _KERNEL */
#endif /* _NETINET_IGMP_VAR_H_ */
