/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, Myricom Inc.
 * Copyright (c) 2008, Intel Corporation.
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2016 Mellanox Technologies.
 * All rights reserved.
 *
 * Portions of this software were developed by Bjoern Zeeb
 * under sponsorship from the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/vnet.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_lro.h>
#include <netinet/tcp_var.h>

#include <netinet6/ip6_var.h>

#include <machine/in_cksum.h>

static MALLOC_DEFINE(M_LRO, "LRO", "LRO control structures");

#define	TCP_LRO_UPDATE_CSUM	1
#ifndef	TCP_LRO_UPDATE_CSUM
#define	TCP_LRO_INVALID_CSUM	0x0000
#endif

static void	tcp_lro_rx_done(struct lro_ctrl *lc);
static int	tcp_lro_rx2(struct lro_ctrl *lc, struct mbuf *m,
		    uint32_t csum, int use_hash);

SYSCTL_NODE(_net_inet_tcp, OID_AUTO, lro,  CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "TCP LRO");

static unsigned	tcp_lro_entries = TCP_LRO_ENTRIES;
SYSCTL_UINT(_net_inet_tcp_lro, OID_AUTO, entries,
    CTLFLAG_RDTUN | CTLFLAG_MPSAFE, &tcp_lro_entries, 0,
    "default number of LRO entries");

static __inline void
tcp_lro_active_insert(struct lro_ctrl *lc, struct lro_head *bucket,
    struct lro_entry *le)
{

	LIST_INSERT_HEAD(&lc->lro_active, le, next);
	LIST_INSERT_HEAD(bucket, le, hash_next);
}

static __inline void
tcp_lro_active_remove(struct lro_entry *le)
{

	LIST_REMOVE(le, next);		/* active list */
	LIST_REMOVE(le, hash_next);	/* hash bucket */
}

int
tcp_lro_init(struct lro_ctrl *lc)
{
	return (tcp_lro_init_args(lc, NULL, tcp_lro_entries, 0));
}

int
tcp_lro_init_args(struct lro_ctrl *lc, struct ifnet *ifp,
    unsigned lro_entries, unsigned lro_mbufs)
{
	struct lro_entry *le;
	size_t size;
	unsigned i, elements;

	lc->lro_bad_csum = 0;
	lc->lro_queued = 0;
	lc->lro_flushed = 0;
	lc->lro_mbuf_count = 0;
	lc->lro_mbuf_max = lro_mbufs;
	lc->lro_cnt = lro_entries;
	lc->lro_ackcnt_lim = TCP_LRO_ACKCNT_MAX;
	lc->lro_length_lim = TCP_LRO_LENGTH_MAX;
	lc->ifp = ifp;
	LIST_INIT(&lc->lro_free);
	LIST_INIT(&lc->lro_active);

	/* create hash table to accelerate entry lookup */
	if (lro_entries > lro_mbufs)
		elements = lro_entries;
	else
		elements = lro_mbufs;
	lc->lro_hash = phashinit_flags(elements, M_LRO, &lc->lro_hashsz,
	    HASH_NOWAIT);
	if (lc->lro_hash == NULL) {
		memset(lc, 0, sizeof(*lc));
		return (ENOMEM);
	}

	/* compute size to allocate */
	size = (lro_mbufs * sizeof(struct lro_mbuf_sort)) +
	    (lro_entries * sizeof(*le));
	lc->lro_mbuf_data = (struct lro_mbuf_sort *)
	    malloc(size, M_LRO, M_NOWAIT | M_ZERO);

	/* check for out of memory */
	if (lc->lro_mbuf_data == NULL) {
		free(lc->lro_hash, M_LRO);
		memset(lc, 0, sizeof(*lc));
		return (ENOMEM);
	}
	/* compute offset for LRO entries */
	le = (struct lro_entry *)
	    (lc->lro_mbuf_data + lro_mbufs);

	/* setup linked list */
	for (i = 0; i != lro_entries; i++)
		LIST_INSERT_HEAD(&lc->lro_free, le + i, next);

	return (0);
}

