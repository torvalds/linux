/*-
 * Copyright (c) 2015 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2015 Adrian Chadd <adrian@FreeBSD.org>
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)ip_input.c	8.2 (Berkeley) 1/4/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/hash.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <net/rss_config.h>
#include <net/netisr.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_rss.h>
#ifdef MAC
#include <security/mac/mac_framework.h>
#endif

SYSCTL_DECL(_net_inet_ip);

/*
 * Reassembly headers are stored in hash buckets.
 */
#define	IPREASS_NHASH_LOG2	10
#define	IPREASS_NHASH		(1 << IPREASS_NHASH_LOG2)
#define	IPREASS_HMASK		(IPREASS_NHASH - 1)

struct ipqbucket {
	TAILQ_HEAD(ipqhead, ipq) head;
	struct mtx		 lock;
	int			 count;
};

VNET_DEFINE_STATIC(struct ipqbucket, ipq[IPREASS_NHASH]);
#define	V_ipq		VNET(ipq)
VNET_DEFINE_STATIC(uint32_t, ipq_hashseed);
#define V_ipq_hashseed   VNET(ipq_hashseed)

#define	IPQ_LOCK(i)	mtx_lock(&V_ipq[i].lock)
#define	IPQ_TRYLOCK(i)	mtx_trylock(&V_ipq[i].lock)
#define	IPQ_UNLOCK(i)	mtx_unlock(&V_ipq[i].lock)
#define	IPQ_LOCK_ASSERT(i)	mtx_assert(&V_ipq[i].lock, MA_OWNED)

VNET_DEFINE_STATIC(int, ipreass_maxbucketsize);
#define	V_ipreass_maxbucketsize	VNET(ipreass_maxbucketsize)

void		ipreass_init(void);
void		ipreass_drain(void);
void		ipreass_slowtimo(void);
#ifdef VIMAGE
void		ipreass_destroy(void);
#endif
static int	sysctl_maxfragpackets(SYSCTL_HANDLER_ARGS);
static int	sysctl_maxfragbucketsize(SYSCTL_HANDLER_ARGS);
static void	ipreass_zone_change(void *);
static void	ipreass_drain_tomax(void);
static void	ipq_free(struct ipqbucket *, struct ipq *);
static struct ipq * ipq_reuse(int);

static inline void
ipq_timeout(struct ipqbucket *bucket, struct ipq *fp)
{

	IPSTAT_ADD(ips_fragtimeout, fp->ipq_nfrags);
	ipq_free(bucket, fp);
}

static inline void
ipq_drop(struct ipqbucket *bucket, struct ipq *fp)
{

	IPSTAT_ADD(ips_fragdropped, fp->ipq_nfrags);
	ipq_free(bucket, fp);
}

/*
 * By default, limit the number of IP fragments across all reassembly
 * queues to  1/32 of the total number of mbuf clusters.
 *
 * Limit the total number of reassembly queues per VNET to the
 * IP fragment limit, but ensure the limit will not allow any bucket
 * to grow above 100 items. (The bucket limit is
 * IP_MAXFRAGPACKETS / (IPREASS_NHASH / 2), so the 50 is the correct
 * multiplier to reach a 100-item limit.)
 * The 100-item limit was chosen as brief testing seems to show that
 * this produces "reasonable" performance on some subset of systems
 * under DoS attack.
 */
#define	IP_MAXFRAGS		(nmbclusters / 32)
#define	IP_MAXFRAGPACKETS	(imin(IP_MAXFRAGS, IPREASS_NHASH * 50))

static int		maxfrags;
static volatile u_int	nfrags;
SYSCTL_INT(_net_inet_ip, OID_AUTO, maxfrags, CTLFLAG_RW,
    &maxfrags, 0,
    "Maximum number of IPv4 fragments allowed across all reassembly queues");
SYSCTL_UINT(_net_inet_ip, OID_AUTO, curfrags, CTLFLAG_RD,
    __DEVOLATILE(u_int *, &nfrags), 0,
    "Current number of IPv4 fragments across all reassembly queues");

