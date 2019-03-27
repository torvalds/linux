/*-
 * Copyright (c) 2015
 *	Jonathan Looney. All rights reserved.
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

#include <sys/queue.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/eventhandler.h>
#include <machine/atomic.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_pcap.h>

#define M_LEADINGSPACE_NOWRITE(m)					\
	((m)->m_data - M_START(m))

int tcp_pcap_aggressive_free = 1;
static int tcp_pcap_clusters_referenced_cur = 0;
static int tcp_pcap_clusters_referenced_max = 0;

SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_aggressive_free,
	CTLFLAG_RW, &tcp_pcap_aggressive_free, 0,
	"Free saved packets when the memory system comes under pressure");
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_clusters_referenced_cur,
	CTLFLAG_RD, &tcp_pcap_clusters_referenced_cur, 0,
	"Number of clusters currently referenced on TCP PCAP queues");
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_clusters_referenced_max,
	CTLFLAG_RW, &tcp_pcap_clusters_referenced_max, 0,
	"Maximum number of clusters allowed to be referenced on TCP PCAP "
	"queues");

static int tcp_pcap_alloc_reuse_ext = 0;
static int tcp_pcap_alloc_reuse_mbuf = 0;
static int tcp_pcap_alloc_new_mbuf = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_alloc_reuse_ext,
	CTLFLAG_RD, &tcp_pcap_alloc_reuse_ext, 0,
	"Number of mbufs with external storage reused for the TCP PCAP "
	"functionality");
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_alloc_reuse_mbuf,
	CTLFLAG_RD, &tcp_pcap_alloc_reuse_mbuf, 0,
	"Number of mbufs with internal storage reused for the TCP PCAP "
	"functionality");
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_alloc_new_mbuf,
	CTLFLAG_RD, &tcp_pcap_alloc_new_mbuf, 0,
	"Number of new mbufs allocated for the TCP PCAP functionality");

VNET_DEFINE(int, tcp_pcap_packets) = 0;
#define V_tcp_pcap_packets	VNET(tcp_pcap_packets)
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcp_pcap_packets,
	CTLFLAG_RW, &VNET_NAME(tcp_pcap_packets), 0,
	"Default number of packets saved per direction per TCPCB");

/* Initialize the values. */
static void
tcp_pcap_max_set(void)
{

	tcp_pcap_clusters_referenced_max = nmbclusters / 4;
}

void
tcp_pcap_init(void)
{

	tcp_pcap_max_set();
	EVENTHANDLER_REGISTER(nmbclusters_change, tcp_pcap_max_set,
		NULL, EVENTHANDLER_PRI_ANY);
}

/*
 * If we are below the maximum allowed cluster references,
 * increment the reference count and return TRUE. Otherwise,
 * leave the reference count alone and return FALSE.
 */
static __inline bool
tcp_pcap_take_cluster_reference(void)
{
	if (atomic_fetchadd_int(&tcp_pcap_clusters_referenced_cur, 1) >=
		tcp_pcap_clusters_referenced_max) {
		atomic_add_int(&tcp_pcap_clusters_referenced_cur, -1);
		return FALSE;
	}
	return TRUE;
}

/*
 * For all the external entries in m, apply the given adjustment.
 * This can be used to adjust the counter when an mbuf chain is
 * copied or freed.
 */
static __inline void
tcp_pcap_adj_cluster_reference(struct mbuf *m, int adj)
{
	while (m) {
		if (m->m_flags & M_EXT)
			atomic_add_int(&tcp_pcap_clusters_referenced_cur, adj);

		m = m->m_next;
	}
}

/*
 * Free all mbufs in a chain, decrementing the reference count as
 * necessary.
 *
 * Functions in this file should use this instead of m_freem() when
 * they are freeing mbuf chains that may contain clusters that were
 * already included in tcp_pcap_clusters_referenced_cur.
 */
static void
tcp_pcap_m_freem(struct mbuf *mb)
{
	while (mb != NULL) {
		if (mb->m_flags & M_EXT)
			atomic_subtract_int(&tcp_pcap_clusters_referenced_cur,
			    1);
		mb = m_free(mb);
	}
}

/*
 * Copy data from m to n, where n cannot fit all the data we might
 * want from m.
 *
 * Prioritize data like this:
 * 1. TCP header
 * 2. IP header
 * 3. Data
 */