void
tcp_lro_free(struct lro_ctrl *lc)
{
	struct lro_entry *le;
	unsigned x;

	/* reset LRO free list */
	LIST_INIT(&lc->lro_free);

	/* free active mbufs, if any */
	while ((le = LIST_FIRST(&lc->lro_active)) != NULL) {
		tcp_lro_active_remove(le);
		m_freem(le->m_head);
	}

	/* free hash table */
	free(lc->lro_hash, M_LRO);
	lc->lro_hash = NULL;
	lc->lro_hashsz = 0;

	/* free mbuf array, if any */
	for (x = 0; x != lc->lro_mbuf_count; x++)
		m_freem(lc->lro_mbuf_data[x].mb);
	lc->lro_mbuf_count = 0;

	/* free allocated memory, if any */
	free(lc->lro_mbuf_data, M_LRO);
	lc->lro_mbuf_data = NULL;
}

#ifdef TCP_LRO_UPDATE_CSUM
static uint16_t
tcp_lro_csum_th(struct tcphdr *th)
{
	uint32_t ch;
	uint16_t *p, l;

	ch = th->th_sum = 0x0000;
	l = th->th_off;
	p = (uint16_t *)th;
	while (l > 0) {
		ch += *p;
		p++;
		ch += *p;
		p++;
		l--;
	}
	while (ch > 0xffff)
		ch = (ch >> 16) + (ch & 0xffff);

	return (ch & 0xffff);
}

static uint16_t
tcp_lro_rx_csum_fixup(struct lro_entry *le, void *l3hdr, struct tcphdr *th,
    uint16_t tcp_data_len, uint16_t csum)
{
	uint32_t c;
	uint16_t cs;

	c = csum;

	/* Remove length from checksum. */
	switch (le->eh_type) {
#ifdef INET6
	case ETHERTYPE_IPV6:
	{
		struct ip6_hdr *ip6;

		ip6 = (struct ip6_hdr *)l3hdr;
		if (le->append_cnt == 0)
			cs = ip6->ip6_plen;
		else {
			uint32_t cx;

			cx = ntohs(ip6->ip6_plen);
			cs = in6_cksum_pseudo(ip6, cx, ip6->ip6_nxt, 0);
		}
		break;
	}
#endif
#ifdef INET
	case ETHERTYPE_IP:
	{
		struct ip *ip4;

		ip4 = (struct ip *)l3hdr;
		if (le->append_cnt == 0)
			cs = ip4->ip_len;
		else {
			cs = in_addword(ntohs(ip4->ip_len) - sizeof(*ip4),
			    IPPROTO_TCP);
			cs = in_pseudo(ip4->ip_src.s_addr, ip4->ip_dst.s_addr,
			    htons(cs));
		}
		break;
	}
#endif
	default:
		cs = 0;		/* Keep compiler happy. */
	}

	cs = ~cs;
	c += cs;

	/* Remove TCP header csum. */
	cs = ~tcp_lro_csum_th(th);
	c += cs;
	while (c > 0xffff)
		c = (c >> 16) + (c & 0xffff);

	return (c & 0xffff);
}
#endif

static void
tcp_lro_rx_done(struct lro_ctrl *lc)
{
	struct lro_entry *le;

	while ((le = LIST_FIRST(&lc->lro_active)) != NULL) {
		tcp_lro_active_remove(le);
		tcp_lro_flush(lc, le);
	}
}

void
tcp_lro_flush_inactive(struct lro_ctrl *lc, const struct timeval *timeout)
{
	struct lro_entry *le, *le_tmp;
	struct timeval tv;

	if (LIST_EMPTY(&lc->lro_active))
		return;

	getmicrotime(&tv);
	timevalsub(&tv, timeout);
	LIST_FOREACH_SAFE(le, &lc->lro_active, next, le_tmp) {
		if (timevalcmp(&tv, &le->mtime, >=)) {
			tcp_lro_active_remove(le);
			tcp_lro_flush(lc, le);
		}
	}
}

