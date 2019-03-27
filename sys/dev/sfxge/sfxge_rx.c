/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_rss.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/limits.h>
#include <sys/syslog.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>

#include <machine/in_cksum.h>

#ifdef RSS
#include <net/rss_config.h>
#endif

#include "common/efx.h"


#include "sfxge.h"
#include "sfxge_rx.h"

#define	RX_REFILL_THRESHOLD(_entries)	(EFX_RXQ_LIMIT(_entries) * 9 / 10)

#ifdef SFXGE_LRO

SYSCTL_NODE(_hw_sfxge, OID_AUTO, lro, CTLFLAG_RD, NULL,
	    "Large receive offload (LRO) parameters");

#define	SFXGE_LRO_PARAM(_param)	SFXGE_PARAM(lro._param)

/* Size of the LRO hash table.  Must be a power of 2.  A larger table
 * means we can accelerate a larger number of streams.
 */
static unsigned lro_table_size = 128;
TUNABLE_INT(SFXGE_LRO_PARAM(table_size), &lro_table_size);
SYSCTL_UINT(_hw_sfxge_lro, OID_AUTO, table_size, CTLFLAG_RDTUN,
	    &lro_table_size, 0,
	    "Size of the LRO hash table (must be a power of 2)");

/* Maximum length of a hash chain.  If chains get too long then the lookup
 * time increases and may exceed the benefit of LRO.
 */
static unsigned lro_chain_max = 20;
TUNABLE_INT(SFXGE_LRO_PARAM(chain_max), &lro_chain_max);
SYSCTL_UINT(_hw_sfxge_lro, OID_AUTO, chain_max, CTLFLAG_RDTUN,
	    &lro_chain_max, 0,
	    "The maximum length of a hash chain");

/* Maximum time (in ticks) that a connection can be idle before it's LRO
 * state is discarded.
 */
static unsigned lro_idle_ticks; /* initialised in sfxge_rx_init() */
TUNABLE_INT(SFXGE_LRO_PARAM(idle_ticks), &lro_idle_ticks);
SYSCTL_UINT(_hw_sfxge_lro, OID_AUTO, idle_ticks, CTLFLAG_RDTUN,
	    &lro_idle_ticks, 0,
	    "The maximum time (in ticks) that a connection can be idle "
	    "before it's LRO state is discarded");

/* Number of packets with payload that must arrive in-order before a
 * connection is eligible for LRO.  The idea is we should avoid coalescing
 * segments when the sender is in slow-start because reducing the ACK rate
 * can damage performance.
 */
static int lro_slow_start_packets = 2000;
TUNABLE_INT(SFXGE_LRO_PARAM(slow_start_packets), &lro_slow_start_packets);
SYSCTL_UINT(_hw_sfxge_lro, OID_AUTO, slow_start_packets, CTLFLAG_RDTUN,
	    &lro_slow_start_packets, 0,
	    "Number of packets with payload that must arrive in-order before "
	    "a connection is eligible for LRO");

/* Number of packets with payload that must arrive in-order following loss
 * before a connection is eligible for LRO.  The idea is we should avoid
 * coalescing segments when the sender is recovering from loss, because
 * reducing the ACK rate can damage performance.
 */
static int lro_loss_packets = 20;
TUNABLE_INT(SFXGE_LRO_PARAM(loss_packets), &lro_loss_packets);
SYSCTL_UINT(_hw_sfxge_lro, OID_AUTO, loss_packets, CTLFLAG_RDTUN,
	    &lro_loss_packets, 0,
	    "Number of packets with payload that must arrive in-order "
	    "following loss before a connection is eligible for LRO");

/* Flags for sfxge_lro_conn::l2_id; must not collide with EVL_VLID_MASK */
#define	SFXGE_LRO_L2_ID_VLAN 0x4000
#define	SFXGE_LRO_L2_ID_IPV6 0x8000
#define	SFXGE_LRO_CONN_IS_VLAN_ENCAP(c) ((c)->l2_id & SFXGE_LRO_L2_ID_VLAN)
#define	SFXGE_LRO_CONN_IS_TCPIPV4(c) (!((c)->l2_id & SFXGE_LRO_L2_ID_IPV6))

/* Compare IPv6 addresses, avoiding conditional branches */
static unsigned long ipv6_addr_cmp(const struct in6_addr *left,
				   const struct in6_addr *right)
{
#if LONG_BIT == 64
	const uint64_t *left64 = (const uint64_t *)left;
	const uint64_t *right64 = (const uint64_t *)right;
	return (left64[0] - right64[0]) | (left64[1] - right64[1]);
#else
	return (left->s6_addr32[0] - right->s6_addr32[0]) |
	       (left->s6_addr32[1] - right->s6_addr32[1]) |
	       (left->s6_addr32[2] - right->s6_addr32[2]) |
	       (left->s6_addr32[3] - right->s6_addr32[3]);
#endif
}

#endif	/* SFXGE_LRO */

void
sfxge_rx_qflush_done(struct sfxge_rxq *rxq)
{

	rxq->flush_state = SFXGE_FLUSH_DONE;
}

void
sfxge_rx_qflush_failed(struct sfxge_rxq *rxq)
{

	rxq->flush_state = SFXGE_FLUSH_FAILED;
}

#ifdef RSS
static uint8_t toep_key[RSS_KEYSIZE];
#else
static uint8_t toep_key[] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};
#endif

static void
sfxge_rx_post_refill(void *arg)
{
	struct sfxge_rxq *rxq = arg;
	struct sfxge_softc *sc;
	unsigned int index;
	struct sfxge_evq *evq;
	uint16_t magic;

	sc = rxq->sc;
	index = rxq->index;
	evq = sc->evq[index];
	magic = sfxge_sw_ev_rxq_magic(SFXGE_SW_EV_RX_QREFILL, rxq);

	/* This is guaranteed due to the start/stop order of rx and ev */
	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq not started"));
	KASSERT(rxq->init_state == SFXGE_RXQ_STARTED,
	    ("rxq not started"));
	efx_ev_qpost(evq->common, magic);
}

