/*	$OpenBSD: raw_ip6.h,v 1.4 2017/02/09 15:23:35 jca Exp $	*/
/*	$KAME: raw_ip6.h,v 1.2 2001/05/27 13:28:35 itojun Exp $	*/

/*
 * Copyright (C) 2001 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETINET6_RAW_IP6_H_
#define _NETINET6_RAW_IP6_H_

/*
 * ICMPv6 stat is counted separately.  see netinet/icmp6.h
 */
struct rip6stat {
	u_int64_t rip6s_ipackets;	/* total input packets */
	u_int64_t rip6s_isum;		/* input checksum computations */
	u_int64_t rip6s_badsum;		/* of above, checksum error */
	u_int64_t rip6s_nosock;		/* no matching socket */
	u_int64_t rip6s_nosockmcast;	/* of above, arrived as multicast */
	u_int64_t rip6s_fullsock;	/* not delivered, input socket full */

	u_int64_t rip6s_opackets;	/* total output packets */
};

/*
 * Names for RIP6 sysctl objects
 */
#define RIPV6CTL_STATS		1	/* RIP6 stats */
#define RIPV6CTL_MAXID		2

#define RIPM6CTL_NAMES { \
	{ 0, 0 }, \
	{ "stats", CTLTYPE_NODE }, \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum rip6stat_counters {
	rip6s_ipackets,
	rip6s_isum,
	rip6s_badsum,
	rip6s_nosock,
	rip6s_nosockmcast,
	rip6s_fullsock,
	rip6s_opackets,
	rip6s_ncounters,
};

extern struct cpumem *rip6counters;

static inline void
rip6stat_inc(enum rip6stat_counters c)
{
	counters_inc(rip6counters, c);
}

#endif

#endif
