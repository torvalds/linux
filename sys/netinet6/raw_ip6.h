/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 *	$KAME: raw_ip6.h,v 1.2 2001/05/27 13:28:35 itojun Exp $
 * $FreeBSD$
 */

#ifndef _NETINET6_RAW_IP6_H_
#define _NETINET6_RAW_IP6_H_

/*
 * ICMPv6 stat is counted separately.  see netinet/icmp6.h
 */
struct rip6stat {
	uint64_t rip6s_ipackets;	/* total input packets */
	uint64_t rip6s_isum;		/* input checksum computations */
	uint64_t rip6s_badsum;		/* of above, checksum error */
	uint64_t rip6s_nosock;		/* no matching socket */
	uint64_t rip6s_nosockmcast;	/* of above, arrived as multicast */
	uint64_t rip6s_fullsock;	/* not delivered, input socket full */

	uint64_t rip6s_opackets;	/* total output packets */
};

#ifdef _KERNEL
#include <sys/counter.h>

VNET_PCPUSTAT_DECLARE(struct rip6stat, rip6stat);
#define	RIP6STAT_ADD(name, val)	\
    VNET_PCPUSTAT_ADD(struct rip6stat, rip6stat, name, (val))
#define	RIP6STAT_INC(name)	RIP6STAT_ADD(name, 1)
#endif /* _KERNEL */

#endif
