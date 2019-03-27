/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
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
 *
 * $FreeBSD$
 */

#ifndef	_IF_GEMVAR_H
#define	_IF_GEMVAR_H

#include <sys/queue.h>
#include <sys/callout.h>

/*
 * Transmit descriptor ring size - this is arbitrary, but allocate
 * enough descriptors for 64 pending transmissions and 16 segments
 * per packet.  This limit is not actually enforced (packets with
 * more segments can be sent, depending on the busdma backend); it
 * is however used as an estimate for the TX window size.
 */
#define	GEM_NTXSEGS		16

#define	GEM_TXQUEUELEN		64
#define	GEM_NTXDESC		(GEM_TXQUEUELEN * GEM_NTXSEGS)
#define	GEM_MAXTXFREE		(GEM_NTXDESC - 1)
#define	GEM_NTXDESC_MASK	(GEM_NTXDESC - 1)
#define	GEM_NEXTTX(x)		((x + 1) & GEM_NTXDESC_MASK)

/*
 * Receive descriptor ring size - we have one RX buffer per incoming
 * packet, so this logic is a little simpler.
 */
#define	GEM_NRXDESC		256
#define	GEM_NRXDESC_MASK	(GEM_NRXDESC - 1)
#define	GEM_NEXTRX(x)		((x + 1) & GEM_NRXDESC_MASK)

/*
 * How many ticks to wait until to retry on a RX descriptor that is
 * still owned by the hardware.
 */
#define	GEM_RXOWN_TICKS		(hz / 50)

/*
 * Control structures are DMA'd to the chip.  We allocate them
 * in a single clump that maps to a single DMA segment to make
 * several things easier.
 */
struct gem_control_data {
	struct gem_desc gcd_txdescs[GEM_NTXDESC];	/* TX descriptors */
	struct gem_desc gcd_rxdescs[GEM_NRXDESC];	/* RX descriptors */
};

#define	GEM_CDOFF(x)		offsetof(struct gem_control_data, x)
#define	GEM_CDTXOFF(x)		GEM_CDOFF(gcd_txdescs[(x)])
#define	GEM_CDRXOFF(x)		GEM_CDOFF(gcd_rxdescs[(x)])

/*
 * software state for transmit job mbufs (may be elements of mbuf chains)
 */
struct gem_txsoft {
	struct mbuf *txs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t txs_dmamap;	/* our DMA map */
	u_int txs_firstdesc;		/* first descriptor in packet */
	u_int txs_lastdesc;		/* last descriptor in packet */
	u_int txs_ndescs;		/* number of descriptors */
	STAILQ_ENTRY(gem_txsoft) txs_q;
};

STAILQ_HEAD(gem_txsq, gem_txsoft);

/*
 * software state for receive jobs
 */
struct gem_rxsoft {
	struct mbuf *rxs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t rxs_dmamap;	/* our DMA map */
	bus_addr_t rxs_paddr;		/* physical address of the segment */
};

/*
 * software state per device
 */
struct gem_softc {
	struct ifnet	*sc_ifp;
	struct mtx	sc_mtx;
	device_t	sc_miibus;
	struct mii_data	*sc_mii;	/* MII media control */
	device_t	sc_dev;		/* generic device information */
	u_char		sc_enaddr[ETHER_ADDR_LEN];
	struct callout	sc_tick_ch;	/* tick callout */
	struct callout	sc_rx_ch;	/* delayed RX callout */
	u_int		sc_wdog_timer;	/* watchdog timer */

	void		*sc_ih;
	struct resource *sc_res[3];
#define	GEM_RES_INTR		0
#define	GEM_RES_BANK1		1
#define	GEM_RES_BANK2		2

	bus_dma_tag_t	sc_pdmatag;	/* parent bus DMA tag */
	bus_dma_tag_t	sc_rdmatag;	/* RX bus DMA tag */
	bus_dma_tag_t	sc_tdmatag;	/* TX bus DMA tag */
	bus_dma_tag_t	sc_cdmatag;	/* control data bus DMA tag */
	bus_dmamap_t	sc_dmamap;	/* bus DMA handle */

