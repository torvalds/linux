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

#ifndef _SFXGE_TX_H
#define	_SFXGE_TX_H

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

/* If defined, parse TX packets directly in if_transmit
 * for better cache locality and reduced time under TX lock
 */
#define SFXGE_TX_PARSE_EARLY 1

/* Maximum size of TSO packet */
#define	SFXGE_TSO_MAX_SIZE		(65535)

/*
 * Maximum number of segments to be created for a TSO packet.
 * Allow for a reasonable minimum MSS of 512.
 */
#define	SFXGE_TSO_MAX_SEGS		howmany(SFXGE_TSO_MAX_SIZE, 512)

/* Maximum number of DMA segments needed to map an mbuf chain.  With
 * TSO, the mbuf length may be just over 64K, divided into 2K mbuf
 * clusters taking into account that the first may be not 2K cluster
 * boundary aligned.
 * Packet header may be split into two segments because of, for example,
 * VLAN header insertion.
 * The chain could be longer than this initially, but can be shortened
 * with m_collapse().
 */
#define	SFXGE_TX_MAPPING_MAX_SEG					\
	(2 + howmany(SFXGE_TSO_MAX_SIZE, MCLBYTES) + 1)

/*
 * Buffer mapping flags.
 *
 * Buffers and DMA mappings must be freed when the last descriptor
 * referring to them is completed.  Set the TX_BUF_UNMAP and
 * TX_BUF_MBUF flags on the last descriptor generated for an mbuf
 * chain.  Set only the TX_BUF_UNMAP flag on a descriptor referring to
 * a heap buffer.
 */
enum sfxge_tx_buf_flags {
	TX_BUF_UNMAP = 1,
	TX_BUF_MBUF = 2,
};

/*
 * Buffer mapping information for descriptors in flight.
 */
struct sfxge_tx_mapping {
	union {
		struct mbuf	*mbuf;
		caddr_t		heap_buf;
	}			u;
	bus_dmamap_t		map;
	enum sfxge_tx_buf_flags	flags;
};

#define	SFXGE_TX_DPL_GET_PKT_LIMIT_DEFAULT		(64 * 1024)
#define	SFXGE_TX_DPL_GET_NON_TCP_PKT_LIMIT_DEFAULT	1024
#define	SFXGE_TX_DPL_PUT_PKT_LIMIT_DEFAULT		1024

/*
 * Deferred packet list.
 */
struct sfxge_tx_dpl {
	unsigned int	std_get_max;		/* Maximum number  of packets
						 * in get list */
	unsigned int	std_get_non_tcp_max;	/* Maximum number
						 * of non-TCP packets
						 * in get list */
	unsigned int	std_put_max;		/* Maximum number of packets
						 * in put list */
	uintptr_t	std_put;		/* Head of put list. */
	struct mbuf	*std_get;		/* Head of get list. */
	struct mbuf	**std_getp;		/* Tail of get list. */
	unsigned int	std_get_count;		/* Packets in get list. */
	unsigned int	std_get_non_tcp_count;	/* Non-TCP packets
						 * in get list */
	unsigned int	std_get_hiwat;		/* Packets in get list
						 * high watermark */
	unsigned int	std_put_hiwat;		/* Packets in put list
						 * high watermark */
};


#define	SFXGE_TX_BUFFER_SIZE	0x400
#define	SFXGE_TX_HEADER_SIZE	0x100
#define	SFXGE_TX_COPY_THRESHOLD	0x200

enum sfxge_txq_state {
	SFXGE_TXQ_UNINITIALIZED = 0,
	SFXGE_TXQ_INITIALIZED,
	SFXGE_TXQ_STARTED
};

enum sfxge_txq_type {
	SFXGE_TXQ_NON_CKSUM = 0,
	SFXGE_TXQ_IP_CKSUM,
	SFXGE_TXQ_IP_TCP_UDP_CKSUM,
	SFXGE_TXQ_NTYPES
};

#define	SFXGE_EVQ0_N_TXQ(_sc)						\
	((_sc)->txq_dynamic_cksum_toggle_supported ?			\
	1 : SFXGE_TXQ_NTYPES)

#define	SFXGE_TXQ_UNBLOCK_LEVEL(_entries)	(EFX_TXQ_LIMIT(_entries) / 4)

#define	SFXGE_TX_BATCH	64