VNET_DEFINE_STATIC(uma_zone_t, ipq_zone);
#define	V_ipq_zone	VNET(ipq_zone)
SYSCTL_PROC(_net_inet_ip, OID_AUTO, maxfragpackets, CTLFLAG_VNET |
    CTLTYPE_INT | CTLFLAG_RW, NULL, 0, sysctl_maxfragpackets, "I",
    "Maximum number of IPv4 fragment reassembly queue entries");
SYSCTL_UMA_CUR(_net_inet_ip, OID_AUTO, fragpackets, CTLFLAG_VNET,
    &VNET_NAME(ipq_zone),
    "Current number of IPv4 fragment reassembly queue entries");

VNET_DEFINE_STATIC(int, noreass);
#define	V_noreass	VNET(noreass)

VNET_DEFINE_STATIC(int, maxfragsperpacket);
#define	V_maxfragsperpacket	VNET(maxfragsperpacket)
SYSCTL_INT(_net_inet_ip, OID_AUTO, maxfragsperpacket, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(maxfragsperpacket), 0,
    "Maximum number of IPv4 fragments allowed per packet");
SYSCTL_PROC(_net_inet_ip, OID_AUTO, maxfragbucketsize,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RW, NULL, 0,
    sysctl_maxfragbucketsize, "I",
    "Maximum number of IPv4 fragment reassembly queue entries per bucket");

/*
 * Take incoming datagram fragment and try to reassemble it into
 * whole datagram.  If the argument is the first fragment or one
 * in between the function will return NULL and store the mbuf
 * in the fragment chain.  If the argument is the last fragment
 * the packet will be reassembled and the pointer to the new
 * mbuf returned for further processing.  Only m_tags attached
 * to the first packet/fragment are preserved.
 * The IP header is *NOT* adjusted out of iplen.
 */