	u_int		sc_variant;
#define	GEM_UNKNOWN		0	/* don't know */
#define	GEM_SUN_GEM		1	/* Sun GEM */
#define	GEM_SUN_ERI		2	/* Sun ERI */
#define	GEM_APPLE_GMAC		3	/* Apple GMAC */
#define	GEM_APPLE_K2_GMAC	4	/* Apple K2 GMAC */

#define	GEM_IS_APPLE(sc)						\
	((sc)->sc_variant == GEM_APPLE_GMAC ||				\
	(sc)->sc_variant == GEM_APPLE_K2_GMAC)

	u_int		sc_flags;
#define	GEM_INITED	(1 << 0)	/* reset persistent regs init'ed */
#define	GEM_LINK	(1 << 1)	/* link is up */
#define	GEM_PCI		(1 << 2)	/* PCI busses are little-endian */
#define	GEM_PCI66	(1 << 3)	/* PCI bus runs at 66MHz */
#define	GEM_SERDES	(1 << 4)	/* use the SERDES */

	/*
	 * ring buffer DMA stuff
	 */
	bus_dmamap_t	sc_cddmamap;	/* control data DMA map */
	bus_addr_t	sc_cddma;

	/*
	 * software state for transmit and receive descriptors
	 */
	struct gem_txsoft sc_txsoft[GEM_TXQUEUELEN];
	struct gem_rxsoft sc_rxsoft[GEM_NRXDESC];

	/*
	 * control data structures
	 */
	struct gem_control_data *sc_control_data;
#define	sc_txdescs	sc_control_data->gcd_txdescs
#define	sc_rxdescs	sc_control_data->gcd_rxdescs

	u_int		sc_txfree;	/* number of free TX descriptors */
	u_int		sc_txnext;	/* next ready TX descriptor */
	u_int		sc_txwin;	/* TX desc. since last TX intr. */

	struct gem_txsq	sc_txfreeq;	/* free TX descsofts */
	struct gem_txsq	sc_txdirtyq;	/* dirty TX descsofts */

	u_int		sc_rxptr;	/* next ready RX descriptor/state */
	u_int		sc_rxfifosize;	/* RX FIFO size (bytes) */

	uint32_t	sc_mac_rxcfg;	/* RX MAC conf. % GEM_MAC_RX_ENABLE */

	int		sc_ifflags;
	u_long		sc_csum_features;
};

#define	GEM_BANKN_BARRIER(n, sc, offs, len, flags)			\
	bus_barrier((sc)->sc_res[(n)], (offs), (len), (flags))
#define	GEM_BANK1_BARRIER(sc, offs, len, flags)				\
	GEM_BANKN_BARRIER(GEM_RES_BANK1, (sc), (offs), (len), (flags))
#define	GEM_BANK2_BARRIER(sc, offs, len, flags)				\
	GEM_BANKN_BARRIER(GEM_RES_BANK2, (sc), (offs), (len), (flags))

#define	GEM_BANKN_READ_M(n, m, sc, offs)				\
	bus_read_ ## m((sc)->sc_res[(n)], (offs))
#define	GEM_BANK1_READ_1(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK1, 1, (sc), (offs))
#define	GEM_BANK1_READ_2(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK1, 2, (sc), (offs))
#define	GEM_BANK1_READ_4(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK1, 4, (sc), (offs))
#define	GEM_BANK2_READ_1(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK2, 1, (sc), (offs))
#define	GEM_BANK2_READ_2(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK2, 2, (sc), (offs))
#define	GEM_BANK2_READ_4(sc, offs)					\
	GEM_BANKN_READ_M(GEM_RES_BANK2, 4, (sc), (offs))

#define	GEM_BANKN_WRITE_M(n, m, sc, offs, v)				\
	bus_write_ ## m((sc)->sc_res[n], (offs), (v))