void
tcp_lro_flush(struct lro_ctrl *lc, struct lro_entry *le)
{

	if (le->append_cnt > 0) {
		struct tcphdr *th;
		uint16_t p_len;

		p_len = htons(le->p_len);
		switch (le->eh_type) {
#ifdef INET6
		case ETHERTYPE_IPV6:
		{
			struct ip6_hdr *ip6;

			ip6 = le->le_ip6;
			ip6->ip6_plen = p_len;
			th = (struct tcphdr *)(ip6 + 1);
			le->m_head->m_pkthdr.csum_flags = CSUM_DATA_VALID |
			    CSUM_PSEUDO_HDR;
			le->p_len += ETHER_HDR_LEN + sizeof(*ip6);
			break;
		}
#endif
#ifdef INET
		case ETHERTYPE_IP:
		{
			struct ip *ip4;
#ifdef TCP_LRO_UPDATE_CSUM
			uint32_t cl;
			uint16_t c;
#endif

			ip4 = le->le_ip4;
#ifdef TCP_LRO_UPDATE_CSUM
			/* Fix IP header checksum for new length. */
			c = ~ip4->ip_sum;
			cl = c;
			c = ~ip4->ip_len;
			cl += c + p_len;
			while (cl > 0xffff)
				cl = (cl >> 16) + (cl & 0xffff);
			c = cl;
			ip4->ip_sum = ~c;
#else
			ip4->ip_sum = TCP_LRO_INVALID_CSUM;
#endif
			ip4->ip_len = p_len;
			th = (struct tcphdr *)(ip4 + 1);
			le->m_head->m_pkthdr.csum_flags = CSUM_DATA_VALID |
			    CSUM_PSEUDO_HDR | CSUM_IP_CHECKED | CSUM_IP_VALID;
			le->p_len += ETHER_HDR_LEN;
			break;
		}
#endif
		default:
			th = NULL;	/* Keep compiler happy. */
		}
		le->m_head->m_pkthdr.csum_data = 0xffff;
		le->m_head->m_pkthdr.len = le->p_len;

		/* Incorporate the latest ACK into the TCP header. */
		th->th_ack = le->ack_seq;
		th->th_win = le->window;
		/* Incorporate latest timestamp into the TCP header. */
		if (le->timestamp != 0) {
			uint32_t *ts_ptr;

			ts_ptr = (uint32_t *)(th + 1);
			ts_ptr[1] = htonl(le->tsval);
			ts_ptr[2] = le->tsecr;
		}
#ifdef TCP_LRO_UPDATE_CSUM
		/* Update the TCP header checksum. */
		le->ulp_csum += p_len;
		le->ulp_csum += tcp_lro_csum_th(th);
		while (le->ulp_csum > 0xffff)
			le->ulp_csum = (le->ulp_csum >> 16) +
			    (le->ulp_csum & 0xffff);
		th->th_sum = (le->ulp_csum & 0xffff);
		th->th_sum = ~th->th_sum;
#else
		th->th_sum = TCP_LRO_INVALID_CSUM;
#endif
	}

	le->m_head->m_pkthdr.lro_nsegs = le->append_cnt + 1;
	(*lc->ifp->if_input)(lc->ifp, le->m_head);
	lc->lro_queued += le->append_cnt + 1;
	lc->lro_flushed++;
	bzero(le, sizeof(*le));
	LIST_INSERT_HEAD(&lc->lro_free, le, next);
}

#ifdef HAVE_INLINE_FLSLL
#define	tcp_lro_msb_64(x) (1ULL << (flsll(x) - 1))
#else
static inline uint64_t
tcp_lro_msb_64(uint64_t x)
{
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	x |= (x >> 32);
	return (x & ~(x >> 1));
}
#endif

/*
 * The tcp_lro_sort() routine is comparable to qsort(), except it has
 * a worst case complexity limit of O(MIN(N,64)*N), where N is the
 * number of elements to sort and 64 is the number of sequence bits
 * available. The algorithm is bit-slicing the 64-bit sequence number,
 * sorting one bit at a time from the most significant bit until the
 * least significant one, skipping the constant bits. This is
 * typically called a radix sort.
 */
static void
tcp_lro_sort(struct lro_mbuf_sort *parray, uint32_t size)
{
	struct lro_mbuf_sort temp;
	uint64_t ones;
	uint64_t zeros;
	uint32_t x;
	uint32_t y;

repeat:
	/* for small arrays insertion sort is faster */
	if (size <= 12) {
		for (x = 1; x < size; x++) {
			temp = parray[x];
			for (y = x; y > 0 && temp.seq < parray[y - 1].seq; y--)
				parray[y] = parray[y - 1];
			parray[y] = temp;
		}
		return;
	}

	/* compute sequence bits which are constant */
	ones = 0;
	zeros = 0;
	for (x = 0; x != size; x++) {
		ones |= parray[x].seq;
		zeros |= ~parray[x].seq;
	}

	/* compute bits which are not constant into "ones" */
	ones &= zeros;
	if (ones == 0)
		return;

	/* pick the most significant bit which is not constant */
	ones = tcp_lro_msb_64(ones);

	/*
	 * Move entries having cleared sequence bits to the beginning
	 * of the array:
	 */
	for (x = y = 0; y != size; y++) {
		/* skip set bits */
		if (parray[y].seq & ones)
			continue;
		/* swap entries */
		temp = parray[x];
		parray[x] = parray[y];
		parray[y] = temp;
		x++;
	}

	KASSERT(x != 0 && x != size, ("Memory is corrupted\n"));

	/* sort zeros */
	tcp_lro_sort(parray, x);

	/* sort ones */
	parray += x;
	size -= x;
	goto repeat;
}