static void
sfxge_rx_schedule_refill(struct sfxge_rxq *rxq, boolean_t retrying)
{
	/* Initially retry after 100 ms, but back off in case of
	 * repeated failures as we probably have to wait for the
	 * administrator to raise the pool limit. */
	if (retrying)
		rxq->refill_delay = min(rxq->refill_delay * 2, 10 * hz);
	else
		rxq->refill_delay = hz / 10;

	callout_reset_curcpu(&rxq->refill_callout, rxq->refill_delay,
			     sfxge_rx_post_refill, rxq);
}

#define	SFXGE_REFILL_BATCH  64

static void
sfxge_rx_qfill(struct sfxge_rxq *rxq, unsigned int target, boolean_t retrying)
{
	struct sfxge_softc *sc;
	unsigned int index;
	struct sfxge_evq *evq;
	unsigned int batch;
	unsigned int rxfill;
	unsigned int mblksize;
	int ntodo;
	efsys_dma_addr_t addr[SFXGE_REFILL_BATCH];

	sc = rxq->sc;
	index = rxq->index;
	evq = sc->evq[index];

	prefetch_read_many(sc->enp);
	prefetch_read_many(rxq->common);

	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	if (__predict_false(rxq->init_state != SFXGE_RXQ_STARTED))
		return;

	rxfill = rxq->added - rxq->completed;
	KASSERT(rxfill <= EFX_RXQ_LIMIT(rxq->entries),
	    ("rxfill > EFX_RXQ_LIMIT(rxq->entries)"));
	ntodo = min(EFX_RXQ_LIMIT(rxq->entries) - rxfill, target);
	KASSERT(ntodo <= EFX_RXQ_LIMIT(rxq->entries),
	    ("ntodo > EFX_RQX_LIMIT(rxq->entries)"));

	if (ntodo == 0)
		return;

	batch = 0;
	mblksize = sc->rx_buffer_size - sc->rx_buffer_align;
	while (ntodo-- > 0) {
		unsigned int id;
		struct sfxge_rx_sw_desc *rx_desc;
		bus_dma_segment_t seg;
		struct mbuf *m;

		id = (rxq->added + batch) & rxq->ptr_mask;
		rx_desc = &rxq->queue[id];
		KASSERT(rx_desc->mbuf == NULL, ("rx_desc->mbuf != NULL"));

		rx_desc->flags = EFX_DISCARD;
		m = rx_desc->mbuf = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
		    sc->rx_cluster_size);
		if (m == NULL)
			break;

		/* m_len specifies length of area to be mapped for DMA */
		m->m_len  = mblksize;
		m->m_data = (caddr_t)P2ROUNDUP((uintptr_t)m->m_data, CACHE_LINE_SIZE);
		m->m_data += sc->rx_buffer_align;

		sfxge_map_mbuf_fast(rxq->mem.esm_tag, rxq->mem.esm_map, m, &seg);
		addr[batch++] = seg.ds_addr;

		if (batch == SFXGE_REFILL_BATCH) {
			efx_rx_qpost(rxq->common, addr, mblksize, batch,
			    rxq->completed, rxq->added);
			rxq->added += batch;
			batch = 0;
		}
	}

	if (ntodo != 0)
		sfxge_rx_schedule_refill(rxq, retrying);

	if (batch != 0) {
		efx_rx_qpost(rxq->common, addr, mblksize, batch,
		    rxq->completed, rxq->added);
		rxq->added += batch;
	}

	/* Make the descriptors visible to the hardware */
	bus_dmamap_sync(rxq->mem.esm_tag, rxq->mem.esm_map,
			BUS_DMASYNC_PREWRITE);

	efx_rx_qpush(rxq->common, rxq->added, &rxq->pushed);

	/* The queue could still be empty if no descriptors were actually
	 * pushed, in which case there will be no event to cause the next
	 * refill, so we must schedule a refill ourselves.
	 */
	if(rxq->pushed == rxq->completed) {
		sfxge_rx_schedule_refill(rxq, retrying);
	}
}

void
sfxge_rx_qrefill(struct sfxge_rxq *rxq)
{

	if (__predict_false(rxq->init_state != SFXGE_RXQ_STARTED))
		return;

	/* Make sure the queue is full */
	sfxge_rx_qfill(rxq, EFX_RXQ_LIMIT(rxq->entries), B_TRUE);
}

static void __sfxge_rx_deliver(struct sfxge_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp = sc->ifnet;

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.csum_data = 0xffff;
	ifp->if_input(ifp, m);
}

static void
sfxge_rx_deliver(struct sfxge_rxq *rxq, struct sfxge_rx_sw_desc *rx_desc)
{
	struct sfxge_softc *sc = rxq->sc;
	struct mbuf *m = rx_desc->mbuf;
	int flags = rx_desc->flags;
	int csum_flags;

	/* Convert checksum flags */
	csum_flags = (flags & EFX_CKSUM_IPV4) ?
		(CSUM_IP_CHECKED | CSUM_IP_VALID) : 0;
	if (flags & EFX_CKSUM_TCPUDP)
		csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;

	if (flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
		m->m_pkthdr.flowid =
			efx_pseudo_hdr_hash_get(rxq->common,
						EFX_RX_HASHALG_TOEPLITZ,
						mtod(m, uint8_t *));
		/* The hash covers a 4-tuple for TCP only */
		M_HASHTYPE_SET(m,
		    (flags & EFX_PKT_IPV4) ?
			((flags & EFX_PKT_TCP) ?
			    M_HASHTYPE_RSS_TCP_IPV4 : M_HASHTYPE_RSS_IPV4) :
			((flags & EFX_PKT_TCP) ?
			    M_HASHTYPE_RSS_TCP_IPV6 : M_HASHTYPE_RSS_IPV6));
	}
	m->m_data += sc->rx_prefix_size;
	m->m_len = rx_desc->size - sc->rx_prefix_size;
	m->m_pkthdr.len = m->m_len;
	m->m_pkthdr.csum_flags = csum_flags;
	__sfxge_rx_deliver(sc, rx_desc->mbuf);

	rx_desc->flags = EFX_DISCARD;
	rx_desc->mbuf = NULL;
}

#ifdef SFXGE_LRO

