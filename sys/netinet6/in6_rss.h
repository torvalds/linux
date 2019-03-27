/*-
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef _NETINET6_IN6_RSS_H_
#define	_NETINET6_IN6_RSS_H_

#include <netinet/in.h>		/* in_addr_t */

/*
 * Network stack interface to generate a hash for a protocol tuple.
 */
uint32_t	rss_hash_ip6_4tuple(const struct in6_addr *src, u_short srcport,
		    const struct in6_addr *dst, u_short dstport);
uint32_t	rss_hash_ip6_2tuple(const struct in6_addr *src,
		    const struct in6_addr *dst);

/*
 * Functions to calculate a software RSS hash for a given mbuf or
 * packet detail.
 */
int		rss_mbuf_software_hash_v6(const struct mbuf *m, int dir,
		    uint32_t *hashval, uint32_t *hashtype);
int		rss_proto_software_hash_v6(const struct in6_addr *src,
		    const struct in6_addr *dst, u_short src_port,
		    u_short dst_port, int proto, uint32_t *hashval,
		    uint32_t *hashtype);
struct mbuf *	rss_soft_m2cpuid_v6(struct mbuf *m, uintptr_t source,
		    u_int *cpuid);

#endif /* !_NETINET6_IN6_RSS_H_ */