static void
tcp_pcap_copy_bestfit(struct tcphdr *th, struct mbuf *m, struct mbuf *n)
{
	struct mbuf *m_cur = m;
	int bytes_to_copy=0, trailing_data, skip=0, tcp_off;

	/* Below, we assume these will be non-NULL. */
	KASSERT(th, ("%s: called with th == NULL", __func__));
	KASSERT(m, ("%s: called with m == NULL", __func__));
	KASSERT(n, ("%s: called with n == NULL", __func__));

	/* We assume this initialization occurred elsewhere. */
	KASSERT(n->m_len == 0, ("%s: called with n->m_len=%d (expected 0)",
		__func__, n->m_len));
	KASSERT(n->m_data == M_START(n),
		("%s: called with n->m_data != M_START(n)", __func__));

	/*
	 * Calculate the size of the TCP header. We use this often
	 * enough that it is worth just calculating at the start.
	 */
	tcp_off = th->th_off << 2;

	/* Trim off leading empty mbufs. */
	while (m && m->m_len == 0)
		m = m->m_next;

	if (m) {
		m_cur = m;
	}
	else {
		/*
		 * No data? Highly unusual. We would expect to at
		 * least see a TCP header in the mbuf.
		 * As we have a pointer to the TCP header, I guess
		 * we should just copy that. (???)
		 */
fallback:
		bytes_to_copy = tcp_off;
		if (bytes_to_copy > M_SIZE(n))
			bytes_to_copy = M_SIZE(n);
		bcopy(th, n->m_data, bytes_to_copy);
		n->m_len = bytes_to_copy;
		return;
	}

	/*
	 * Find TCP header. Record the total number of bytes up to,
	 * and including, the TCP header.
	 */
	while (m_cur) {
		if ((caddr_t) th >= (caddr_t) m_cur->m_data &&
			(caddr_t) th < (caddr_t) (m_cur->m_data + m_cur->m_len))
			break;
		bytes_to_copy += m_cur->m_len;
		m_cur = m_cur->m_next;
	}
	if (m_cur)
		bytes_to_copy += (caddr_t) th - (caddr_t) m_cur->m_data;
	else
		goto fallback;
	bytes_to_copy += tcp_off;

	/*
	 * If we already want to copy more bytes than we can hold
	 * in the destination mbuf, skip leading bytes and copy
	 * what we can.
	 *
	 * Otherwise, consider trailing data.
	 */
	if (bytes_to_copy > M_SIZE(n)) {
		skip  = bytes_to_copy - M_SIZE(n);
		bytes_to_copy = M_SIZE(n);
	}
	else {
		/*
		 * Determine how much trailing data is in the chain.
		 * We start with the length of this mbuf (the one
		 * containing th) and subtract the size of the TCP
		 * header (tcp_off) and the size of the data prior
		 * to th (th - m_cur->m_data).
		 *
		 * This *should not* be negative, as the TCP code
		 * should put the whole TCP header in a single
		 * mbuf. But, it isn't a problem if it is. We will
		 * simple work off our negative balance as we look
		 * at subsequent mbufs.
		 */
		trailing_data = m_cur->m_len - tcp_off;
		trailing_data -= (caddr_t) th - (caddr_t) m_cur->m_data;
		m_cur = m_cur->m_next;
		while (m_cur) {
			trailing_data += m_cur->m_len;
			m_cur = m_cur->m_next;
		}
		if ((bytes_to_copy + trailing_data) > M_SIZE(n))
			bytes_to_copy = M_SIZE(n);
		else
			bytes_to_copy += trailing_data;
	}

	m_copydata(m, skip, bytes_to_copy, n->m_data);
	n->m_len = bytes_to_copy;
}