static void
sfxge_lro_deliver(struct sfxge_lro_state *st, struct sfxge_lro_conn *c)
{
	struct sfxge_softc *sc = st->sc;
	struct mbuf *m = c->mbuf;
	struct tcphdr *c_th;
	int csum_flags;

	KASSERT(m, ("no mbuf to deliver"));

	++st->n_bursts;

	/* Finish off packet munging and recalculate IP header checksum. */
	if (SFXGE_LRO_CONN_IS_TCPIPV4(c)) {
		struct ip *iph = c->nh;
		iph->ip_len = htons(iph->ip_len);
		iph->ip_sum = 0;
		iph->ip_sum = in_cksum_hdr(iph);
		c_th = (struct tcphdr *)(iph + 1);
		csum_flags = (CSUM_DATA_VALID | CSUM_PSEUDO_HDR |
			      CSUM_IP_CHECKED | CSUM_IP_VALID);
	} else {
		struct ip6_hdr *iph = c->nh;
		iph->ip6_plen = htons(iph->ip6_plen);
		c_th = (struct tcphdr *)(iph + 1);
		csum_flags = CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
	}

	c_th->th_win = c->th_last->th_win;
	c_th->th_ack = c->th_last->th_ack;
	if (c_th->th_off == c->th_last->th_off) {
		/* Copy TCP options (take care to avoid going negative). */
		int optlen = ((c_th->th_off - 5) & 0xf) << 2u;
		memcpy(c_th + 1, c->th_last + 1, optlen);
	}

	m->m_pkthdr.flowid = c->conn_hash;
	M_HASHTYPE_SET(m,
	    SFXGE_LRO_CONN_IS_TCPIPV4(c) ?
		M_HASHTYPE_RSS_TCP_IPV4 : M_HASHTYPE_RSS_TCP_IPV6);

	m->m_pkthdr.csum_flags = csum_flags;
	__sfxge_rx_deliver(sc, m);

	c->mbuf = NULL;
	c->delivered = 1;
}

/* Drop the given connection, and add it to the free list. */
static void sfxge_lro_drop(struct sfxge_rxq *rxq, struct sfxge_lro_conn *c)
{
	unsigned bucket;

	KASSERT(!c->mbuf, ("found orphaned mbuf"));

	if (c->next_buf.mbuf != NULL) {
		sfxge_rx_deliver(rxq, &c->next_buf);
		LIST_REMOVE(c, active_link);
	}

	bucket = c->conn_hash & rxq->lro.conns_mask;
	KASSERT(rxq->lro.conns_n[bucket] > 0, ("LRO: bucket fill level wrong"));
	--rxq->lro.conns_n[bucket];
	TAILQ_REMOVE(&rxq->lro.conns[bucket], c, link);
	TAILQ_INSERT_HEAD(&rxq->lro.free_conns, c, link);
}

/* Stop tracking connections that have gone idle in order to keep hash
 * chains short.
 */
static void sfxge_lro_purge_idle(struct sfxge_rxq *rxq, unsigned now)
{
	struct sfxge_lro_conn *c;
	unsigned i;

	KASSERT(LIST_EMPTY(&rxq->lro.active_conns),
		("found active connections"));

	rxq->lro.last_purge_ticks = now;
	for (i = 0; i <= rxq->lro.conns_mask; ++i) {
		if (TAILQ_EMPTY(&rxq->lro.conns[i]))
			continue;

		c = TAILQ_LAST(&rxq->lro.conns[i], sfxge_lro_tailq);
		if (now - c->last_pkt_ticks > lro_idle_ticks) {
			++rxq->lro.n_drop_idle;
			sfxge_lro_drop(rxq, c);
		}
	}
}

static void
sfxge_lro_merge(struct sfxge_lro_state *st, struct sfxge_lro_conn *c,
		struct mbuf *mbuf, struct tcphdr *th)
{
	struct tcphdr *c_th;

	/* Tack the new mbuf onto the chain. */
	KASSERT(!mbuf->m_next, ("mbuf already chained"));
	c->mbuf_tail->m_next = mbuf;
	c->mbuf_tail = mbuf;

	/* Increase length appropriately */
	c->mbuf->m_pkthdr.len += mbuf->m_len;

	/* Update the connection state flags */
	if (SFXGE_LRO_CONN_IS_TCPIPV4(c)) {
		struct ip *iph = c->nh;
		iph->ip_len += mbuf->m_len;
		c_th = (struct tcphdr *)(iph + 1);
	} else {
		struct ip6_hdr *iph = c->nh;
		iph->ip6_plen += mbuf->m_len;
		c_th = (struct tcphdr *)(iph + 1);
	}
	c_th->th_flags |= (th->th_flags & TH_PUSH);
	c->th_last = th;
	++st->n_merges;

	/* Pass packet up now if another segment could overflow the IP
	 * length.
	 */
	if (c->mbuf->m_pkthdr.len > 65536 - 9200)
		sfxge_lro_deliver(st, c);
}

static void
sfxge_lro_start(struct sfxge_lro_state *st, struct sfxge_lro_conn *c,
		struct mbuf *mbuf, void *nh, struct tcphdr *th)
{
	/* Start the chain */
	c->mbuf = mbuf;
	c->mbuf_tail = c->mbuf;
	c->nh = nh;
	c->th_last = th;

	mbuf->m_pkthdr.len = mbuf->m_len;

	/* Mangle header fields for later processing */
	if (SFXGE_LRO_CONN_IS_TCPIPV4(c)) {
		struct ip *iph = nh;
		iph->ip_len = ntohs(iph->ip_len);
	} else {
		struct ip6_hdr *iph = nh;
		iph->ip6_plen = ntohs(iph->ip6_plen);
	}
}

/* Try to merge or otherwise hold or deliver (as appropriate) the
 * packet buffered for this connection (c->next_buf).  Return a flag
 * indicating whether the connection is still active for LRO purposes.
 */
