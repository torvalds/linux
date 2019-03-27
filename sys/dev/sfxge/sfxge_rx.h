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
 *
 * $FreeBSD$
 */

#ifndef _SFXGE_RX_H
#define	_SFXGE_RX_H

#include "opt_inet.h"
#include "opt_inet6.h"

#if defined(INET) || defined(INET6)
#define	SFXGE_LRO	1
#endif

#define	SFXGE_RX_SCALE_MAX	EFX_MAXRSS

struct sfxge_rx_sw_desc {
	struct mbuf	*mbuf;
	bus_dmamap_t	map;
	int		flags;
	int		size;
};

#ifdef SFXGE_LRO

/**
 * struct sfxge_lro_conn - Connection state for software LRO
 * @link: Link for hash table and free list.
 * @active_link: Link for active_conns list
 * @l2_id: Identifying information from layer 2
 * @conn_hash: Hash of connection 4-tuple
 * @nh: IP (v4 or v6) header of super-packet
 * @source: Source TCP port number
 * @dest: Destination TCP port number
 * @n_in_order_pkts: Number of in-order packets with payload.
 * @next_seq: Next in-order sequence number.
 * @last_pkt_ticks: Time we last saw a packet on this connection.
 * @mbuf: The mbuf we are currently holding.
 *	If %NULL, then all following fields are undefined.
 * @mbuf_tail: The tail of the frag_list of mbufs we're holding.
 *	Only valid after at least one merge.
 * @th_last: The TCP header of the last packet merged.
 * @next_buf: The next RX buffer to process.
 * @next_eh: Ethernet header of the next buffer.
 * @next_nh: IP header of the next buffer.
 * @delivered: True if we've delivered a payload packet up this interrupt.
 */
struct sfxge_lro_conn {
	TAILQ_ENTRY(sfxge_lro_conn) link;
	LIST_ENTRY(sfxge_lro_conn) active_link;
	uint16_t l2_id;
	uint32_t conn_hash;
	void *nh;
	uint16_t source, dest;
	int n_in_order_pkts;
	unsigned next_seq;
	unsigned last_pkt_ticks;
	struct mbuf *mbuf;
	struct mbuf *mbuf_tail;
	struct tcphdr *th_last;
	struct sfxge_rx_sw_desc next_buf;
	void *next_eh;
	void *next_nh;
	int delivered;
};

/**
 * struct sfxge_lro_state - Port state for software LRO
 * @sc: The associated NIC.
 * @conns_mask: Number of hash buckets - 1.
 * @conns: Hash buckets for tracked connections.
 * @conns_n: Length of linked list for each hash bucket.
 * @active_conns: Connections that are holding a packet.
 *	Connections are self-linked when not in this list.
 * @free_conns: Free sfxge_lro_conn instances.
 * @last_purge_ticks: The value of ticks last time we purged idle
 *	connections.
 * @n_merges: Number of packets absorbed by LRO.
 * @n_bursts: Number of bursts spotted by LRO.
 * @n_slow_start: Number of packets not merged because connection may be in
 *	slow-start.
 * @n_misorder: Number of out-of-order packets seen in tracked streams.
 * @n_too_many: Incremented when we're trying to track too many streams.
 * @n_new_stream: Number of distinct streams we've tracked.
 * @n_drop_idle: Number of streams discarded because they went idle.
 * @n_drop_closed: Number of streams that have seen a FIN or RST.
 */
struct sfxge_lro_state {
	struct sfxge_softc *sc;
	unsigned conns_mask;
	TAILQ_HEAD(sfxge_lro_tailq, sfxge_lro_conn) *conns;
	unsigned *conns_n;
	LIST_HEAD(, sfxge_lro_conn) active_conns;
	TAILQ_HEAD(, sfxge_lro_conn) free_conns;
	unsigned last_purge_ticks;
	unsigned n_merges;
	unsigned n_bursts;
	unsigned n_slow_start;
	unsigned n_misorder;
	unsigned n_too_many;
	unsigned n_new_stream;
	unsigned n_drop_idle;
	unsigned n_drop_closed;
};

#endif	/* SFXGE_LRO */

enum sfxge_flush_state {
	SFXGE_FLUSH_DONE = 0,
	SFXGE_FLUSH_REQUIRED,
	SFXGE_FLUSH_PENDING,
	SFXGE_FLUSH_FAILED
};

enum sfxge_rxq_state {
	SFXGE_RXQ_UNINITIALIZED = 0,
	SFXGE_RXQ_INITIALIZED,
	SFXGE_RXQ_STARTED
};

#define	SFXGE_RX_BATCH	128

struct sfxge_rxq {
	struct sfxge_softc		*sc __aligned(CACHE_LINE_SIZE);
	unsigned int			index;
	efsys_mem_t			mem;
	enum sfxge_rxq_state		init_state;
	unsigned int			entries;
	unsigned int			ptr_mask;
	efx_rxq_t			*common;

	struct sfxge_rx_sw_desc		*queue __aligned(CACHE_LINE_SIZE);
	unsigned int			added;
	unsigned int			pushed;
	unsigned int			pending;
	unsigned int			completed;
	unsigned int			loopback;
#ifdef SFXGE_LRO
	struct sfxge_lro_state		lro;
#endif
	unsigned int			refill_threshold;
	struct callout			refill_callout;
	unsigned int			refill_delay;

	volatile enum sfxge_flush_state	flush_state __aligned(CACHE_LINE_SIZE);
	unsigned int			buf_base_id;
};

/*
 * From sfxge_rx.c.
 */
extern int sfxge_rx_init(struct sfxge_softc *sc);
extern void sfxge_rx_fini(struct sfxge_softc *sc);
extern int sfxge_rx_start(struct sfxge_softc *sc);
extern void sfxge_rx_stop(struct sfxge_softc *sc);
extern void sfxge_rx_qcomplete(struct sfxge_rxq *rxq, boolean_t eop);
extern void sfxge_rx_qrefill(struct sfxge_rxq *rxq);
extern void sfxge_rx_qflush_done(struct sfxge_rxq *rxq);
extern void sfxge_rx_qflush_failed(struct sfxge_rxq *rxq);
extern void sfxge_rx_scale_update(void *arg, int npending);

#endif