#define	GEM_BANK1_WRITE_1(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK1, 1, (sc), (offs), (v))
#define	GEM_BANK1_WRITE_2(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK1, 2, (sc), (offs), (v))
#define	GEM_BANK1_WRITE_4(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK1, 4, (sc), (offs), (v))
#define	GEM_BANK2_WRITE_1(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK2, 1, (sc), (offs), (v))
#define	GEM_BANK2_WRITE_2(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK2, 2, (sc), (offs), (v))
#define	GEM_BANK2_WRITE_4(sc, offs, v)					\
	GEM_BANKN_WRITE_M(GEM_RES_BANK2, 4, (sc), (offs), (v))

/* XXX this should be handled by bus_dma(9). */
#define	GEM_DMA_READ(sc, v)						\
	((((sc)->sc_flags & GEM_PCI) != 0) ? le64toh(v) : be64toh(v))
#define	GEM_DMA_WRITE(sc, v)						\
	((((sc)->sc_flags & GEM_PCI) != 0) ? htole64(v) : htobe64(v))

#define	GEM_CDTXADDR(sc, x)	((sc)->sc_cddma + GEM_CDTXOFF((x)))
#define	GEM_CDRXADDR(sc, x)	((sc)->sc_cddma + GEM_CDRXOFF((x)))

#define	GEM_CDSYNC(sc, ops)						\
	bus_dmamap_sync((sc)->sc_cdmatag, (sc)->sc_cddmamap, (ops));

#define	GEM_INIT_RXDESC(sc, x)						\
do {									\
	struct gem_rxsoft *__rxs = &sc->sc_rxsoft[(x)];			\
	struct gem_desc *__rxd = &sc->sc_rxdescs[(x)];			\
	struct mbuf *__m = __rxs->rxs_mbuf;				\
									\
	__m->m_data = __m->m_ext.ext_buf;				\
	__rxd->gd_addr =						\
	    GEM_DMA_WRITE((sc), __rxs->rxs_paddr);			\
	__rxd->gd_flags = GEM_DMA_WRITE((sc),				\
	    (((__m->m_ext.ext_size) << GEM_RD_BUFSHIFT)	&		\
	    GEM_RD_BUFSIZE) | GEM_RD_OWN);				\
} while (0)

#define	GEM_UPDATE_RXDESC(sc, x)					\
do {									\
	struct gem_rxsoft *__rxs = &sc->sc_rxsoft[(x)];			\
	struct gem_desc *__rxd = &sc->sc_rxdescs[(x)];			\
	struct mbuf *__m = __rxs->rxs_mbuf;				\
									\
	__rxd->gd_flags = GEM_DMA_WRITE((sc),				\
	    (((__m->m_ext.ext_size) << GEM_RD_BUFSHIFT)	&		\
	    GEM_RD_BUFSIZE) | GEM_RD_OWN);				\
} while (0)

#define	GEM_LOCK_INIT(_sc, _name)					\
	mtx_init(&(_sc)->sc_mtx, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	GEM_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	GEM_UNLOCK(_sc)			mtx_unlock(&(_sc)->sc_mtx)
#define	GEM_LOCK_ASSERT(_sc, _what)	mtx_assert(&(_sc)->sc_mtx, (_what))
#define	GEM_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->sc_mtx)

#ifdef _KERNEL
extern devclass_t gem_devclass;

int	gem_attach(struct gem_softc *sc);
void	gem_detach(struct gem_softc *sc);
void	gem_intr(void *v);
void	gem_resume(struct gem_softc *sc);
void	gem_suspend(struct gem_softc *sc);

int	gem_mediachange(struct ifnet *ifp);
void	gem_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr);

/* MII methods & callbacks */
int	gem_mii_readreg(device_t dev, int phy, int reg);
void	gem_mii_statchg(device_t dev);
int	gem_mii_writereg(device_t dev, int phy, int reg, int val);

#endif /* _KERNEL */

#endif