static int
sfxge_lro_try_merge(struct sfxge_rxq *rxq, struct sfxge_lro_conn *c)
{
	struct sfxge_rx_sw_desc *rx_buf = &c->next_buf;
	char *eh = c->next_eh;
	int data_length, hdr_length, dont_merge;
	unsigned th_seq, pkt_length;
	struct tcphdr *th;
	unsigned now;

	if (SFXGE_LRO_CONN_IS_TCPIPV4(c)) {
		struct ip *iph = c->next_nh;
		th = (struct tcphdr *)(iph + 1);
		pkt_length = ntohs(iph->ip_len) + (char *) iph - eh;
	} else {
		struct ip6_hdr *iph = c->next_nh;
		th = (struct tcphdr *)(iph + 1);
		pkt_length = ntohs(iph->ip6_plen) + (char *) th - eh;
	}

	hdr_length = (char *) th + th->th_off * 4 - eh;
	data_length = (min(pkt_length, rx_buf->size - rxq->sc->rx_prefix_size) -
		       hdr_length);
	th_seq = ntohl(th->th_seq);
	dont_merge = ((data_length <= 0)
		      | (th->th_flags & (TH_URG | TH_SYN | TH_RST | TH_FIN)));

	/* Check for options other than aligned timestamp. */
	if (th->th_off != 5) {
		const uint32_t *opt_ptr = (const uint32_t *) (th + 1);
		if (th->th_off == 8 &&
		    opt_ptr[0] == ntohl((TCPOPT_NOP << 24) |
					(TCPOPT_NOP << 16) |
					(TCPOPT_TIMESTAMP << 8) |
					TCPOLEN_TIMESTAMP)) {
			/* timestamp option -- okay */
		} else {
			dont_merge = 1;
		}
	}

	if (__predict_false(th_seq != c->next_seq)) {
		/* Out-of-order, so start counting again. */
		if (c->mbuf != NULL)
			sfxge_lro_deliver(&rxq->lro, c);
		c->n_in_order_pkts -= lro_loss_packets;
		c->next_seq = th_seq + data_length;
		++rxq->lro.n_misorder;
		goto deliver_buf_out;
	}
	c->next_seq = th_seq + data_length;

	now = ticks;
	if (now - c->last_pkt_ticks > lro_idle_ticks) {
		++rxq->lro.n_drop_idle;
		if (c->mbuf != NULL)
			sfxge_lro_deliver(&rxq->lro, c);
		sfxge_lro_drop(rxq, c);
		return (0);
	}
	c->last_pkt_ticks = ticks;

	if (c->n_in_order_pkts < lro_slow_start_packets) {
		/* May be in slow-start, so don't merge. */
		++rxq->lro.n_slow_start;
		++c->n_in_order_pkts;
		goto deliver_buf_out;
	}

	if (__predict_false(dont_merge)) {
		if (c->mbuf != NULL)
			sfxge_lro_deliver(&rxq->lro, c);
		if (th->th_flags & (TH_FIN | TH_RST)) {
			++rxq->lro.n_drop_closed;
			sfxge_lro_drop(rxq, c);
			return (0);
		}
		goto deliver_buf_out;
	}

	rx_buf->mbuf->m_data += rxq->sc->rx_prefix_size;

	if (__predict_true(c->mbuf != NULL)) {
		/* Remove headers and any padding */
		rx_buf->mbuf->m_data += hdr_length;
		rx_buf->mbuf->m_len = data_length;

		sfxge_lro_merge(&rxq->lro, c, rx_buf->mbuf, th);
	} else {
		/* Remove any padding */
		rx_buf->mbuf->m_len = pkt_length;

		sfxge_lro_start(&rxq->lro, c, rx_buf->mbuf, c->next_nh, th);
	}

	rx_buf->mbuf = NULL;
	return (1);

 deliver_buf_out:
	sfxge_rx_deliver(rxq, rx_buf);
	return (1);
}

static void sfxge_lro_new_conn(struct sfxge_lro_state *st, uint32_t conn_hash,
			       uint16_t l2_id, void *nh, struct tcphdr *th)
{
	unsigned bucket = conn_hash & st->conns_mask;
	struct sfxge_lro_conn *c;

	if (st->conns_n[bucket] >= lro_chain_max) {
		++st->n_too_many;
		return;
	}

	if (!TAILQ_EMPTY(&st->free_conns)) {
		c = TAILQ_FIRST(&st->free_conns);
		TAILQ_REMOVE(&st->free_conns, c, link);
	} else {
		c = malloc(sizeof(*c), M_SFXGE, M_NOWAIT);
		if (c == NULL)
			return;
		c->mbuf = NULL;
		c->next_buf.mbuf = NULL;
	}

	/* Create the connection tracking data */
	++st->conns_n[bucket];
	TAILQ_INSERT_HEAD(&st->conns[bucket], c, link);
	c->l2_id = l2_id;
	c->conn_hash = conn_hash;
	c->source = th->th_sport;
	c->dest = th->th_dport;
	c->n_in_order_pkts = 0;
	c->last_pkt_ticks = *(volatile int *)&ticks;
	c->delivered = 0;
	++st->n_new_stream;
	/* NB. We don't initialise c->next_seq, and it doesn't matter what
	 * value it has.  Most likely the next packet received for this
	 * connection will not match -- no harm done.
	 */
}

/* Process mbuf and decide whether to dispatch it to the stack now or
 * later.
 */