void
tcp_lro_flush_all(struct lro_ctrl *lc)
{
	uint64_t seq;
	uint64_t nseq;
	unsigned x;

	/* check if no mbufs to flush */
	if (lc->lro_mbuf_count == 0)
		goto done;

	/* sort all mbufs according to stream */
	tcp_lro_sort(lc->lro_mbuf_data, lc->lro_mbuf_count);

	/* input data into LRO engine, stream by stream */
	seq = 0;
	for (x = 0; x != lc->lro_mbuf_count; x++) {
		struct mbuf *mb;

		/* get mbuf */
		mb = lc->lro_mbuf_data[x].mb;

		/* get sequence number, masking away the packet index */
		nseq = lc->lro_mbuf_data[x].seq & (-1ULL << 24);

		/* check for new stream */
		if (seq != nseq) {
			seq = nseq;

			/* flush active streams */
			tcp_lro_rx_done(lc);
		}

		/* add packet to LRO engine */
		if (tcp_lro_rx2(lc, mb, 0, 0) != 0) {
			/* input packet to network layer */
			(*lc->ifp->if_input)(lc->ifp, mb);
			lc->lro_queued++;
			lc->lro_flushed++;
		}
	}
done:
	/* flush active streams */
	tcp_lro_rx_done(lc);

	lc->lro_mbuf_count = 0;
}

#ifdef INET6
static int
tcp_lro_rx_ipv6(struct lro_ctrl *lc, struct mbuf *m, struct ip6_hdr *ip6,
    struct tcphdr **th)
{

	/* XXX-BZ we should check the flow-label. */

	/* XXX-BZ We do not yet support ext. hdrs. */
	if (ip6->ip6_nxt != IPPROTO_TCP)
		return (TCP_LRO_NOT_SUPPORTED);

	/* Find the TCP header. */
	*th = (struct tcphdr *)(ip6 + 1);

	return (0);
}
#endif

#ifdef INET
static int
tcp_lro_rx_ipv4(struct lro_ctrl *lc, struct mbuf *m, struct ip *ip4,
    struct tcphdr **th)
{
	int csum_flags;
	uint16_t csum;

	if (ip4->ip_p != IPPROTO_TCP)
		return (TCP_LRO_NOT_SUPPORTED);

	/* Ensure there are no options. */
	if ((ip4->ip_hl << 2) != sizeof (*ip4))
		return (TCP_LRO_CANNOT);

	/* .. and the packet is not fragmented. */
	if (ip4->ip_off & htons(IP_MF|IP_OFFMASK))
		return (TCP_LRO_CANNOT);

	/* Legacy IP has a header checksum that needs to be correct. */
	csum_flags = m->m_pkthdr.csum_flags;
	if (csum_flags & CSUM_IP_CHECKED) {
		if (__predict_false((csum_flags & CSUM_IP_VALID) == 0)) {
			lc->lro_bad_csum++;
			return (TCP_LRO_CANNOT);
		}
	} else {
		csum = in_cksum_hdr(ip4);
		if (__predict_false((csum) != 0)) {
			lc->lro_bad_csum++;
			return (TCP_LRO_CANNOT);
		}
	}

	/* Find the TCP header (we assured there are no IP options). */
	*th = (struct tcphdr *)(ip4 + 1);

	return (0);
}
#endif

