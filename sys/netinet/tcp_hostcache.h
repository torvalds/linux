/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Andre Oppermann, Internet Business Solutions AG
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/*
 * Many thanks to jlemon for basic structure of tcp_syncache which is being
 * followed here.
 */

#ifndef _NETINET_TCP_HOSTCACHE_H_
#define _NETINET_TCP_HOSTCACHE_H_

TAILQ_HEAD(hc_qhead, hc_metrics);

struct hc_head {
	struct hc_qhead	hch_bucket;
	u_int		hch_length;
	struct mtx	hch_mtx;
};

struct hc_metrics {
	/* housekeeping */
	TAILQ_ENTRY(hc_metrics) rmx_q;
	struct		hc_head *rmx_head; /* head of bucket tail queue */
	struct		in_addr ip4;	/* IP address */
	struct		in6_addr ip6;	/* IP6 address */
	uint32_t	ip6_zoneid;	/* IPv6 scope zone id */
	/* endpoint specific values for tcp */
	uint32_t	rmx_mtu;	/* MTU for this path */
	uint32_t	rmx_ssthresh;	/* outbound gateway buffer limit */
	uint32_t	rmx_rtt;	/* estimated round trip time */
	uint32_t	rmx_rttvar;	/* estimated rtt variance */
	uint32_t	rmx_cwnd;	/* congestion window */
	uint32_t	rmx_sendpipe;	/* outbound delay-bandwidth product */
	uint32_t	rmx_recvpipe;	/* inbound delay-bandwidth product */
	/* TCP hostcache internal data */
	int		rmx_expire;	/* lifetime for object */
	u_long		rmx_hits;	/* number of hits */
	u_long		rmx_updates;	/* number of updates */
};

struct tcp_hostcache {
	struct	hc_head *hashbase;
	uma_zone_t zone;
	u_int	hashsize;
	u_int	hashmask;
	u_int	bucket_limit;
	u_int	cache_count;
	u_int	cache_limit;
	int	expire;
	int	prune;
	int	purgeall;
};

#endif /* !_NETINET_TCP_HOSTCACHE_H_*/