void
tcp_pcap_add(struct tcphdr *th, struct mbuf *m, struct mbufq *queue)
{
	struct mbuf *n = NULL, *mhead;

	KASSERT(th, ("%s: called with th == NULL", __func__));
	KASSERT(m, ("%s: called with m == NULL", __func__));
	KASSERT(queue, ("%s: called with queue == NULL", __func__));

	/* We only care about data packets. */
	while (m && m->m_type != MT_DATA)
		m = m->m_next;

	/* We only need to do something if we still have an mbuf. */
	if (!m)
		return;

	/* If we are not saving mbufs, return now. */
	if (queue->mq_maxlen == 0)
		return;

	/*
	 * Check to see if we will need to recycle mbufs.
	 *
	 * If we need to get rid of mbufs to stay below
	 * our packet count, try to reuse the mbuf. Once
	 * we already have a new mbuf (n), then we can
	 * simply free subsequent mbufs.
	 *
	 * Note that most of the logic in here is to deal
	 * with the reuse. If we are fine with constant
	 * mbuf allocs/deallocs, we could ditch this logic.
	 * But, it only seems to make sense to reuse
	 * mbufs we already have.
	 */
	while (mbufq_full(queue)) {
		mhead = mbufq_dequeue(queue);

		if (n) {
			tcp_pcap_m_freem(mhead);
		}
		else {
			/*
			 * If this held an external cluster, try to
			 * detach the cluster. But, if we held the
			 * last reference, go through the normal
			 * free-ing process.
			 */
			if (mhead->m_flags & M_EXT) {
				switch (mhead->m_ext.ext_type) {
				case EXT_SFBUF:
					/* Don't mess around with these. */
					tcp_pcap_m_freem(mhead);
					continue;
				default:
					if (atomic_fetchadd_int(
						mhead->m_ext.ext_cnt, -1) == 1)
					{
						/*
						 * We held the last reference
						 * on this cluster. Restore
						 * the reference count and put
						 * it back in the pool.
				 		 */
						*(mhead->m_ext.ext_cnt) = 1;
						tcp_pcap_m_freem(mhead);
						continue;
					}
					/*
					 * We were able to cleanly free the
					 * reference.
				 	 */
					atomic_subtract_int(
					    &tcp_pcap_clusters_referenced_cur,
					    1);
					tcp_pcap_alloc_reuse_ext++;
					break;
				}
			}
			else {
				tcp_pcap_alloc_reuse_mbuf++;
			}

			n = mhead;
			tcp_pcap_m_freem(n->m_next);
			m_init(n, M_NOWAIT, MT_DATA, 0);
		}
	}

	/* Check to see if we need to get a new mbuf. */
	if (!n) {
		if (!(n = m_get(M_NOWAIT, MT_DATA)))
			return;
		tcp_pcap_alloc_new_mbuf++;
	}

	/*
	 * What are we dealing with? If a cluster, attach it. Otherwise,
	 * try to copy the data from the beginning of the mbuf to the
	 * end of data. (There may be data between the start of the data
	 * area and the current data pointer. We want to get this, because
	 * it may contain header information that is useful.)
	 * In cases where that isn't possible, settle for what we can
	 * get.
	 */
	if ((m->m_flags & M_EXT) && tcp_pcap_take_cluster_reference()) {
		n->m_data = m->m_data;
		n->m_len = m->m_len;
		mb_dupcl(n, m);
	}
	else if (((m->m_data + m->m_len) - M_START(m)) <= M_SIZE(n)) {
		/*
		 * At this point, n is guaranteed to be a normal mbuf
		 * with no cluster and no packet header. Because the
		 * logic in this code block requires this, the assert
		 * is here to catch any instances where someone
		 * changes the logic to invalidate that assumption.
		 */
		KASSERT((n->m_flags & (M_EXT | M_PKTHDR)) == 0,
			("%s: Unexpected flags (%#x) for mbuf",
			__func__, n->m_flags));
		n->m_data = n->m_dat + M_LEADINGSPACE_NOWRITE(m);
		n->m_len = m->m_len;
		bcopy(M_START(m), n->m_dat,
			m->m_len + M_LEADINGSPACE_NOWRITE(m));
	}
	else {
		/*
		 * This is the case where we need to "settle for what
		 * we can get". The most probable way to this code
		 * path is that we've already taken references to the
		 * maximum number of mbuf clusters we can, and the data
		 * is too long to fit in an mbuf's internal storage.
		 * Try for a "best fit".
		 */
		tcp_pcap_copy_bestfit(th, m, n);

		/* Don't try to get additional data. */
		goto add_to_queue;
	}

	if (m->m_next) {
		n->m_next = m_copym(m->m_next, 0, M_COPYALL, M_NOWAIT);
		tcp_pcap_adj_cluster_reference(n->m_next, 1);
	}

add_to_queue:
	/* Add the new mbuf to the list. */
	if (mbufq_enqueue(queue, n)) {
		/* This shouldn't happen. If INVARIANTS is defined, panic. */
		KASSERT(0, ("%s: mbufq was unexpectedly full!", __func__));
		tcp_pcap_m_freem(n);
	}
}

void
tcp_pcap_drain(struct mbufq *queue)
{
	struct mbuf *m;
	while ((m = mbufq_dequeue(queue)))
		tcp_pcap_m_freem(m);
}

void
tcp_pcap_tcpcb_init(struct tcpcb *tp)
{
	mbufq_init(&(tp->t_inpkts), V_tcp_pcap_packets);
	mbufq_init(&(tp->t_outpkts), V_tcp_pcap_packets);
}

void
tcp_pcap_set_sock_max(struct mbufq *queue, int newval)
{
	queue->mq_maxlen = newval;
	while (queue->mq_len > queue->mq_maxlen)
		tcp_pcap_m_freem(mbufq_dequeue(queue));
}

int
tcp_pcap_get_sock_max(struct mbufq *queue)
{
	return queue->mq_maxlen;
}