static int
tcp_lro_rx2(struct lro_ctrl *lc, struct mbuf *m, uint32_t csum, int use_hash)
{
	struct lro_entry *le;
	struct ether_header *eh;
#ifdef INET6
	struct ip6_hdr *ip6 = NULL;	/* Keep compiler happy. */
#endif
#ifdef INET
	struct ip *ip4 = NULL;		/* Keep compiler happy. */
#endif
	struct tcphdr *th;
	void *l3hdr = NULL;		/* Keep compiler happy. */
	uint32_t *ts_ptr;
	tcp_seq seq;
	int error, ip_len, l;
	uint16_t eh_type, tcp_data_len;
	struct lro_head *bucket;
	int force_flush = 0;

	/* We expect a contiguous header [eh, ip, tcp]. */

	eh = mtod(m, struct ether_header *);
	eh_type = ntohs(eh->ether_type);
	switch (eh_type) {
#ifdef INET6
	case ETHERTYPE_IPV6:
	{
		CURVNET_SET(lc->ifp->if_vnet);
		if (V_ip6_forwarding != 0) {
			/* XXX-BZ stats but changing lro_ctrl is a problem. */
			CURVNET_RESTORE();
			return (TCP_LRO_CANNOT);
		}
		CURVNET_RESTORE();
		l3hdr = ip6 = (struct ip6_hdr *)(eh + 1);
		error = tcp_lro_rx_ipv6(lc, m, ip6, &th);
		if (error != 0)
			return (error);
		tcp_data_len = ntohs(ip6->ip6_plen);
		ip_len = sizeof(*ip6) + tcp_data_len;
		break;
	}
#endif
#ifdef INET
	case ETHERTYPE_IP:
	{
		CURVNET_SET(lc->ifp->if_vnet);
		if (V_ipforwarding != 0) {
			/* XXX-BZ stats but changing lro_ctrl is a problem. */
			CURVNET_RESTORE();
			return (TCP_LRO_CANNOT);
		}
		CURVNET_RESTORE();
		l3hdr = ip4 = (struct ip *)(eh + 1);
		error = tcp_lro_rx_ipv4(lc, m, ip4, &th);
		if (error != 0)
			return (error);
		ip_len = ntohs(ip4->ip_len);
		tcp_data_len = ip_len - sizeof(*ip4);
		break;
	}
#endif
	/* XXX-BZ what happens in case of VLAN(s)? */
	default:
		return (TCP_LRO_NOT_SUPPORTED);
	}

	/*
	 * If the frame is padded beyond the end of the IP packet, then we must
	 * trim the extra bytes off.
	 */
	l = m->m_pkthdr.len - (ETHER_HDR_LEN + ip_len);
	if (l != 0) {
		if (l < 0)
			/* Truncated packet. */
			return (TCP_LRO_CANNOT);

		m_adj(m, -l);
	}

	/*
	 * Check TCP header constraints.
	 */
	/* Ensure no bits set besides ACK or PSH. */
	if ((th->th_flags & ~(TH_ACK | TH_PUSH)) != 0) {
		if (th->th_flags & TH_SYN)
			return (TCP_LRO_CANNOT);
		/*
		 * Make sure that previously seen segements/ACKs are delivered
		 * before this segement, e.g. FIN.
		 */
		force_flush = 1;
	}

	/* XXX-BZ We lose a ACK|PUSH flag concatenating multiple segments. */
	/* XXX-BZ Ideally we'd flush on PUSH? */

	/*
	 * Check for timestamps.
	 * Since the only option we handle are timestamps, we only have to
	 * handle the simple case of aligned timestamps.
	 */
	l = (th->th_off << 2);
	tcp_data_len -= l;
	l -= sizeof(*th);
	ts_ptr = (uint32_t *)(th + 1);
	if (l != 0 && (__predict_false(l != TCPOLEN_TSTAMP_APPA) ||
	    (*ts_ptr != ntohl(TCPOPT_NOP<<24|TCPOPT_NOP<<16|
	    TCPOPT_TIMESTAMP<<8|TCPOLEN_TIMESTAMP)))) {
		/*
		 * Make sure that previously seen segements/ACKs are delivered
		 * before this segement.
		 */
		force_flush = 1;
	}

	/* If the driver did not pass in the checksum, set it now. */
	if (csum == 0x0000)
		csum = th->th_sum;

	seq = ntohl(th->th_seq);

	if (!use_hash) {
		bucket = &lc->lro_hash[0];
	} else if (M_HASHTYPE_ISHASH(m)) {
		bucket = &lc->lro_hash[m->m_pkthdr.flowid % lc->lro_hashsz];
	} else {
		uint32_t hash;

		switch (eh_type) {
#ifdef INET
		case ETHERTYPE_IP:
			hash = ip4->ip_src.s_addr + ip4->ip_dst.s_addr;
			break;
#endif
#ifdef INET6
		case ETHERTYPE_IPV6:
			hash = ip6->ip6_src.s6_addr32[0] +
			    ip6->ip6_dst.s6_addr32[0];
			hash += ip6->ip6_src.s6_addr32[1] +
			    ip6->ip6_dst.s6_addr32[1];
			hash += ip6->ip6_src.s6_addr32[2] +
			    ip6->ip6_dst.s6_addr32[2];
			hash += ip6->ip6_src.s6_addr32[3] +
			    ip6->ip6_dst.s6_addr32[3];
			break;
#endif
		default:
			hash = 0;
			break;
		}
		hash += th->th_sport + th->th_dport;
		bucket = &lc->lro_hash[hash % lc->lro_hashsz];
	}

	/* Try to find a matching previous segment. */
	LIST_FOREACH(le, bucket, hash_next) {
		if (le->eh_type != eh_type)
			continue;
		if (le->source_port != th->th_sport ||
		    le->dest_port != th->th_dport)
			continue;
		switch (eh_type) {
#ifdef INET6
		case ETHERTYPE_IPV6:
			if (bcmp(&le->source_ip6, &ip6->ip6_src,
			    sizeof(struct in6_addr)) != 0 ||
			    bcmp(&le->dest_ip6, &ip6->ip6_dst,
			    sizeof(struct in6_addr)) != 0)
				continue;
			break;
#endif
#ifdef INET
		case ETHERTYPE_IP:
			if (le->source_ip4 != ip4->ip_src.s_addr ||
			    le->dest_ip4 != ip4->ip_dst.s_addr)
				continue;
			break;
#endif
		}

		if (force_flush) {
			/* Timestamps mismatch; this is a FIN, etc */
			tcp_lro_active_remove(le);
			tcp_lro_flush(lc, le);
			return (TCP_LRO_CANNOT);
		}

		/* Flush now if appending will result in overflow. */
		if (le->p_len > (lc->lro_length_lim - tcp_data_len)) {
			tcp_lro_active_remove(le);
			tcp_lro_flush(lc, le);
			break;
		}

		/* Try to append the new segment. */
		if (__predict_false(seq != le->next_seq ||
		    (tcp_data_len == 0 &&
		    le->ack_seq == th->th_ack &&
		    le->window == th->th_win))) {
			/* Out of order packet or duplicate ACK. */
			tcp_lro_active_remove(le);
			tcp_lro_flush(lc, le);
			return (TCP_LRO_CANNOT);
		}

		if (l != 0) {
			uint32_t tsval = ntohl(*(ts_ptr + 1));
			/* Make sure timestamp values are increasing. */
			/* XXX-BZ flip and use TSTMP_GEQ macro for this? */
			if (__predict_false(le->tsval > tsval ||
			    *(ts_ptr + 2) == 0))
				return (TCP_LRO_CANNOT);
			le->tsval = tsval;
			le->tsecr = *(ts_ptr + 2);
		}
		if (tcp_data_len || SEQ_GT(ntohl(th->th_ack), ntohl(le->ack_seq))) {
			le->next_seq += tcp_data_len;
			le->ack_seq = th->th_ack;
			le->window = th->th_win;
			le->append_cnt++;
		} else if (th->th_ack == le->ack_seq) {
			le->window = WIN_MAX(le->window, th->th_win);
			le->append_cnt++;
		} else {
			/* no data and old ack */
			le->append_cnt++;
			m_freem(m);
			return (0);
		}
#ifdef TCP_LRO_UPDATE_CSUM
		le->ulp_csum += tcp_lro_rx_csum_fixup(le, l3hdr, th,
		    tcp_data_len, ~csum);
#endif

		if (tcp_data_len == 0) {
			m_freem(m);
			/*
			 * Flush this LRO entry, if this ACK should not
			 * be further delayed.
			 */
			if (le->append_cnt >= lc->lro_ackcnt_lim) {
				tcp_lro_active_remove(le);
				tcp_lro_flush(lc, le);
			}
			return (0);
		}

		le->p_len += tcp_data_len;

		/*
		 * Adjust the mbuf so that m_data points to the first byte of
		 * the ULP payload.  Adjust the mbuf to avoid complications and
		 * append new segment to existing mbuf chain.
		 */
		m_adj(m, m->m_pkthdr.len - tcp_data_len);
		m_demote_pkthdr(m);

		le->m_tail->m_next = m;
		le->m_tail = m_last(m);

		/*
		 * If a possible next full length packet would cause an
		 * overflow, pro-actively flush now.
		 */
		if (le->p_len > (lc->lro_length_lim - lc->ifp->if_mtu)) {
			tcp_lro_active_remove(le);
			tcp_lro_flush(lc, le);
		} else
			getmicrotime(&le->mtime);

		return (0);
	}

	if (force_flush) {
		/*
		 * Nothing to flush, but this segment can not be further
		 * aggregated/delayed.
		 */
		return (TCP_LRO_CANNOT);
	}

	/* Try to find an empty slot. */
	if (LIST_EMPTY(&lc->lro_free))
		return (TCP_LRO_NO_ENTRIES);

	/* Start a new segment chain. */
	le = LIST_FIRST(&lc->lro_free);
	LIST_REMOVE(le, next);
	tcp_lro_active_insert(lc, bucket, le);
	getmicrotime(&le->mtime);

	/* Start filling in details. */
	switch (eh_type) {
#ifdef INET6
	case ETHERTYPE_IPV6:
		le->le_ip6 = ip6;
		le->source_ip6 = ip6->ip6_src;
		le->dest_ip6 = ip6->ip6_dst;
		le->eh_type = eh_type;
		le->p_len = m->m_pkthdr.len - ETHER_HDR_LEN - sizeof(*ip6);
		break;
#endif
#ifdef INET
	case ETHERTYPE_IP:
		le->le_ip4 = ip4;
		le->source_ip4 = ip4->ip_src.s_addr;
		le->dest_ip4 = ip4->ip_dst.s_addr;
		le->eh_type = eh_type;
		le->p_len = m->m_pkthdr.len - ETHER_HDR_LEN;
		break;
#endif
	}
	le->source_port = th->th_sport;
	le->dest_port = th->th_dport;

	le->next_seq = seq + tcp_data_len;
	le->ack_seq = th->th_ack;
	le->window = th->th_win;
	if (l != 0) {
		le->timestamp = 1;
		le->tsval = ntohl(*(ts_ptr + 1));
		le->tsecr = *(ts_ptr + 2);
	}

#ifdef TCP_LRO_UPDATE_CSUM
	/*
	 * Do not touch the csum of the first packet.  However save the
	 * "adjusted" checksum of just the source and destination addresses,
	 * the next header and the TCP payload.  The length and TCP header
	 * parts may change, so we remove those from the saved checksum and
	 * re-add with final values on tcp_lro_flush() if needed.
	 */
	KASSERT(le->ulp_csum == 0, ("%s: le=%p le->ulp_csum=0x%04x\n",
	    __func__, le, le->ulp_csum));

	le->ulp_csum = tcp_lro_rx_csum_fixup(le, l3hdr, th, tcp_data_len,
	    ~csum);
	th->th_sum = csum;	/* Restore checksum on first packet. */
#endif

	le->m_head = m;
	le->m_tail = m_last(m);

	return (0);
}