static void
sfxge_lro(struct sfxge_rxq *rxq, struct sfxge_rx_sw_desc *rx_buf)
{
	struct sfxge_softc *sc = rxq->sc;
	struct mbuf *m = rx_buf->mbuf;
	struct ether_header *eh;
	struct sfxge_lro_conn *c;
	uint16_t l2_id;
	uint16_t l3_proto;
	void *nh;
	struct tcphdr *th;
	uint32_t conn_hash;
	unsigned bucket;

	/* Get the hardware hash */
	conn_hash = efx_pseudo_hdr_hash_get(rxq->common,
					    EFX_RX_HASHALG_TOEPLITZ,
					    mtod(m, uint8_t *));

	eh = (struct ether_header *)(m->m_data + sc->rx_prefix_size);
	if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
		struct ether_vlan_header *veh = (struct ether_vlan_header *)eh;
		l2_id = EVL_VLANOFTAG(ntohs(veh->evl_tag)) |
			SFXGE_LRO_L2_ID_VLAN;
		l3_proto = veh->evl_proto;
		nh = veh + 1;
	} else {
		l2_id = 0;
		l3_proto = eh->ether_type;
		nh = eh + 1;
	}

	/* Check whether this is a suitable packet (unfragmented
	 * TCP/IPv4 or TCP/IPv6).  If so, find the TCP header and
	 * length, and compute a hash if necessary.  If not, return.
	 */
	if (l3_proto == htons(ETHERTYPE_IP)) {
		struct ip *iph = nh;

		KASSERT(iph->ip_p == IPPROTO_TCP,
		    ("IPv4 protocol is not TCP, but packet marker is set"));
		if ((iph->ip_hl - (sizeof(*iph) >> 2u)) |
		    (iph->ip_off & htons(IP_MF | IP_OFFMASK)))
			goto deliver_now;
		th = (struct tcphdr *)(iph + 1);
	} else if (l3_proto == htons(ETHERTYPE_IPV6)) {
		struct ip6_hdr *iph = nh;

		KASSERT(iph->ip6_nxt == IPPROTO_TCP,
		    ("IPv6 next header is not TCP, but packet marker is set"));
		l2_id |= SFXGE_LRO_L2_ID_IPV6;
		th = (struct tcphdr *)(iph + 1);
	} else {
		goto deliver_now;
	}

	bucket = conn_hash & rxq->lro.conns_mask;

	TAILQ_FOREACH(c, &rxq->lro.conns[bucket], link) {
		if ((c->l2_id - l2_id) | (c->conn_hash - conn_hash))
			continue;
		if ((c->source - th->th_sport) | (c->dest - th->th_dport))
			continue;
		if (c->mbuf != NULL) {
			if (SFXGE_LRO_CONN_IS_TCPIPV4(c)) {
				struct ip *c_iph, *iph = nh;
				c_iph = c->nh;
				if ((c_iph->ip_src.s_addr - iph->ip_src.s_addr) |
				    (c_iph->ip_dst.s_addr - iph->ip_dst.s_addr))
					continue;
			} else {
				struct ip6_hdr *c_iph, *iph = nh;
				c_iph = c->nh;
				if (ipv6_addr_cmp(&c_iph->ip6_src, &iph->ip6_src) |
				    ipv6_addr_cmp(&c_iph->ip6_dst, &iph->ip6_dst))
					continue;
			}
		}

		/* Re-insert at head of list to reduce lookup time. */
		TAILQ_REMOVE(&rxq->lro.conns[bucket], c, link);
		TAILQ_INSERT_HEAD(&rxq->lro.conns[bucket], c, link);

		if (c->next_buf.mbuf != NULL) {
			if (!sfxge_lro_try_merge(rxq, c))
				goto deliver_now;
		} else {
			LIST_INSERT_HEAD(&rxq->lro.active_conns, c,
			    active_link);
		}
		c->next_buf = *rx_buf;
		c->next_eh = eh;
		c->next_nh = nh;

		rx_buf->mbuf = NULL;
		rx_buf->flags = EFX_DISCARD;
		return;
	}

	sfxge_lro_new_conn(&rxq->lro, conn_hash, l2_id, nh, th);
 deliver_now:
	sfxge_rx_deliver(rxq, rx_buf);
}

static void sfxge_lro_end_of_burst(struct sfxge_rxq *rxq)
{
	struct sfxge_lro_state *st = &rxq->lro;
	struct sfxge_lro_conn *c;
	unsigned t;

	while (!LIST_EMPTY(&st->active_conns)) {
		c = LIST_FIRST(&st->active_conns);
		if (!c->delivered && c->mbuf != NULL)
			sfxge_lro_deliver(st, c);
		if (sfxge_lro_try_merge(rxq, c)) {
			if (c->mbuf != NULL)
				sfxge_lro_deliver(st, c);
			LIST_REMOVE(c, active_link);
		}
		c->delivered = 0;
	}

	t = *(volatile int *)&ticks;
	if (__predict_false(t != st->last_purge_ticks))
		sfxge_lro_purge_idle(rxq, t);
}

#else	/* !SFXGE_LRO */

static void
sfxge_lro(struct sfxge_rxq *rxq, struct sfxge_rx_sw_desc *rx_buf)
{
}

static void
sfxge_lro_end_of_burst(struct sfxge_rxq *rxq)
{
}

#endif	/* SFXGE_LRO */