#define	M_IP_FRAG	M_PROTO9
struct mbuf *
ip_reass(struct mbuf *m)
{
	struct ip *ip;
	struct mbuf *p, *q, *nq, *t;
	struct ipq *fp;
	struct ipqhead *head;
	int i, hlen, next, tmpmax;
	u_int8_t ecn, ecn0;
	uint32_t hash, hashkey[3];
#ifdef	RSS
	uint32_t rss_hash, rss_type;
#endif

	/*
	 * If no reassembling or maxfragsperpacket are 0,
	 * never accept fragments.
	 * Also, drop packet if it would exceed the maximum
	 * number of fragments.
	 */
	tmpmax = maxfrags;
	if (V_noreass == 1 || V_maxfragsperpacket == 0 ||
	    (tmpmax >= 0 && atomic_load_int(&nfrags) >= (u_int)tmpmax)) {
		IPSTAT_INC(ips_fragments);
		IPSTAT_INC(ips_fragdropped);
		m_freem(m);
		return (NULL);
	}

	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;

	/*
	 * Adjust ip_len to not reflect header,
	 * convert offset of this to bytes.
	 */
	ip->ip_len = htons(ntohs(ip->ip_len) - hlen);
	/*
	 * Make sure that fragments have a data length
	 * that's a non-zero multiple of 8 bytes, unless
	 * this is the last fragment.
	 */
	if (ip->ip_len == htons(0) ||
	    ((ip->ip_off & htons(IP_MF)) && (ntohs(ip->ip_len) & 0x7) != 0)) {
		IPSTAT_INC(ips_toosmall); /* XXX */
		IPSTAT_INC(ips_fragdropped);
		m_freem(m);
		return (NULL);
	}
	if (ip->ip_off & htons(IP_MF))
		m->m_flags |= M_IP_FRAG;
	else
		m->m_flags &= ~M_IP_FRAG;
	ip->ip_off = htons(ntohs(ip->ip_off) << 3);

	/*
	 * Make sure the fragment lies within a packet of valid size.
	 */
	if (ntohs(ip->ip_len) + ntohs(ip->ip_off) > IP_MAXPACKET) {
		IPSTAT_INC(ips_toolong);
		IPSTAT_INC(ips_fragdropped);
		m_freem(m);
		return (NULL);
	}

	/*
	 * Attempt reassembly; if it succeeds, proceed.
	 * ip_reass() will return a different mbuf.
	 */
	IPSTAT_INC(ips_fragments);
	m->m_pkthdr.PH_loc.ptr = ip;

	/*
	 * Presence of header sizes in mbufs
	 * would confuse code below.
	 */
	m->m_data += hlen;
	m->m_len -= hlen;

	hashkey[0] = ip->ip_src.s_addr;
	hashkey[1] = ip->ip_dst.s_addr;
	hashkey[2] = (uint32_t)ip->ip_p << 16;
	hashkey[2] += ip->ip_id;
	hash = jenkins_hash32(hashkey, nitems(hashkey), V_ipq_hashseed);
	hash &= IPREASS_HMASK;
	head = &V_ipq[hash].head;
	IPQ_LOCK(hash);

	/*
	 * Look for queue of fragments
	 * of this datagram.
	 */
	TAILQ_FOREACH(fp, head, ipq_list)
		if (ip->ip_id == fp->ipq_id &&
		    ip->ip_src.s_addr == fp->ipq_src.s_addr &&
		    ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
#ifdef MAC
		    mac_ipq_match(m, fp) &&
#endif
		    ip->ip_p == fp->ipq_p)
			break;
	/*
	 * If first fragment to arrive, create a reassembly queue.
	 */
	if (fp == NULL) {
		if (V_ipq[hash].count < V_ipreass_maxbucketsize)
			fp = uma_zalloc(V_ipq_zone, M_NOWAIT);
		if (fp == NULL)
			fp = ipq_reuse(hash);
		if (fp == NULL)
			goto dropfrag;
#ifdef MAC
		if (mac_ipq_init(fp, M_NOWAIT) != 0) {
			uma_zfree(V_ipq_zone, fp);
			fp = NULL;
			goto dropfrag;
		}
		mac_ipq_create(m, fp);
#endif
		TAILQ_INSERT_HEAD(head, fp, ipq_list);
		V_ipq[hash].count++;
		fp->ipq_nfrags = 1;
		atomic_add_int(&nfrags, 1);
		fp->ipq_ttl = IPFRAGTTL;
		fp->ipq_p = ip->ip_p;
		fp->ipq_id = ip->ip_id;
		fp->ipq_src = ip->ip_src;
		fp->ipq_dst = ip->ip_dst;
		fp->ipq_frags = m;
		if (m->m_flags & M_IP_FRAG)
			fp->ipq_maxoff = -1;
		else
			fp->ipq_maxoff = ntohs(ip->ip_off) + ntohs(ip->ip_len);
		m->m_nextpkt = NULL;
		goto done;
	} else {
		/*
		 * If we already saw the last fragment, make sure
		 * this fragment's offset looks sane. Otherwise, if
		 * this is the last fragment, record its endpoint.
		 */
		if (fp->ipq_maxoff > 0) {
			i = ntohs(ip->ip_off) + ntohs(ip->ip_len);
			if (((m->m_flags & M_IP_FRAG) && i >= fp->ipq_maxoff) ||
			    ((m->m_flags & M_IP_FRAG) == 0 &&
			    i != fp->ipq_maxoff)) {
				fp = NULL;
				goto dropfrag;
			}
		} else if ((m->m_flags & M_IP_FRAG) == 0)
			fp->ipq_maxoff = ntohs(ip->ip_off) + ntohs(ip->ip_len);
		fp->ipq_nfrags++;
		atomic_add_int(&nfrags, 1);
#ifdef MAC
		mac_ipq_update(m, fp);
#endif
	}

#define GETIP(m)	((struct ip*)((m)->m_pkthdr.PH_loc.ptr))

	/*
	 * Handle ECN by comparing this segment with the first one;
	 * if CE is set, do not lose CE.
	 * drop if CE and not-ECT are mixed for the same packet.
	 */
	ecn = ip->ip_tos & IPTOS_ECN_MASK;
	ecn0 = GETIP(fp->ipq_frags)->ip_tos & IPTOS_ECN_MASK;
	if (ecn == IPTOS_ECN_CE) {
		if (ecn0 == IPTOS_ECN_NOTECT)
			goto dropfrag;
		if (ecn0 != IPTOS_ECN_CE)
			GETIP(fp->ipq_frags)->ip_tos |= IPTOS_ECN_CE;
	}
	if (ecn == IPTOS_ECN_NOTECT && ecn0 != IPTOS_ECN_NOTECT)
		goto dropfrag;

	/*
	 * Find a segment which begins after this one does.
	 */
	for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt)
		if (ntohs(GETIP(q)->ip_off) > ntohs(ip->ip_off))
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us, otherwise
	 * stick new segment in the proper place.
	 *
	 * If some of the data is dropped from the preceding
	 * segment, then it's checksum is invalidated.
	 */
	if (p) {
		i = ntohs(GETIP(p)->ip_off) + ntohs(GETIP(p)->ip_len) -
		    ntohs(ip->ip_off);
		if (i > 0) {
			if (i >= ntohs(ip->ip_len))
				goto dropfrag;
			m_adj(m, i);
			m->m_pkthdr.csum_flags = 0;
			ip->ip_off = htons(ntohs(ip->ip_off) + i);
			ip->ip_len = htons(ntohs(ip->ip_len) - i);
		}
		m->m_nextpkt = p->m_nextpkt;
		p->m_nextpkt = m;
	} else {
		m->m_nextpkt = fp->ipq_frags;
		fp->ipq_frags = m;
	}

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	for (; q != NULL && ntohs(ip->ip_off) + ntohs(ip->ip_len) >
	    ntohs(GETIP(q)->ip_off); q = nq) {
		i = (ntohs(ip->ip_off) + ntohs(ip->ip_len)) -
		    ntohs(GETIP(q)->ip_off);
		if (i < ntohs(GETIP(q)->ip_len)) {
			GETIP(q)->ip_len = htons(ntohs(GETIP(q)->ip_len) - i);
			GETIP(q)->ip_off = htons(ntohs(GETIP(q)->ip_off) + i);
			m_adj(q, i);
			q->m_pkthdr.csum_flags = 0;
			break;
		}
		nq = q->m_nextpkt;
		m->m_nextpkt = nq;
		IPSTAT_INC(ips_fragdropped);
		fp->ipq_nfrags--;
		atomic_subtract_int(&nfrags, 1);
		m_freem(q);
	}

	/*
	 * Check for complete reassembly and perform frag per packet
	 * limiting.
	 *
	 * Frag limiting is performed here so that the nth frag has
	 * a chance to complete the packet before we drop the packet.
	 * As a result, n+1 frags are actually allowed per packet, but
	 * only n will ever be stored. (n = maxfragsperpacket.)
	 *
	 */
	next = 0;
	for (p = NULL, q = fp->ipq_frags; q; p = q, q = q->m_nextpkt) {
		if (ntohs(GETIP(q)->ip_off) != next) {
			if (fp->ipq_nfrags > V_maxfragsperpacket)
				ipq_drop(&V_ipq[hash], fp);
			goto done;
		}
		next += ntohs(GETIP(q)->ip_len);
	}
	/* Make sure the last packet didn't have the IP_MF flag */
	if (p->m_flags & M_IP_FRAG) {
		if (fp->ipq_nfrags > V_maxfragsperpacket)
			ipq_drop(&V_ipq[hash], fp);
		goto done;
	}

	/*
	 * Reassembly is complete.  Make sure the packet is a sane size.
	 */
	q = fp->ipq_frags;
	ip = GETIP(q);
	if (next + (ip->ip_hl << 2) > IP_MAXPACKET) {
		IPSTAT_INC(ips_toolong);
		ipq_drop(&V_ipq[hash], fp);
		goto done;
	}

	/*
	 * Concatenate fragments.
	 */
	m = q;
	t = m->m_next;
	m->m_next = NULL;
	m_cat(m, t);
	nq = q->m_nextpkt;
	q->m_nextpkt = NULL;
	for (q = nq; q != NULL; q = nq) {
		nq = q->m_nextpkt;
		q->m_nextpkt = NULL;
		m->m_pkthdr.csum_flags &= q->m_pkthdr.csum_flags;
		m->m_pkthdr.csum_data += q->m_pkthdr.csum_data;
		m_demote_pkthdr(q);
		m_cat(m, q);
	}
	/*
	 * In order to do checksumming faster we do 'end-around carry' here
	 * (and not in for{} loop), though it implies we are not going to
	 * reassemble more than 64k fragments.
	 */
	while (m->m_pkthdr.csum_data & 0xffff0000)
		m->m_pkthdr.csum_data = (m->m_pkthdr.csum_data & 0xffff) +
		    (m->m_pkthdr.csum_data >> 16);
	atomic_subtract_int(&nfrags, fp->ipq_nfrags);
