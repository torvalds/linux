/*	$OpenBSD: icmp_var.h,v 1.16 2020/08/22 17:55:54 gnezdo Exp $	*/
/*	$NetBSD: icmp_var.h,v 1.8 1995/03/26 20:32:19 jtc Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)icmp_var.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_ICMP_VAR_H_
#define _NETINET_ICMP_VAR_H_

/*
 * Variables related to this implementation
 * of the internet control message protocol.
 */
struct	icmpstat {
/* statistics related to icmp packets generated */
	u_long	icps_error;		/* # of calls to icmp_error */
	u_long	icps_toofreq;		/* no error because rate limiter */
	u_long	icps_oldshort;		/* no error because old ip too short */
	u_long	icps_oldicmp;		/* no error because old was icmp */
	u_long	icps_outhist[ICMP_MAXTYPE + 1];
/* statistics related to input messages processed */
	u_long	icps_badcode;		/* icmp_code out of range */
	u_long	icps_tooshort;		/* packet < ICMP_MINLEN */
	u_long	icps_checksum;		/* bad checksum */
	u_long	icps_badlen;		/* calculated bound mismatch */
	u_long	icps_reflect;		/* number of responses */
	u_long	icps_bmcastecho;	/* rejected broadcast icmps */
	u_long	icps_inhist[ICMP_MAXTYPE + 1];
};

/*
 * Names for ICMP sysctl objects
 */
#define	ICMPCTL_MASKREPL	1	/* allow replies to netmask requests */
#define ICMPCTL_BMCASTECHO	2	/* reply to icmps to broadcast/mcast */
#define ICMPCTL_ERRPPSLIMIT	3	/* ICMP error pps limitation */
#define	ICMPCTL_REDIRACCEPT	4	/* Accept redirects from routers */
#define	ICMPCTL_REDIRTIMEOUT	5	/* Remove routes added via redirects */
#define	ICMPCTL_TSTAMPREPL	6	/* allow replies to timestamp requests */
#define ICMPCTL_STATS		7	/* ICMP statistics */
#define ICMPCTL_MAXID		8

#define ICMPCTL_NAMES { \
	{ 0, 0 }, \
	{ "maskrepl", CTLTYPE_INT }, \
	{ "bmcastecho", CTLTYPE_INT }, \
	{ "errppslimit", CTLTYPE_INT }, \
	{ "rediraccept", CTLTYPE_INT }, \
	{ "redirtimeout", CTLTYPE_INT }, \
	{ "tstamprepl", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT } \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum icmpstat_counters {
	icps_error,
	icps_toofreq,
	icps_oldshort,
	icps_oldicmp,
	icps_outhist,
	icps_badcode = icps_outhist + ICMP_MAXTYPE + 1,
	icps_tooshort,
	icps_checksum,
	icps_badlen,
	icps_reflect,
	icps_bmcastecho,
	icps_inhist,
	icps_ncounters = icps_inhist + ICMP_MAXTYPE + 1
};

extern struct cpumem *icmpcounters;

static inline void
icmpstat_inc(enum icmpstat_counters c)
{
	counters_inc(icmpcounters, c);
}

#endif /* _KERNEL */
#endif /* _NETINET_ICMP_VAR_H_ */
