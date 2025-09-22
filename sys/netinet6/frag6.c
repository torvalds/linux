/*	$OpenBSD: frag6.c,v 1.95 2025/07/24 22:57:24 mvs Exp $	*/
/*	$KAME: frag6.c,v 1.40 2002/05/27 21:40:31 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/pool.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>		/* for ECN definitions */


/*
 * Locks used to protect global variables in this file:
 *	Q	frag6_mutex
 */

struct mutex frag6_mutex = MUTEX_INITIALIZER(IPL_SOFTNET);

u_int frag6_nfragpackets;			/* [Q] */
u_int frag6_nfrags;				/* [Q] */
TAILQ_HEAD(ip6q_head, ip6q) frag6_queue;	/* [Q] ip6 reassemble queue */

void frag6_freef(struct ip6q *);
void frag6_unlink(struct ip6q *, struct ip6q_head *);

struct pool ip6af_pool;
struct pool ip6q_pool;

/*
 * Initialise reassembly queue and pools.
 */
void
frag6_init(void)
{
	pool_init(&ip6af_pool, sizeof(struct ip6asfrag),
	    0, IPL_SOFTNET, 0, "ip6af", NULL);
	pool_init(&ip6q_pool, sizeof(struct ip6q),
	    0, IPL_SOFTNET, 0, "ip6q", NULL);

	TAILQ_INIT(&frag6_queue);
}

/*
 * In RFC2460, fragment and reassembly rule do not agree with each other,
 * in terms of next header field handling in fragment header.
 * While the sender will use the same value for all of the fragmented packets,
 * receiver is suggested not to check the consistency.
 *
 * fragment rule (p20):
 *	(2) A Fragment header containing:
 *	The Next Header value that identifies the first header of
 *	the Fragmentable Part of the original packet.
 *		-> next header field is same for all fragments
 *
 * reassembly rule (p21):
 *	The Next Header field of the last header of the Unfragmentable
 *	Part is obtained from the Next Header field of the first
 *	fragment's Fragment header.
 *		-> should grab it from the first fragment only
 *
 * The following note also contradicts with fragment rule - noone is going to
 * send different fragment with different next header field.
 *
 * additional note (p22):
 *	The Next Header values in the Fragment headers of different
 *	fragments of the same original packet may differ.  Only the value
 *	from the Offset zero fragment packet is used for reassembly.
 *		-> should grab it from the first fragment only
 *
 * There is no explicit reason given in the RFC.  Historical reason maybe?
 */
/*
 * Fragment input
 */