#ifdef MAC
	mac_ipq_reassemble(fp, m);
	mac_ipq_destroy(fp);
#endif

	/*
	 * Create header for new ip packet by modifying header of first
	 * packet;  dequeue and discard fragment reassembly header.
	 * Make header visible.
	 */
	ip->ip_len = htons((ip->ip_hl << 2) + next);
	ip->ip_src = fp->ipq_src;
	ip->ip_dst = fp->ipq_dst;
	TAILQ_REMOVE(head, fp, ipq_list);
	V_ipq[hash].count--;
	uma_zfree(V_ipq_zone, fp);
	m->m_len += (ip->ip_hl << 2);
	m->m_data -= (ip->ip_hl << 2);
	/* some debugging cruft by sklower, below, will go away soon */
	if (m->m_flags & M_PKTHDR)	/* XXX this should be done elsewhere */
		m_fixhdr(m);
	IPSTAT_INC(ips_reassembled);
	IPQ_UNLOCK(hash);

#ifdef	RSS
	/*
	 * Query the RSS layer for the flowid / flowtype for the
	 * mbuf payload.
	 *
	 * For now, just assume we have to calculate a new one.
	 * Later on we should check to see if the assigned flowid matches
	 * what RSS wants for the given IP protocol and if so, just keep it.
	 *
	 * We then queue into the relevant netisr so it can be dispatched
	 * to the correct CPU.
	 *
	 * Note - this may return 1, which means the flowid in the mbuf
	 * is correct for the configured RSS hash types and can be used.
	 */
	if (rss_mbuf_software_hash_v4(m, 0, &rss_hash, &rss_type) == 0) {
		m->m_pkthdr.flowid = rss_hash;
		M_HASHTYPE_SET(m, rss_type);
	}

	/*
	 * Queue/dispatch for reprocessing.
	 *
	 * Note: this is much slower than just handling the frame in the
	 * current receive context.  It's likely worth investigating
	 * why this is.
	 */
	netisr_dispatch(NETISR_IP_DIRECT, m);
	return (NULL);