void
sfxge_rx_qcomplete(struct sfxge_rxq *rxq, boolean_t eop)
{
	struct sfxge_softc *sc = rxq->sc;
	int if_capenable = sc->ifnet->if_capenable;
	int lro_enabled = if_capenable & IFCAP_LRO;
	unsigned int index;
	struct sfxge_evq *evq;
	unsigned int completed;
	unsigned int level;
	struct mbuf *m;
	struct sfxge_rx_sw_desc *prev = NULL;

	index = rxq->index;
	evq = sc->evq[index];

	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	completed = rxq->completed;
	while (completed != rxq->pending) {
		unsigned int id;
		struct sfxge_rx_sw_desc *rx_desc;

		id = completed++ & rxq->ptr_mask;
		rx_desc = &rxq->queue[id];
		m = rx_desc->mbuf;

		if (__predict_false(rxq->init_state != SFXGE_RXQ_STARTED))
			goto discard;

		if (rx_desc->flags & (EFX_ADDR_MISMATCH | EFX_DISCARD))
			goto discard;

		/* Read the length from the pseudo header if required */
		if (rx_desc->flags & EFX_PKT_PREFIX_LEN) {
			uint16_t tmp_size;
			int rc;
			rc = efx_pseudo_hdr_pkt_length_get(rxq->common,
							   mtod(m, uint8_t *),
							   &tmp_size);
			KASSERT(rc == 0, ("cannot get packet length: %d", rc));
			rx_desc->size = (int)tmp_size + sc->rx_prefix_size;
		}

		prefetch_read_many(mtod(m, caddr_t));

		switch (rx_desc->flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
		case EFX_PKT_IPV4:
			if (~if_capenable & IFCAP_RXCSUM)
				rx_desc->flags &=
				    ~(EFX_CKSUM_IPV4 | EFX_CKSUM_TCPUDP);
			break;
		case EFX_PKT_IPV6:
			if (~if_capenable & IFCAP_RXCSUM_IPV6)
				rx_desc->flags &= ~EFX_CKSUM_TCPUDP;
			break;
		case 0:
			/* Check for loopback packets */
			{
				struct ether_header *etherhp;

				/*LINTED*/
				etherhp = mtod(m, struct ether_header *);

				if (etherhp->ether_type ==
				    htons(SFXGE_ETHERTYPE_LOOPBACK)) {
					EFSYS_PROBE(loopback);

					rxq->loopback++;
					goto discard;
				}
			}
			break;
		default:
			KASSERT(B_FALSE,
			    ("Rx descriptor with both IPv4 and IPv6 flags"));
			goto discard;
		}

		/* Pass packet up the stack or into LRO (pipelined) */
		if (prev != NULL) {
			if (lro_enabled &&
			    ((prev->flags & (EFX_PKT_TCP | EFX_CKSUM_TCPUDP)) ==
			     (EFX_PKT_TCP | EFX_CKSUM_TCPUDP)))
				sfxge_lro(rxq, prev);
			else
				sfxge_rx_deliver(rxq, prev);
		}
		prev = rx_desc;
		continue;

discard:
		/* Return the packet to the pool */
		m_free(m);
		rx_desc->mbuf = NULL;
	}
	rxq->completed = completed;

	level = rxq->added - rxq->completed;

	/* Pass last packet up the stack or into LRO */
	if (prev != NULL) {
		if (lro_enabled &&
		    ((prev->flags & (EFX_PKT_TCP | EFX_CKSUM_TCPUDP)) ==
		     (EFX_PKT_TCP | EFX_CKSUM_TCPUDP)))
			sfxge_lro(rxq, prev);
		else
			sfxge_rx_deliver(rxq, prev);
	}

	/*
	 * If there are any pending flows and this is the end of the
	 * poll then they must be completed.
	 */
	if (eop)
		sfxge_lro_end_of_burst(rxq);

	/* Top up the queue if necessary */
	if (level < rxq->refill_threshold)
		sfxge_rx_qfill(rxq, EFX_RXQ_LIMIT(rxq->entries), B_FALSE);
}

static void
sfxge_rx_qstop(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_rxq *rxq;
	struct sfxge_evq *evq;
	unsigned int count;
	unsigned int retry = 3;

	SFXGE_ADAPTER_LOCK_ASSERT_OWNED(sc);

	rxq = sc->rxq[index];
	evq = sc->evq[index];

	SFXGE_EVQ_LOCK(evq);

	KASSERT(rxq->init_state == SFXGE_RXQ_STARTED,
	    ("rxq not started"));

	rxq->init_state = SFXGE_RXQ_INITIALIZED;

	callout_stop(&rxq->refill_callout);

	while (rxq->flush_state != SFXGE_FLUSH_DONE && retry != 0) {
		rxq->flush_state = SFXGE_FLUSH_PENDING;

		SFXGE_EVQ_UNLOCK(evq);

		/* Flush the receive queue */
		if (efx_rx_qflush(rxq->common) != 0) {
			SFXGE_EVQ_LOCK(evq);
			rxq->flush_state = SFXGE_FLUSH_FAILED;
			break;
		}

		count = 0;
		do {
			/* Spin for 100 ms */
			DELAY(100000);

			if (rxq->flush_state != SFXGE_FLUSH_PENDING)
				break;

		} while (++count < 20);

		SFXGE_EVQ_LOCK(evq);

		if (rxq->flush_state == SFXGE_FLUSH_PENDING) {
			/* Flush timeout - neither done nor failed */
			log(LOG_ERR, "%s: Cannot flush Rx queue %u\n",
			    device_get_nameunit(sc->dev), index);
			rxq->flush_state = SFXGE_FLUSH_DONE;
		}
		retry--;
	}
	if (rxq->flush_state == SFXGE_FLUSH_FAILED) {
		log(LOG_ERR, "%s: Flushing Rx queue %u failed\n",
		    device_get_nameunit(sc->dev), index);
		rxq->flush_state = SFXGE_FLUSH_DONE;
	}

	rxq->pending = rxq->added;
	sfxge_rx_qcomplete(rxq, B_TRUE);

	KASSERT(rxq->completed == rxq->pending,
	    ("rxq->completed != rxq->pending"));

	rxq->added = 0;
	rxq->pushed = 0;
	rxq->pending = 0;
	rxq->completed = 0;
	rxq->loopback = 0;

	/* Destroy the common code receive queue. */
	efx_rx_qdestroy(rxq->common);

	efx_sram_buf_tbl_clear(sc->enp, rxq->buf_base_id,
	    EFX_RXQ_NBUFS(sc->rxq_entries));

	SFXGE_EVQ_UNLOCK(evq);
}

static int
sfxge_rx_qstart(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_rxq *rxq;
	efsys_mem_t *esmp;
	struct sfxge_evq *evq;
	int rc;

	SFXGE_ADAPTER_LOCK_ASSERT_OWNED(sc);

	rxq = sc->rxq[index];
	esmp = &rxq->mem;
	evq = sc->evq[index];

	KASSERT(rxq->init_state == SFXGE_RXQ_INITIALIZED,
	    ("rxq->init_state != SFXGE_RXQ_INITIALIZED"));
	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq->init_state != SFXGE_EVQ_STARTED"));

	/* Program the buffer table. */
	if ((rc = efx_sram_buf_tbl_set(sc->enp, rxq->buf_base_id, esmp,
	    EFX_RXQ_NBUFS(sc->rxq_entries))) != 0)
		return (rc);

	/* Create the common code receive queue. */
	if ((rc = efx_rx_qcreate(sc->enp, index, 0, EFX_RXQ_TYPE_DEFAULT,
	    esmp, sc->rxq_entries, rxq->buf_base_id, EFX_RXQ_FLAG_NONE,
	    evq->common, &rxq->common)) != 0)
		goto fail;

	SFXGE_EVQ_LOCK(evq);

	/* Enable the receive queue. */
	efx_rx_qenable(rxq->common);

	rxq->init_state = SFXGE_RXQ_STARTED;
	rxq->flush_state = SFXGE_FLUSH_REQUIRED;

	/* Try to fill the queue from the pool. */
	sfxge_rx_qfill(rxq, EFX_RXQ_LIMIT(sc->rxq_entries), B_FALSE);

	SFXGE_EVQ_UNLOCK(evq);

	return (0);

fail:
	efx_sram_buf_tbl_clear(sc->enp, rxq->buf_base_id,
	    EFX_RXQ_NBUFS(sc->rxq_entries));
	return (rc);
}