int
frag6_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct mbuf *t;
	struct ip6_hdr *ip6;
	struct ip6_frag *ip6f;
	struct ip6q *q6;
	struct ip6asfrag *af6, *ip6af, *naf6, *paf6;
	int offset = *offp, nxt, i, next;
	int first_frag = 0;
	int fragoff, frgpartlen;	/* must be larger than u_int16_t */
	u_int8_t ecn, ecn0;

	ip6 = mtod(*mp, struct ip6_hdr *);
	ip6f = ip6_exthdr_get(mp, offset, sizeof(*ip6f));
	if (ip6f == NULL)
		return IPPROTO_DONE;

	/* jumbo payload can't contain a fragment header */
	if (ip6->ip6_plen == 0) {
		icmp6_error(*mp, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    offset);
		return IPPROTO_DONE;
	}

	/*
	 * check whether fragment packet's fragment length is
	 * multiple of 8 octets.
	 * sizeof(struct ip6_frag) == 8
	 * sizeof(struct ip6_hdr) = 40
	 */
	if ((ip6f->ip6f_offlg & IP6F_MORE_FRAG) &&
	    (((ntohs(ip6->ip6_plen) - offset) & 0x7) != 0)) {
		icmp6_error(*mp, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    offsetof(struct ip6_hdr, ip6_plen));
		return IPPROTO_DONE;
	}

	ip6stat_inc(ip6s_fragments);

	/* offset now points to data portion */
	offset += sizeof(struct ip6_frag);

	/*
	 * RFC6946:  A host that receives an IPv6 packet which includes
	 * a Fragment Header with the "Fragment Offset" equal to 0 and
	 * the "M" bit equal to 0 MUST process such packet in isolation
	 * from any other packets/fragments.
	 */
	fragoff = ntohs(ip6f->ip6f_offlg & IP6F_OFF_MASK);
	if (fragoff == 0 && !(ip6f->ip6f_offlg & IP6F_MORE_FRAG)) {
		ip6stat_inc(ip6s_reassembled);
		*offp = offset;
		return ip6f->ip6f_nxt;
	}

	/* Ignore empty non atomic fragment, do not classify as overlapping. */
	if (sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen) <= offset) {
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	mtx_enter(&frag6_mutex);

	/*
	 * Enforce upper bound on number of fragments.
	 * If maxfrag is 0, never accept fragments.
	 */
	if (frag6_nfrags >= atomic_load_int(&ip6_maxfrags)) {
		mtx_leave(&frag6_mutex);
		goto dropfrag;
	}

	TAILQ_FOREACH(q6, &frag6_queue, ip6q_queue)
		if (ip6f->ip6f_ident == q6->ip6q_ident &&
		    IN6_ARE_ADDR_EQUAL(&ip6->ip6_src, &q6->ip6q_src) &&
		    IN6_ARE_ADDR_EQUAL(&ip6->ip6_dst, &q6->ip6q_dst))
			break;

	if (q6 == NULL) {
		/*
		 * the first fragment to arrive, create a reassembly queue.
		 */
		first_frag = 1;

		/*
		 * Enforce upper bound on number of fragmented packets
		 * for which we attempt reassembly;
		 * If maxfragpackets is 0, never accept fragments.
		 */
		if (frag6_nfragpackets >=
		    atomic_load_int(&ip6_maxfragpackets)) {
			mtx_leave(&frag6_mutex);
			goto dropfrag;
		}
		frag6_nfragpackets++;
		q6 = pool_get(&ip6q_pool, PR_NOWAIT | PR_ZERO);
		if (q6 == NULL) {
			mtx_leave(&frag6_mutex);
			goto dropfrag;
		}

		TAILQ_INSERT_HEAD(&frag6_queue, q6, ip6q_queue);

		/* ip6q_nxt will be filled afterwards, from 1st fragment */
		LIST_INIT(&q6->ip6q_asfrag);
		q6->ip6q_ident	= ip6f->ip6f_ident;
		q6->ip6q_ttl	= IPV6_FRAGTTL;
		q6->ip6q_src	= ip6->ip6_src;
		q6->ip6q_dst	= ip6->ip6_dst;
		q6->ip6q_ecn	= (ntohl(ip6->ip6_flow) >> 20) & IPTOS_ECN_MASK;
		q6->ip6q_unfrglen = -1;	/* The 1st fragment has not arrived. */
		q6->ip6q_nfrag = 0;
	}

	/*
	 * If it's the 1st fragment, record the length of the
	 * unfragmentable part and the next header of the fragment header.
	 */
	if (fragoff == 0) {
		q6->ip6q_unfrglen = offset - sizeof(struct ip6_hdr) -
		    sizeof(struct ip6_frag);
		q6->ip6q_nxt = ip6f->ip6f_nxt;
	}

	/*
	 * Check that the reassembled packet would not exceed 65535 bytes
	 * in size.
	 * If it would exceed, discard the fragment and return an ICMP error.
	 */
	frgpartlen = sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen) - offset;
	if (q6->ip6q_unfrglen >= 0) {
		/* The 1st fragment has already arrived. */
		if (q6->ip6q_unfrglen + fragoff + frgpartlen > IPV6_MAXPACKET) {
			mtx_leave(&frag6_mutex);
			icmp6_error(*mp, ICMP6_PARAM_PROB,
			    ICMP6_PARAMPROB_HEADER,
			    offset - sizeof(struct ip6_frag) +
			    offsetof(struct ip6_frag, ip6f_offlg));
			return (IPPROTO_DONE);
		}
	} else if (fragoff + frgpartlen > IPV6_MAXPACKET) {
		mtx_leave(&frag6_mutex);
		icmp6_error(*mp, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
			    offset - sizeof(struct ip6_frag) +
				offsetof(struct ip6_frag, ip6f_offlg));
		return (IPPROTO_DONE);
	}
	/*
	 * If it's the first fragment, do the above check for each
	 * fragment already stored in the reassembly queue.
	 */
	if (fragoff == 0) {
		LIST_FOREACH_SAFE(af6, &q6->ip6q_asfrag, ip6af_list, naf6) {
			if (q6->ip6q_unfrglen + af6->ip6af_off +
			    af6->ip6af_frglen > IPV6_MAXPACKET) {
				struct mbuf *merr = af6->ip6af_m;
				struct ip6_hdr *ip6err;
				int erroff = af6->ip6af_offset;

				/* dequeue the fragment. */
				LIST_REMOVE(af6, ip6af_list);
				pool_put(&ip6af_pool, af6);

				/* adjust pointer. */
				ip6err = mtod(merr, struct ip6_hdr *);

				/*
				 * Restore source and destination addresses
				 * in the erroneous IPv6 header.
				 */
				ip6err->ip6_src = q6->ip6q_src;
				ip6err->ip6_dst = q6->ip6q_dst;

				icmp6_error(merr, ICMP6_PARAM_PROB,
				    ICMP6_PARAMPROB_HEADER,
				    erroff - sizeof(struct ip6_frag) +
				    offsetof(struct ip6_frag, ip6f_offlg));
			}
		}
	}

	ip6af = pool_get(&ip6af_pool, PR_NOWAIT | PR_ZERO);
	if (ip6af == NULL) {
		mtx_leave(&frag6_mutex);
		goto dropfrag;
	}
	ip6af->ip6af_mff = ip6f->ip6f_offlg & IP6F_MORE_FRAG;
	ip6af->ip6af_off = fragoff;
	ip6af->ip6af_frglen = frgpartlen;
	ip6af->ip6af_offset = offset;
	ip6af->ip6af_m = *mp;

	if (first_frag) {
		paf6 = NULL;
		goto insert;
	}

	/*
	 * Handle ECN by comparing this segment with the first one;
	 * if CE is set, do not lose CE.
	 * drop if CE and not-ECT are mixed for the same packet.
	 */
	ecn = (ntohl(ip6->ip6_flow) >> 20) & IPTOS_ECN_MASK;
	ecn0 = q6->ip6q_ecn;
	if (ecn == IPTOS_ECN_CE) {
		if (ecn0 == IPTOS_ECN_NOTECT) {
			mtx_leave(&frag6_mutex);
			pool_put(&ip6af_pool, ip6af);
			goto dropfrag;
		}
		if (ecn0 != IPTOS_ECN_CE)
			q6->ip6q_ecn = IPTOS_ECN_CE;
	}
	if (ecn == IPTOS_ECN_NOTECT && ecn0 != IPTOS_ECN_NOTECT) {
		mtx_leave(&frag6_mutex);
		pool_put(&ip6af_pool, ip6af);
		goto dropfrag;
	}

	/*
	 * Find a segment which begins after this one does.
	 */
	for (paf6 = NULL, af6 = LIST_FIRST(&q6->ip6q_asfrag);
	    af6 != NULL;
	    paf6 = af6, af6 = LIST_NEXT(af6, ip6af_list))
		if (af6->ip6af_off > ip6af->ip6af_off)
			break;

	/*
	 * RFC 5722, Errata 3089:  When reassembling an IPv6 datagram, if one
	 * or more its constituent fragments is determined to be an overlapping
	 * fragment, the entire datagram (and any constituent fragments) MUST
	 * be silently discarded.
	 */
	if (paf6 != NULL) {
		i = (paf6->ip6af_off + paf6->ip6af_frglen) - ip6af->ip6af_off;
		if (i > 0)
			goto flushfrags;
	}
	if (af6 != NULL) {
		i = (ip6af->ip6af_off + ip6af->ip6af_frglen) - af6->ip6af_off;
		if (i > 0)
			goto flushfrags;
	}

 insert:
	/*
	 * Stick new segment in its place;
	 * check for complete reassembly.
	 * Move to front of packet queue, as we are
	 * the most recently active fragmented packet.
	 */
	if (paf6 != NULL)
		LIST_INSERT_AFTER(paf6, ip6af, ip6af_list);
	else
		LIST_INSERT_HEAD(&q6->ip6q_asfrag, ip6af, ip6af_list);
	frag6_nfrags++;
	q6->ip6q_nfrag++;
	next = 0;
	for (paf6 = NULL, af6 = LIST_FIRST(&q6->ip6q_asfrag);
	    af6 != NULL;
	    paf6 = af6, af6 = LIST_NEXT(af6, ip6af_list)) {
		if (af6->ip6af_off != next) {
			mtx_leave(&frag6_mutex);
			return IPPROTO_DONE;
		}
		next += af6->ip6af_frglen;
	}
	if (paf6->ip6af_mff) {
		mtx_leave(&frag6_mutex);
		return IPPROTO_DONE;
	}

	/*
	 * Reassembly is complete; concatenate fragments.
	 */
	ip6af = LIST_FIRST(&q6->ip6q_asfrag);
	LIST_REMOVE(ip6af, ip6af_list);
	t = *mp = ip6af->ip6af_m;
	while ((af6 = LIST_FIRST(&q6->ip6q_asfrag)) != NULL) {
		LIST_REMOVE(af6, ip6af_list);
		while (t->m_next)
			t = t->m_next;
		t->m_next = af6->ip6af_m;
		m_adj(t->m_next, af6->ip6af_offset);
		m_removehdr(t->m_next);
		pool_put(&ip6af_pool, af6);
	}

	/* adjust offset to point where the original next header starts */
	offset = ip6af->ip6af_offset - sizeof(struct ip6_frag);
	pool_put(&ip6af_pool, ip6af);
	next += offset - sizeof(struct ip6_hdr);
	if ((u_int)next > IPV6_MAXPACKET) {
		TAILQ_REMOVE(&frag6_queue, q6, ip6q_queue);
		frag6_nfrags -= q6->ip6q_nfrag;
		frag6_nfragpackets--;
		mtx_leave(&frag6_mutex);
		pool_put(&ip6q_pool, q6);
		goto dropfrag;
	}
	ip6 = mtod(*mp, struct ip6_hdr *);
	ip6->ip6_plen = htons(next);
	ip6->ip6_src = q6->ip6q_src;
	ip6->ip6_dst = q6->ip6q_dst;
	if (q6->ip6q_ecn == IPTOS_ECN_CE)
		ip6->ip6_flow |= htonl(IPTOS_ECN_CE << 20);
	nxt = q6->ip6q_nxt;

	/* Delete frag6 header */
	if (frag6_deletefraghdr(*mp, offset) != 0) {
		TAILQ_REMOVE(&frag6_queue, q6, ip6q_queue);
		frag6_nfrags -= q6->ip6q_nfrag;
		frag6_nfragpackets--;
		mtx_leave(&frag6_mutex);
		pool_put(&ip6q_pool, q6);
		goto dropfrag;
	}

	TAILQ_REMOVE(&frag6_queue, q6, ip6q_queue);
	frag6_nfrags -= q6->ip6q_nfrag;
	frag6_nfragpackets--;

	mtx_leave(&frag6_mutex);

	pool_put(&ip6q_pool, q6);

	m_calchdrlen(*mp);

	/*
	 * Restore NXT to the original.
	 */
	{
		int prvnxt = ip6_get_prevhdr(*mp, offset);
		uint8_t *prvnxtp;

		prvnxtp = ip6_exthdr_get(mp, prvnxt, sizeof(*prvnxtp));
		if (prvnxtp == NULL)
			goto dropfrag;
		*prvnxtp = nxt;
	}

	ip6stat_inc(ip6s_reassembled);

	/*
	 * Tell launch routine the next header
	 */
	*offp = offset;
	return nxt;

 flushfrags:
	TAILQ_REMOVE(&frag6_queue, q6, ip6q_queue);
	frag6_nfrags -= q6->ip6q_nfrag;
	frag6_nfragpackets--;

	mtx_leave(&frag6_mutex);

	pool_put(&ip6af_pool, ip6af);

	while ((af6 = LIST_FIRST(&q6->ip6q_asfrag)) != NULL) {
		LIST_REMOVE(af6, ip6af_list);
		m_freem(af6->ip6af_m);
		pool_put(&ip6af_pool, af6);
	}
	ip6stat_add(ip6s_fragdropped, q6->ip6q_nfrag + 1);
	pool_put(&ip6q_pool, q6);
	m_freemp(mp);
	return IPPROTO_DONE;

 dropfrag:
	ip6stat_inc(ip6s_fragdropped);
	m_freemp(mp);
	return IPPROTO_DONE;
}

