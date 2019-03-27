/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: gemvar.h,v 1.8 2002/05/15 02:36:12 matt Exp
 *	from: FreeBSD: if_gemvar.h 177560 2008-03-24 17:23:53Z marius
 *
 * $FreeBSD$
 */

#ifndef	_IF_CASVAR_H
#define	_IF_CASVAR_H

/*
 * The page size is configurable, but needs to be at least 8k (the
 * default) in order to also support jumbo buffers.
 */
#define	CAS_PAGE_SIZE		8192

/*
 * Transmit descriptor ring size - this is arbitrary, but allocate
 * enough descriptors for 64 pending transmissions and 16 segments
 * per packet.  This limit is not actually enforced (packets with
 * more segments can be sent, depending on the busdma backend); it
 * is however used as an estimate for the TX window size.
 */
#define	CAS_NTXSEGS		16

#define	CAS_TXQUEUELEN		64
#define	CAS_NTXDESC		(CAS_TXQUEUELEN * CAS_NTXSEGS)
#define	CAS_MAXTXFREE		(CAS_NTXDESC - 1)
#define	CAS_NTXDESC_MASK	(CAS_NTXDESC - 1)
#define	CAS_NEXTTX(x)		((x + 1) & CAS_NTXDESC_MASK)

/*
 * Receive completion ring size - we have one completion per
 * incoming packet (though the opposite isn't necessarily true),
 * so this logic is a little simpler.
 */
#define	CAS_NRXCOMP		4096
#define	CAS_NRXCOMP_MASK	(CAS_NRXCOMP - 1)
#define	CAS_NEXTRXCOMP(x)	((x + 1) & CAS_NRXCOMP_MASK)

/*
 * Receive descriptor ring sizes - for Cassini+ and Saturn both
 * rings must be at least initialized.
 */
#define	CAS_NRXDESC		1024
#define	CAS_NRXDESC_MASK	(CAS_NRXDESC - 1)
#define	CAS_NEXTRXDESC(x)	((x + 1) & CAS_NRXDESC_MASK)
#define	CAS_NRXDESC2		32
#define	CAS_NRXDESC2_MASK	(CAS_NRXDESC2 - 1)
#define	CAS_NEXTRXDESC2(x)	((x + 1) & CAS_NRXDESC2_MASK)

/*
 * How many ticks to wait until to retry on a RX descriptor that is
 * still owned by the hardware.
 */
#define	CAS_RXOWN_TICKS		(hz / 50)

/*
 * Control structures are DMA'd to the chip.  We allocate them
 * in a single clump that maps to a single DMA segment to make
 * several things easier.
 */
struct cas_control_data {
	struct cas_desc ccd_txdescs[CAS_NTXDESC];	/* TX descriptors */
	struct cas_rx_comp ccd_rxcomps[CAS_NRXCOMP];	/* RX completions */
	struct cas_desc ccd_rxdescs[CAS_NRXDESC];	/* RX descriptors */
	struct cas_desc ccd_rxdescs2[CAS_NRXDESC2];	/* RX descriptors 2 */
};

#define	CAS_CDOFF(x)		offsetof(struct cas_control_data, x)
#define	CAS_CDTXDOFF(x)		CAS_CDOFF(ccd_txdescs[(x)])
#define	CAS_CDRXCOFF(x)		CAS_CDOFF(ccd_rxcomps[(x)])
#define	CAS_CDRXDOFF(x)		CAS_CDOFF(ccd_rxdescs[(x)])
#define	CAS_CDRXD2OFF(x)	CAS_CDOFF(ccd_rxdescs2[(x)])

/*
 * software state for transmit job mbufs (may be elements of mbuf chains)
 */
struct cas_txsoft {
	struct mbuf *txs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t txs_dmamap;	/* our DMA map */
	u_int txs_firstdesc;		/* first descriptor in packet */
	u_int txs_lastdesc;		/* last descriptor in packet */
	u_int txs_ndescs;		/* number of descriptors */
	STAILQ_ENTRY(cas_txsoft) txs_q;
};

STAILQ_HEAD(cas_txsq, cas_txsoft);

/*
 * software state for receive descriptors
 */
struct cas_rxdsoft {
	void *rxds_buf;			/* receive buffer */
	bus_dmamap_t rxds_dmamap;	/* our DMA map */
	bus_addr_t rxds_paddr;		/* physical address of the segment */
	u_int rxds_refcount;		/* hardware + mbuf references */
};

/*
 * software state per device
 */
struct cas_softc {
	struct ifnet	*sc_ifp;
	struct mtx	sc_mtx;
	device_t	sc_miibus;
	struct mii_data	*sc_mii;	/* MII media control */
	device_t	sc_dev;		/* generic device information */
	u_char		sc_enaddr[ETHER_ADDR_LEN];
	struct callout	sc_tick_ch;	/* tick callout */
	struct callout	sc_rx_ch;	/* delayed RX callout */
	struct task	sc_intr_task;
	struct task	sc_tx_task;
	struct taskqueue	*sc_tq;
	u_int		sc_wdog_timer;	/* watchdog timer */

	void		*sc_ih;
	struct resource *sc_res[2];
#define	CAS_RES_INTR	0
#define	CAS_RES_MEM	1

	bus_dma_tag_t	sc_pdmatag;	/* parent bus DMA tag */
	bus_dma_tag_t	sc_rdmatag;	/* RX bus DMA tag */
	bus_dma_tag_t	sc_tdmatag;	/* TX bus DMA tag */
	bus_dma_tag_t	sc_cdmatag;	/* control data bus DMA tag */
	bus_dmamap_t	sc_dmamap;	/* bus DMA handle */