void
sfxge_rx_stop(struct sfxge_softc *sc)
{
	int index;

	efx_mac_filter_default_rxq_clear(sc->enp);

	/* Stop the receive queue(s) */
	index = sc->rxq_count;
	while (--index >= 0)
		sfxge_rx_qstop(sc, index);

	sc->rx_prefix_size = 0;
	sc->rx_buffer_size = 0;

	efx_rx_fini(sc->enp);
}

int
sfxge_rx_start(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	const efx_nic_cfg_t *encp;
	size_t hdrlen, align, reserved;
	int index;
	int rc;

	intr = &sc->intr;

	/* Initialize the common code receive module. */
	if ((rc = efx_rx_init(sc->enp)) != 0)
		return (rc);

	encp = efx_nic_cfg_get(sc->enp);
	sc->rx_buffer_size = EFX_MAC_PDU(sc->ifnet->if_mtu);

	/* Calculate the receive packet buffer size. */
	sc->rx_prefix_size = encp->enc_rx_prefix_size;

	/* Ensure IP headers are 32bit aligned */
	hdrlen = sc->rx_prefix_size + sizeof (struct ether_header);
	sc->rx_buffer_align = P2ROUNDUP(hdrlen, 4) - hdrlen;

	sc->rx_buffer_size += sc->rx_buffer_align;

	/* Align end of packet buffer for RX DMA end padding */
	align = MAX(1, encp->enc_rx_buf_align_end);
	EFSYS_ASSERT(ISP2(align));
	sc->rx_buffer_size = P2ROUNDUP(sc->rx_buffer_size, align);

	/*
	 * Standard mbuf zones only guarantee pointer-size alignment;
	 * we need extra space to align to the cache line
	 */
	reserved = sc->rx_buffer_size + CACHE_LINE_SIZE;

	/* Select zone for packet buffers */
	if (reserved <= MCLBYTES)
		sc->rx_cluster_size = MCLBYTES;
	else if (reserved <= MJUMPAGESIZE)
		sc->rx_cluster_size = MJUMPAGESIZE;
	else if (reserved <= MJUM9BYTES)
		sc->rx_cluster_size = MJUM9BYTES;
	else
		sc->rx_cluster_size = MJUM16BYTES;

	/*
	 * Set up the scale table.  Enable all hash types and hash insertion.
	 */
	for (index = 0; index < nitems(sc->rx_indir_table); index++)
#ifdef RSS
		sc->rx_indir_table[index] =
			rss_get_indirection_to_bucket(index) % sc->rxq_count;
#else
		sc->rx_indir_table[index] = index % sc->rxq_count;
#endif
	if ((rc = efx_rx_scale_tbl_set(sc->enp, EFX_RSS_CONTEXT_DEFAULT,
				       sc->rx_indir_table,
				       nitems(sc->rx_indir_table))) != 0)
		goto fail;
	(void)efx_rx_scale_mode_set(sc->enp, EFX_RSS_CONTEXT_DEFAULT,
	    EFX_RX_HASHALG_TOEPLITZ,
	    EFX_RX_HASH_IPV4 | EFX_RX_HASH_TCPIPV4 |
	    EFX_RX_HASH_IPV6 | EFX_RX_HASH_TCPIPV6, B_TRUE);

#ifdef RSS
	rss_getkey(toep_key);
#endif
	if ((rc = efx_rx_scale_key_set(sc->enp, EFX_RSS_CONTEXT_DEFAULT,
				       toep_key,
				       sizeof(toep_key))) != 0)
		goto fail;

	/* Start the receive queue(s). */
	for (index = 0; index < sc->rxq_count; index++) {
		if ((rc = sfxge_rx_qstart(sc, index)) != 0)
			goto fail2;
	}

	rc = efx_mac_filter_default_rxq_set(sc->enp, sc->rxq[0]->common,
					    sc->intr.n_alloc > 1);
	if (rc != 0)
		goto fail3;

	return (0);

fail3:
fail2:
	while (--index >= 0)
		sfxge_rx_qstop(sc, index);

fail:
	efx_rx_fini(sc->enp);

	return (rc);
}

#ifdef SFXGE_LRO

static void sfxge_lro_init(struct sfxge_rxq *rxq)
{
	struct sfxge_lro_state *st = &rxq->lro;
	unsigned i;

	st->conns_mask = lro_table_size - 1;
	KASSERT(!((st->conns_mask + 1) & st->conns_mask),
		("lro_table_size must be a power of 2"));
	st->sc = rxq->sc;
	st->conns = malloc((st->conns_mask + 1) * sizeof(st->conns[0]),
			   M_SFXGE, M_WAITOK);
	st->conns_n = malloc((st->conns_mask + 1) * sizeof(st->conns_n[0]),
			     M_SFXGE, M_WAITOK);
	for (i = 0; i <= st->conns_mask; ++i) {
		TAILQ_INIT(&st->conns[i]);
		st->conns_n[i] = 0;
	}
	LIST_INIT(&st->active_conns);
	TAILQ_INIT(&st->free_conns);
}

static void sfxge_lro_fini(struct sfxge_rxq *rxq)
{
	struct sfxge_lro_state *st = &rxq->lro;
	struct sfxge_lro_conn *c;
	unsigned i;

	/* Return cleanly if sfxge_lro_init() has not been called. */
	if (st->conns == NULL)
		return;

	KASSERT(LIST_EMPTY(&st->active_conns), ("found active connections"));

	for (i = 0; i <= st->conns_mask; ++i) {
		while (!TAILQ_EMPTY(&st->conns[i])) {
			c = TAILQ_LAST(&st->conns[i], sfxge_lro_tailq);
			sfxge_lro_drop(rxq, c);
		}
	}

	while (!TAILQ_EMPTY(&st->free_conns)) {
		c = TAILQ_FIRST(&st->free_conns);
		TAILQ_REMOVE(&st->free_conns, c, link);
		KASSERT(!c->mbuf, ("found orphaned mbuf"));
		free(c, M_SFXGE);
	}

	free(st->conns_n, M_SFXGE);
	free(st->conns, M_SFXGE);
	st->conns = NULL;
}