#endif

	/* Handle in-line */
	return (m);

dropfrag:
	IPSTAT_INC(ips_fragdropped);
	if (fp != NULL) {
		fp->ipq_nfrags--;
		atomic_subtract_int(&nfrags, 1);
	}
	m_freem(m);
done:
	IPQ_UNLOCK(hash);
	return (NULL);

#undef GETIP
}

/*
 * Initialize IP reassembly structures.
 */
void
ipreass_init(void)
{
	int max;

	for (int i = 0; i < IPREASS_NHASH; i++) {
		TAILQ_INIT(&V_ipq[i].head);
		mtx_init(&V_ipq[i].lock, "IP reassembly", NULL,
		    MTX_DEF | MTX_DUPOK);
		V_ipq[i].count = 0;
	}
	V_ipq_hashseed = arc4random();
	V_maxfragsperpacket = 16;
	V_ipq_zone = uma_zcreate("ipq", sizeof(struct ipq), NULL, NULL, NULL,
	    NULL, UMA_ALIGN_PTR, 0);
	max = IP_MAXFRAGPACKETS;
	max = uma_zone_set_max(V_ipq_zone, max);
	V_ipreass_maxbucketsize = imax(max / (IPREASS_NHASH / 2), 1);

	if (IS_DEFAULT_VNET(curvnet)) {
		maxfrags = IP_MAXFRAGS;
		EVENTHANDLER_REGISTER(nmbclusters_change, ipreass_zone_change,
		    NULL, EVENTHANDLER_PRI_ANY);
	}
}