#define	SFXGE_TXQ_LOCK_INIT(_txq, _ifname, _txq_index)			\
	do {								\
		struct sfxge_txq  *__txq = (_txq);			\
									\
		snprintf((__txq)->lock_name,				\
			 sizeof((__txq)->lock_name),			\
			 "%s:txq%u", (_ifname), (_txq_index));		\
		mtx_init(&(__txq)->lock, (__txq)->lock_name,		\
			 NULL, MTX_DEF);				\
	} while (B_FALSE)
#define	SFXGE_TXQ_LOCK_DESTROY(_txq)					\
	mtx_destroy(&(_txq)->lock)
#define	SFXGE_TXQ_LOCK(_txq)						\
	mtx_lock(&(_txq)->lock)
#define	SFXGE_TXQ_TRYLOCK(_txq)						\
	mtx_trylock(&(_txq)->lock)
#define	SFXGE_TXQ_UNLOCK(_txq)						\
	mtx_unlock(&(_txq)->lock)
#define	SFXGE_TXQ_LOCK_ASSERT_OWNED(_txq)				\
	mtx_assert(&(_txq)->lock, MA_OWNED)
#define	SFXGE_TXQ_LOCK_ASSERT_NOTOWNED(_txq)				\
	mtx_assert(&(_txq)->lock, MA_NOTOWNED)


struct sfxge_txq {
	/* The following fields should be written very rarely */
	struct sfxge_softc		*sc;
	enum sfxge_txq_state		init_state;
	enum sfxge_flush_state		flush_state;
	unsigned int			tso_fw_assisted;
	enum sfxge_txq_type		type;
	unsigned int			evq_index;
	efsys_mem_t			mem;
	unsigned int			buf_base_id;
	unsigned int			entries;
	unsigned int			ptr_mask;
	unsigned int			max_pkt_desc;

	struct sfxge_tx_mapping		*stmp;	/* Packets in flight. */
	bus_dma_tag_t			packet_dma_tag;
	efx_desc_t			*pend_desc;
	efx_txq_t			*common;

	efsys_mem_t			*tsoh_buffer;

	char				lock_name[SFXGE_LOCK_NAME_MAX];

	/* This field changes more often and is read regularly on both
	 * the initiation and completion paths
	 */
	int				blocked __aligned(CACHE_LINE_SIZE);

	/* The following fields change more often, and are used mostly
	 * on the initiation path
	 */
	struct mtx			lock __aligned(CACHE_LINE_SIZE);
	struct sfxge_tx_dpl		dpl;	/* Deferred packet list. */
	unsigned int			n_pend_desc;
	unsigned int			added;
	unsigned int			reaped;

	/* The last (or constant) set of HW offloads requested on the queue */
	uint16_t			hw_cksum_flags;

	/* The last VLAN TCI seen on the queue if FW-assisted tagging is
	   used */
	uint16_t			hw_vlan_tci;

	/* Statistics */
	unsigned long			tso_bursts;
	unsigned long			tso_packets;
	unsigned long			tso_long_headers;
	unsigned long			collapses;
	unsigned long			drops;
	unsigned long			get_overflow;
	unsigned long			get_non_tcp_overflow;
	unsigned long			put_overflow;
	unsigned long			netdown_drops;
	unsigned long			tso_pdrop_too_many;
	unsigned long			tso_pdrop_no_rsrc;

	/* The following fields change more often, and are used mostly
	 * on the completion path
	 */
	unsigned int			pending __aligned(CACHE_LINE_SIZE);
	unsigned int			completed;
	struct sfxge_txq		*next;
};

struct sfxge_evq;

extern uint64_t sfxge_tx_get_drops(struct sfxge_softc *sc);

extern int sfxge_tx_init(struct sfxge_softc *sc);
extern void sfxge_tx_fini(struct sfxge_softc *sc);
extern int sfxge_tx_start(struct sfxge_softc *sc);
extern void sfxge_tx_stop(struct sfxge_softc *sc);
extern void sfxge_tx_qcomplete(struct sfxge_txq *txq, struct sfxge_evq *evq);
extern void sfxge_tx_qflush_done(struct sfxge_txq *txq);
extern void sfxge_if_qflush(struct ifnet *ifp);
extern int sfxge_if_transmit(struct ifnet *ifp, struct mbuf *m);

#endif