int
tcp_lro_rx(struct lro_ctrl *lc, struct mbuf *m, uint32_t csum)
{

	return tcp_lro_rx2(lc, m, csum, 1);
}

void
tcp_lro_queue_mbuf(struct lro_ctrl *lc, struct mbuf *mb)
{
	/* sanity checks */
	if (__predict_false(lc->ifp == NULL || lc->lro_mbuf_data == NULL ||
	    lc->lro_mbuf_max == 0)) {
		/* packet drop */
		m_freem(mb);
		return;
	}

	/* check if packet is not LRO capable */
	if (__predict_false(mb->m_pkthdr.csum_flags == 0 ||
	    (lc->ifp->if_capenable & IFCAP_LRO) == 0)) {

		/* input packet to network layer */
		(*lc->ifp->if_input) (lc->ifp, mb);
		return;
	}

	/* create sequence number */
	lc->lro_mbuf_data[lc->lro_mbuf_count].seq =
	    (((uint64_t)M_HASHTYPE_GET(mb)) << 56) |
	    (((uint64_t)mb->m_pkthdr.flowid) << 24) |
	    ((uint64_t)lc->lro_mbuf_count);

	/* enter mbuf */
	lc->lro_mbuf_data[lc->lro_mbuf_count].mb = mb;

	/* flush if array is full */
	if (__predict_false(++lc->lro_mbuf_count == lc->lro_mbuf_max))
		tcp_lro_flush_all(lc);
}

/* end */