/*
 * If a timer expires on a reassembly queue, discard it.
 */
void
ipreass_slowtimo(void)
{
	struct ipq *fp, *tmp;

	for (int i = 0; i < IPREASS_NHASH; i++) {
		IPQ_LOCK(i);
		TAILQ_FOREACH_SAFE(fp, &V_ipq[i].head, ipq_list, tmp)
		if (--fp->ipq_ttl == 0)
				ipq_timeout(&V_ipq[i], fp);
		IPQ_UNLOCK(i);
	}
}

/*
 * Drain off all datagram fragments.
 */
void
ipreass_drain(void)
{

	for (int i = 0; i < IPREASS_NHASH; i++) {
		IPQ_LOCK(i);
		while(!TAILQ_EMPTY(&V_ipq[i].head))
			ipq_drop(&V_ipq[i], TAILQ_FIRST(&V_ipq[i].head));
		KASSERT(V_ipq[i].count == 0,
		    ("%s: V_ipq[%d] count %d (V_ipq=%p)", __func__, i,
		    V_ipq[i].count, V_ipq));
		IPQ_UNLOCK(i);
	}
}

#ifdef VIMAGE
/*
 * Destroy IP reassembly structures.
 */
void
ipreass_destroy(void)
{

	ipreass_drain();
	uma_zdestroy(V_ipq_zone);
	for (int i = 0; i < IPREASS_NHASH; i++)
		mtx_destroy(&V_ipq[i].lock);
}
#endif

/*
 * After maxnipq has been updated, propagate the change to UMA.  The UMA zone
 * max has slightly different semantics than the sysctl, for historical
 * reasons.
 */
static void
ipreass_drain_tomax(void)
{
	struct ipq *fp;
	int target;

	/*
	 * Make sure each bucket is under the new limit. If
	 * necessary, drop enough of the oldest elements from
	 * each bucket to get under the new limit.
	 */
	for (int i = 0; i < IPREASS_NHASH; i++) {
		IPQ_LOCK(i);
		while (V_ipq[i].count > V_ipreass_maxbucketsize &&
		    (fp = TAILQ_LAST(&V_ipq[i].head, ipqhead)) != NULL)
			ipq_timeout(&V_ipq[i], fp);
		IPQ_UNLOCK(i);
	}

	/*
	 * If we are over the maximum number of fragments,
	 * drain off enough to get down to the new limit,
	 * stripping off last elements on queues.  Every
	 * run we strip the oldest element from each bucket.
	 */
	target = uma_zone_get_max(V_ipq_zone);
	while (uma_zone_get_cur(V_ipq_zone) > target) {
		for (int i = 0; i < IPREASS_NHASH; i++) {
			IPQ_LOCK(i);
			fp = TAILQ_LAST(&V_ipq[i].head, ipqhead);
			if (fp != NULL)
				ipq_timeout(&V_ipq[i], fp);
			IPQ_UNLOCK(i);
		}
	}
}