#else

static void
sfxge_lro_init(struct sfxge_rxq *rxq)
{
}

static void
sfxge_lro_fini(struct sfxge_rxq *rxq)
{
}

#endif	/* SFXGE_LRO */

static void
sfxge_rx_qfini(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_rxq *rxq;

	rxq = sc->rxq[index];

	KASSERT(rxq->init_state == SFXGE_RXQ_INITIALIZED,
	    ("rxq->init_state != SFXGE_RXQ_INITIALIZED"));

	/* Free the context array and the flow table. */
	free(rxq->queue, M_SFXGE);
	sfxge_lro_fini(rxq);

	/* Release DMA memory. */
	sfxge_dma_free(&rxq->mem);

	sc->rxq[index] = NULL;

	free(rxq, M_SFXGE);
}

static int
sfxge_rx_qinit(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_rxq *rxq;
	struct sfxge_evq *evq;
	efsys_mem_t *esmp;
	int rc;

	KASSERT(index < sc->rxq_count, ("index >= %d", sc->rxq_count));

	rxq = malloc(sizeof(struct sfxge_rxq), M_SFXGE, M_ZERO | M_WAITOK);
	rxq->sc = sc;
	rxq->index = index;
	rxq->entries = sc->rxq_entries;
	rxq->ptr_mask = rxq->entries - 1;
	rxq->refill_threshold = RX_REFILL_THRESHOLD(rxq->entries);

	sc->rxq[index] = rxq;
	esmp = &rxq->mem;

	evq = sc->evq[index];

	/* Allocate and zero DMA space. */
	if ((rc = sfxge_dma_alloc(sc, EFX_RXQ_SIZE(sc->rxq_entries), esmp)) != 0)
		return (rc);

	/* Allocate buffer table entries. */
	sfxge_sram_buf_tbl_alloc(sc, EFX_RXQ_NBUFS(sc->rxq_entries),
				 &rxq->buf_base_id);

	/* Allocate the context array and the flow table. */
	rxq->queue = malloc(sizeof(struct sfxge_rx_sw_desc) * sc->rxq_entries,
	    M_SFXGE, M_WAITOK | M_ZERO);
	sfxge_lro_init(rxq);

	callout_init(&rxq->refill_callout, 1);

	rxq->init_state = SFXGE_RXQ_INITIALIZED;

	return (0);
}

static const struct {
	const char *name;
	size_t offset;
} sfxge_rx_stats[] = {
#define	SFXGE_RX_STAT(name, member) \
	{ #name, offsetof(struct sfxge_rxq, member) }
#ifdef SFXGE_LRO
	SFXGE_RX_STAT(lro_merges, lro.n_merges),
	SFXGE_RX_STAT(lro_bursts, lro.n_bursts),
	SFXGE_RX_STAT(lro_slow_start, lro.n_slow_start),
	SFXGE_RX_STAT(lro_misorder, lro.n_misorder),
	SFXGE_RX_STAT(lro_too_many, lro.n_too_many),
	SFXGE_RX_STAT(lro_new_stream, lro.n_new_stream),
	SFXGE_RX_STAT(lro_drop_idle, lro.n_drop_idle),
	SFXGE_RX_STAT(lro_drop_closed, lro.n_drop_closed)
#endif
};

static int
sfxge_rx_stat_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	unsigned int id = arg2;
	unsigned int sum, index;

	/* Sum across all RX queues */
	sum = 0;
	for (index = 0; index < sc->rxq_count; index++)
		sum += *(unsigned int *)((caddr_t)sc->rxq[index] +
					 sfxge_rx_stats[id].offset);

	return (SYSCTL_OUT(req, &sum, sizeof(sum)));
}

static void
sfxge_rx_stat_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid_list *stat_list;
	unsigned int id;

	stat_list = SYSCTL_CHILDREN(sc->stats_node);

	for (id = 0; id < nitems(sfxge_rx_stats); id++) {
		SYSCTL_ADD_PROC(
			ctx, stat_list,
			OID_AUTO, sfxge_rx_stats[id].name,
			CTLTYPE_UINT|CTLFLAG_RD,
			sc, id, sfxge_rx_stat_handler, "IU",
			"");
	}
}

void
sfxge_rx_fini(struct sfxge_softc *sc)
{
	int index;

	index = sc->rxq_count;
	while (--index >= 0)
		sfxge_rx_qfini(sc, index);

	sc->rxq_count = 0;
}

int
sfxge_rx_init(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	int index;
	int rc;

#ifdef SFXGE_LRO
	if (!ISP2(lro_table_size)) {
		log(LOG_ERR, "%s=%u must be power of 2",
		    SFXGE_LRO_PARAM(table_size), lro_table_size);
		rc = EINVAL;
		goto fail_lro_table_size;
	}

	if (lro_idle_ticks == 0)
		lro_idle_ticks = hz / 10 + 1; /* 100 ms */
#endif

	intr = &sc->intr;

	sc->rxq_count = intr->n_alloc;

	KASSERT(intr->state == SFXGE_INTR_INITIALIZED,
	    ("intr->state != SFXGE_INTR_INITIALIZED"));

	/* Initialize the receive queue(s) - one per interrupt. */
	for (index = 0; index < sc->rxq_count; index++) {
		if ((rc = sfxge_rx_qinit(sc, index)) != 0)
			goto fail;
	}

	sfxge_rx_stat_init(sc);

	return (0);

fail:
	/* Tear down the receive queue(s). */
	while (--index >= 0)
		sfxge_rx_qfini(sc, index);

	sc->rxq_count = 0;

#ifdef SFXGE_LRO
fail_lro_table_size:
#endif
	return (rc);
}