/*
 * Delete fragment header after the unfragmentable header portions.
 */
int
frag6_deletefraghdr(struct mbuf *m, int offset)
{
	struct mbuf *t;

	if (m->m_len >= offset + sizeof(struct ip6_frag)) {
		memmove(mtod(m, caddr_t) + sizeof(struct ip6_frag),
		    mtod(m, caddr_t), offset);
		m->m_data += sizeof(struct ip6_frag);
		m->m_len -= sizeof(struct ip6_frag);
	} else {
		/* this comes with no copy if the boundary is on cluster */
		if ((t = m_split(m, offset, M_DONTWAIT)) == NULL)
			return (ENOBUFS);
		m_adj(t, sizeof(struct ip6_frag));
		m_cat(m, t);
	}

	return (0);
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 * The header must not be in any queue.
 */
void
frag6_freef(struct ip6q *q6)
{
	struct ip6asfrag *af6;

	while ((af6 = LIST_FIRST(&q6->ip6q_asfrag)) != NULL) {
		struct mbuf *m = af6->ip6af_m;

		LIST_REMOVE(af6, ip6af_list);

		/*
		 * Return ICMP time exceeded error for the 1st fragment.
		 * Just free other fragments.
		 */
		if (af6->ip6af_off == 0) {
			struct ip6_hdr *ip6;

			/* adjust pointer */
			ip6 = mtod(m, struct ip6_hdr *);

			/* restore source and destination addresses */
			ip6->ip6_src = q6->ip6q_src;
			ip6->ip6_dst = q6->ip6q_dst;

			NET_LOCK_SHARED();
			icmp6_error(m, ICMP6_TIME_EXCEEDED,
				    ICMP6_TIME_EXCEED_REASSEMBLY, 0);
			NET_UNLOCK_SHARED();
		} else
			m_freem(m);
		pool_put(&ip6af_pool, af6);
	}
	pool_put(&ip6q_pool, q6);
}

/*
 * Unlinks a fragment reassembly header from the reassembly queue
 * and inserts it into a given remove queue.
 */
void
frag6_unlink(struct ip6q *q6, struct ip6q_head *rmq6)
{
	MUTEX_ASSERT_LOCKED(&frag6_mutex);

	TAILQ_REMOVE(&frag6_queue, q6, ip6q_queue);
	TAILQ_INSERT_HEAD(rmq6, q6, ip6q_queue);
	frag6_nfrags -= q6->ip6q_nfrag;
	frag6_nfragpackets--;
}

/*
 * IPv6 reassembling timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
frag6_slowtimo(void)
{
	struct ip6q_head rmq6;
	struct ip6q *q6, *nq6;
	u_int ip6_maxfragpackets_local = atomic_load_int(&ip6_maxfragpackets);

	TAILQ_INIT(&rmq6);

	mtx_enter(&frag6_mutex);

	TAILQ_FOREACH_SAFE(q6, &frag6_queue, ip6q_queue, nq6) {
		if (--q6->ip6q_ttl == 0) {
			ip6stat_inc(ip6s_fragtimeout);
			frag6_unlink(q6, &rmq6);
		}
	}

	/*
	 * If we are over the maximum number of fragments
	 * (due to the limit being lowered), drain off
	 * enough to get down to the new limit.
	 */
	while (frag6_nfragpackets > ip6_maxfragpackets_local &&
	    !TAILQ_EMPTY(&frag6_queue)) {
		ip6stat_inc(ip6s_fragoverflow);
		frag6_unlink(TAILQ_LAST(&frag6_queue, ip6q_head), &rmq6);
	}

	mtx_leave(&frag6_mutex);

	while ((q6 = TAILQ_FIRST(&rmq6)) != NULL) {
		TAILQ_REMOVE(&rmq6, q6, ip6q_queue);
		frag6_freef(q6);
	}
}