static void
ipreass_zone_change(void *tag)
{
	VNET_ITERATOR_DECL(vnet_iter);
	int max;

	maxfrags = IP_MAXFRAGS;
	max = IP_MAXFRAGPACKETS;
	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		max = uma_zone_set_max(V_ipq_zone, max);
		V_ipreass_maxbucketsize = imax(max / (IPREASS_NHASH / 2), 1);
		ipreass_drain_tomax();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * Change the limit on the UMA zone, or disable the fragment allocation
 * at all.  Since 0 and -1 is a special values here, we need our own handler,
 * instead of sysctl_handle_uma_zone_max().
 */
static int
sysctl_maxfragpackets(SYSCTL_HANDLER_ARGS)
{
	int error, max;

	if (V_noreass == 0) {
		max = uma_zone_get_max(V_ipq_zone);
		if (max == 0)
			max = -1;
	} else 
		max = 0;
	error = sysctl_handle_int(oidp, &max, 0, req);
	if (error || !req->newptr)
		return (error);
	if (max > 0) {
		/*
		 * XXXRW: Might be a good idea to sanity check the argument
		 * and place an extreme upper bound.
		 */
		max = uma_zone_set_max(V_ipq_zone, max);
		V_ipreass_maxbucketsize = imax(max / (IPREASS_NHASH / 2), 1);
		ipreass_drain_tomax();
		V_noreass = 0;
	} else if (max == 0) {
		V_noreass = 1;
		ipreass_drain();
	} else if (max == -1) {
		V_noreass = 0;
		uma_zone_set_max(V_ipq_zone, 0);
		V_ipreass_maxbucketsize = INT_MAX;
	} else
		return (EINVAL);
	return (0);
}

/*
 * Seek for old fragment queue header that can be reused.  Try to
 * reuse a header from currently locked hash bucket.
 */
static struct ipq *
ipq_reuse(int start)
{
	struct ipq *fp;
	int bucket, i;

	IPQ_LOCK_ASSERT(start);

	for (i = 0; i < IPREASS_NHASH; i++) {
		bucket = (start + i) % IPREASS_NHASH;
		if (bucket != start && IPQ_TRYLOCK(bucket) == 0)
			continue;
		fp = TAILQ_LAST(&V_ipq[bucket].head, ipqhead);
		if (fp) {
			struct mbuf *m;

			IPSTAT_ADD(ips_fragtimeout, fp->ipq_nfrags);
			atomic_subtract_int(&nfrags, fp->ipq_nfrags);
			while (fp->ipq_frags) {
				m = fp->ipq_frags;
				fp->ipq_frags = m->m_nextpkt;
				m_freem(m);
			}
			TAILQ_REMOVE(&V_ipq[bucket].head, fp, ipq_list);
			V_ipq[bucket].count--;
			if (bucket != start)
				IPQ_UNLOCK(bucket);
			break;
		}
		if (bucket != start)
			IPQ_UNLOCK(bucket);
	}
	IPQ_LOCK_ASSERT(start);
	return (fp);
}

/*
 * Free a fragment reassembly header and all associated datagrams.
 */
static void
ipq_free(struct ipqbucket *bucket, struct ipq *fp)
{
	struct mbuf *q;

	atomic_subtract_int(&nfrags, fp->ipq_nfrags);
	while (fp->ipq_frags) {
		q = fp->ipq_frags;
		fp->ipq_frags = q->m_nextpkt;
		m_freem(q);
	}
	TAILQ_REMOVE(&bucket->head, fp, ipq_list);
	bucket->count--;
	uma_zfree(V_ipq_zone, fp);
}

/*
 * Get or set the maximum number of reassembly queues per bucket.
 */
static int
sysctl_maxfragbucketsize(SYSCTL_HANDLER_ARGS)
{
	int error, max;

	max = V_ipreass_maxbucketsize;
	error = sysctl_handle_int(oidp, &max, 0, req);
	if (error || !req->newptr)
		return (error);
	if (max <= 0)
		return (EINVAL);
	V_ipreass_maxbucketsize = max;
	ipreass_drain_tomax();
	return (0);
}