	u_int		sc_variant;
#define	CAS_UNKNOWN	0		/* don't know */
#define	CAS_CAS		1		/* Sun Cassini */
#define	CAS_CASPLUS	2		/* Sun Cassini+ */
#define	CAS_SATURN	3		/* National Semiconductor Saturn */

	u_int		sc_flags;
#define	CAS_INITED	(1 << 0)	/* reset persistent regs init'ed */
#define	CAS_NO_CSUM	(1 << 1)	/* don't use hardware checksumming */
#define	CAS_LINK	(1 << 2)	/* link is up */
#define	CAS_REG_PLUS	(1 << 3)	/* has Cassini+ registers */
#define	CAS_SERDES	(1 << 4)	/* use the SERDES */
#define	CAS_TABORT	(1 << 5)	/* has target abort issues */

	bus_dmamap_t	sc_cddmamap;	/* control data DMA map */
	bus_addr_t	sc_cddma;

	/*
	 * software state for transmit and receive descriptors
	 */
	struct cas_txsoft sc_txsoft[CAS_TXQUEUELEN];
	struct cas_rxdsoft sc_rxdsoft[CAS_NRXDESC];

	/*
	 * control data structures
	 */
	struct cas_control_data *sc_control_data;
#define	sc_txdescs	sc_control_data->ccd_txdescs
#define	sc_rxcomps	sc_control_data->ccd_rxcomps
#define	sc_rxdescs	sc_control_data->ccd_rxdescs
#define	sc_rxdescs2	sc_control_data->ccd_rxdescs2

	u_int		sc_txfree;	/* number of free TX descriptors */
	u_int		sc_txnext;	/* next ready TX descriptor */
	u_int		sc_txwin;	/* TX desc. since last TX intr. */

	struct cas_txsq	sc_txfreeq;	/* free software TX descriptors */
	struct cas_txsq	sc_txdirtyq;	/* dirty software TX descriptors */

	u_int		sc_rxcptr;	/* next ready RX completion */
	u_int		sc_rxdptr;	/* next ready RX descriptor */

	uint32_t	sc_mac_rxcfg;	/* RX MAC conf. % CAS_MAC_RX_CONF_EN */

	int		sc_ifflags;
};

#define	CAS_BARRIER(sc, offs, len, flags)				\
	bus_barrier((sc)->sc_res[CAS_RES_MEM], (offs), (len), (flags))

#define	CAS_READ_N(n, sc, offs)						\
	bus_read_ ## n((sc)->sc_res[CAS_RES_MEM], (offs))
#define	CAS_READ_1(sc, offs)		CAS_READ_N(1, (sc), (offs))
#define	CAS_READ_2(sc, offs)		CAS_READ_N(2, (sc), (offs))
#define	CAS_READ_4(sc, offs)		CAS_READ_N(4, (sc), (offs))

#define	CAS_WRITE_N(n, sc, offs, v)					\
	bus_write_ ## n((sc)->sc_res[CAS_RES_MEM], (offs), (v))
#define	CAS_WRITE_1(sc, offs, v)	CAS_WRITE_N(1, (sc), (offs), (v))
#define	CAS_WRITE_2(sc, offs, v)	CAS_WRITE_N(2, (sc), (offs), (v))
#define	CAS_WRITE_4(sc, offs, v)	CAS_WRITE_N(4, (sc), (offs), (v))

#define	CAS_CDTXDADDR(sc, x)	((sc)->sc_cddma + CAS_CDTXDOFF((x)))
#define	CAS_CDRXCADDR(sc, x)	((sc)->sc_cddma + CAS_CDRXCOFF((x)))
#define	CAS_CDRXDADDR(sc, x)	((sc)->sc_cddma + CAS_CDRXDOFF((x)))
#define	CAS_CDRXD2ADDR(sc, x)	((sc)->sc_cddma + CAS_CDRXD2OFF((x)))

#define	CAS_CDSYNC(sc, ops)						\
	bus_dmamap_sync((sc)->sc_cdmatag, (sc)->sc_cddmamap, (ops));

#define	__CAS_UPDATE_RXDESC(rxd, rxds, s)				\
do {									\
									\
	refcount_init(&(rxds)->rxds_refcount, 1);			\
	(rxd)->cd_buf_ptr = htole64((rxds)->rxds_paddr);		\
	KASSERT((s) < CAS_RD_BUF_INDEX_MASK >> CAS_RD_BUF_INDEX_SHFT,	\
	    ("%s: RX buffer index too large!", __func__));		\
	(rxd)->cd_flags =						\
	    htole64((uint64_t)((s) << CAS_RD_BUF_INDEX_SHFT));		\
} while (0)

#define	CAS_UPDATE_RXDESC(sc, d, s)					\
	__CAS_UPDATE_RXDESC(&(sc)->sc_rxdescs[(d)],			\
	    &(sc)->sc_rxdsoft[(s)], (s))

#define	CAS_INIT_RXDESC(sc, d, s)	CAS_UPDATE_RXDESC(sc, d, s)

#define	CAS_LOCK_INIT(_sc, _name)					\
	mtx_init(&(_sc)->sc_mtx, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	CAS_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	CAS_UNLOCK(_sc)			mtx_unlock(&(_sc)->sc_mtx)
#define	CAS_LOCK_ASSERT(_sc, _what)	mtx_assert(&(_sc)->sc_mtx, (_what))
#define	CAS_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->sc_mtx)
#define	CAS_LOCK_OWNED(_sc)		mtx_owned(&(_sc)->sc_mtx)

#endif
