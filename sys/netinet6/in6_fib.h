/*-
 * Copyright (c) 2015
 * 	Alexander V. Chernikov <melifaro@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef _NETINET6_IN6_FIB_H_
#define	_NETINET6_IN6_FIB_H_

/* Basic nexthop info used for uRPF/mtu checks */
struct nhop6_basic {
	struct ifnet	*nh_ifp;	/* Logical egress interface */
	uint16_t	nh_mtu;		/* nexthop mtu */
	uint16_t	nh_flags;	/* nhop flags */
	uint8_t		spare[4];
	struct in6_addr	nh_addr;	/* GW/DST IPv4 address */
};

/* Does not differ from nhop6_basic */
struct nhop6_extended {
	struct ifnet	*nh_ifp;	/* Logical egress interface */
	uint16_t	nh_mtu;		/* nexthop mtu */
	uint16_t	nh_flags;	/* nhop flags */
	uint8_t		spare[4];
	struct in6_addr	nh_addr;	/* GW/DST IPv6 address */
	uint64_t	spare2[2];
};

int fib6_lookup_nh_basic(uint32_t fibnum, const struct in6_addr *dst,
    uint32_t scopeid, uint32_t flags, uint32_t flowid,struct nhop6_basic *pnh6);
int fib6_lookup_nh_ext(uint32_t fibnum, const struct in6_addr *dst,
    uint32_t scopeid, uint32_t flags, uint32_t flowid,
    struct nhop6_extended *pnh6);
void fib6_free_nh_ext(uint32_t fibnum, struct nhop6_extended *pnh6);
#endif

