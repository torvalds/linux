/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Nathan Whitehorn
 * Copyright (c) 2003 Peter Grehan
 * All rights reserved
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

/*
 * Number of transmit/receive DBDMA descriptors.
 * XXX allow override with a tuneable ?
 */
#define BM_MAX_DMA_COMMANDS	256
#define BM_NTXSEGS		16

#define BM_MAX_TX_PACKETS	100
#define BM_MAX_RX_PACKETS	100

/*
 * Mutex macros
 */
#define BM_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define BM_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)

/*
 * software state for transmit job mbufs (may be elements of mbuf chains)
 */
struct bm_txsoft {
	struct mbuf *txs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t txs_dmamap;	/* our DMA map */
	int txs_firstdesc;		/* first descriptor in packet */
	int txs_lastdesc;		/* last descriptor in packet */
	int txs_stopdesc;		/* the location of the closing STOP */
	
	int txs_ndescs;			/* number of descriptors */
	STAILQ_ENTRY(bm_txsoft) txs_q;
};

STAILQ_HEAD(bm_txsq, bm_txsoft);

/*
 * software state for receive jobs
 */
struct bm_rxsoft {
	struct mbuf *rxs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t rxs_dmamap;	/* our DMA map */

	int dbdma_slot;
	bus_dma_segment_t segment;
};

struct bm_softc {
	struct ifnet    	*sc_ifp;
	struct mtx		sc_mtx;	
	u_char			sc_enaddr[ETHER_ADDR_LEN];

	int			sc_streaming;
	int			sc_ifpflags;
	int			sc_duplex;
	int 			sc_wdog_timer;

	struct callout		sc_tick_ch;
	
	device_t		sc_dev;		/* back ptr to dev */
	struct resource		*sc_memr;	/* macio bus mem resource */
	int			sc_memrid;
	device_t		sc_miibus;

	struct mii_data		*sc_mii;

	struct resource		*sc_txdmar, *sc_rxdmar;
	int			sc_txdmarid, sc_rxdmarid;

	struct resource		*sc_txdmairq, *sc_rxdmairq;
	void			*sc_txihtx, *sc_rxih;
	int			sc_txdmairqid, sc_rxdmairqid;

	bus_dma_tag_t		sc_pdma_tag;

	bus_dma_tag_t		sc_tdma_tag;
	struct bm_txsoft	sc_txsoft[BM_MAX_TX_PACKETS];
	int			first_used_txdma_slot, next_txdma_slot;

	struct bm_txsq		sc_txfreeq;
	struct bm_txsq		sc_txdirtyq;

	bus_dma_tag_t		sc_rdma_tag;
	struct bm_rxsoft	sc_rxsoft[BM_MAX_TX_PACKETS];
	int			next_rxdma_slot, rxdma_loop_slot;

	dbdma_channel_t		*sc_txdma, *sc_rxdma;
};
