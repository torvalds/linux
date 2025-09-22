/*	$OpenBSD: gemvar.h,v 1.33 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: gemvar.h,v 1.1 2001/09/16 00:11:43 eeh Exp $ */

/*
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * All rights reserved.
 *
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
 */

#ifndef	_IF_GEMVAR_H
#define	_IF_GEMVAR_H

#include <sys/queue.h>
#include <sys/timeout.h>

/*
 * Misc. definitions for the Sun ``Gem'' Ethernet controller family driver.
 */

/*
 * Transmit descriptor list size.  This is arbitrary, but allocate
 * enough descriptors for 64 pending transmissions and 16 segments
 * per packet.
 */
#define	GEM_NTXSEGS		16

#define	GEM_TXQUEUELEN		64
#define	GEM_NTXDESC		(GEM_TXQUEUELEN * GEM_NTXSEGS)
#define	GEM_NTXDESC_MASK	(GEM_NTXDESC - 1)
#define	GEM_NEXTTX(x)		((x + 1) & GEM_NTXDESC_MASK)

struct gem_sxd {
	struct mbuf *sd_mbuf;
	bus_dmamap_t sd_map;
};

/*
 * Receive descriptor list size.  We have one Rx buffer per incoming
 * packet, so this logic is a little simpler.
 */
#define	GEM_NRXDESC		128
#define	GEM_NRXDESC_MASK	(GEM_NRXDESC - 1)
#define	GEM_NEXTRX(x)		((x + 1) & GEM_NRXDESC_MASK)

/*
 * Control structures are DMA'd to the GEM chip.  We allocate them in
 * a single clump that maps to a single DMA segment to make several things
 * easier.
 */
struct gem_control_data {
	/*
	 * The transmit descriptors.
	 */
	struct gem_desc gcd_txdescs[GEM_NTXDESC];

	/*
	 * The receive descriptors.
	 */
	struct gem_desc gcd_rxdescs[GEM_NRXDESC];
};

#define	GEM_CDOFF(x)		offsetof(struct gem_control_data, x)
#define	GEM_CDTXOFF(x)		GEM_CDOFF(gcd_txdescs[(x)])
#define	GEM_CDRXOFF(x)		GEM_CDOFF(gcd_rxdescs[(x)])

/*
 * Software state for receive jobs.
 */
struct gem_rxsoft {
	struct mbuf *rxs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t rxs_dmamap;	/* our DMA map */
};


/*
 * Table which describes the transmit threshold mode.  We generally
 * start at index 0.  Whenever we get a transmit underrun, we increment
 * our index, falling back if we encounter the NULL terminator.
 */
struct gem_txthresh_tab {
	u_int32_t txth_opmode;		/* OPMODE bits */
	const char *txth_name;		/* name of mode */
};

/*
 * Some misc. statistics, useful for debugging.
 */
struct gem_stats {
	u_long		ts_tx_uf;	/* transmit underflow errors */
	u_long		ts_tx_to;	/* transmit jabber timeouts */
	u_long		ts_tx_ec;	/* excessive collision count */
	u_long		ts_tx_lc;	/* late collision count */
};

/*
 * Software state per device.
 */
struct gem_softc {
	struct device	sc_dev;		/* generic device information */
	struct arpcom	sc_arpcom;	/* ethernet common data */
	struct mii_data	sc_mii;		/* MII media control */
#define sc_media	sc_mii.mii_media/* shorthand */
	struct timeout	sc_tick_ch;	/* tick callout */
	void		*sc_ih;		/* interrupt handler */

	/* The following bus handles are to be provided by the bus front-end */
	bus_space_tag_t	sc_bustag;	/* bus tag */
	bus_dma_tag_t	sc_dmatag;	/* bus dma tag */
	bus_dmamap_t	sc_dmamap;	/* bus dma handle */
	bus_space_handle_t sc_h1;	/* bus space handle for bank 1 regs */
	bus_space_handle_t sc_h2;	/* bus space handle for bank 2 regs */
#if 0
	/* The following may be needed for SBus */
	bus_space_handle_t sc_seb;	/* HME Global registers */
	bus_space_handle_t sc_erx;	/* HME ERX registers */
	bus_space_handle_t sc_etx;	/* HME ETX registers */
	bus_space_handle_t sc_mac;	/* HME MAC registers */
	bus_space_handle_t sc_mif;	/* HME MIF registers */
#endif
	int		sc_burst;	/* DVMA burst size in effect */

	int		sc_mif_config;	/* Selected MII reg setting */

	int		sc_pci;		/* XXXXX -- PCI buses are LE. */
	u_int		sc_variant;	/* which GEM are we dealing with? */
#define GEM_UNKNOWN			0	/* don't know */
#define GEM_SUN_GEM			1	/* Sun GEM */
#define GEM_SUN_ERI			2	/* Sun ERI */
#define GEM_APPLE_GMAC			3	/* Apple GMAC */
#define GEM_APPLE_K2_GMAC		4	/* Apple K2 GMAC */

#define	GEM_IS_APPLE(sc)						\
	((sc)->sc_variant == GEM_APPLE_GMAC ||				\
	 (sc)->sc_variant == GEM_APPLE_K2_GMAC)

	u_int		sc_flags;	/* */
#define	GEM_GIGABIT		0x0001	/* has a gigabit PHY */


	struct gem_stats sc_stats;	/* debugging stats */

	/*
	 * Ring buffer DMA stuff.
	 */
	bus_dma_segment_t sc_cdseg;	/* control data memory */
	int		sc_cdnseg;	/* number of segments */
	bus_dmamap_t sc_cddmamap;	/* control data DMA map */
#define	sc_cddma	sc_cddmamap->dm_segs[0].ds_addr

	/*
	 * Software state for transmit and receive descriptors.
	 */
	struct gem_sxd sc_txd[GEM_NTXDESC];
	u_int32_t sc_tx_cnt, sc_tx_prod, sc_tx_cons;

	struct gem_rxsoft sc_rxsoft[GEM_NRXDESC];
	struct if_rxring sc_rx_ring;
	u_int32_t sc_rx_prod, sc_rx_cons;

	/*
	 * Control data structures.
	 */
	struct gem_control_data *sc_control_data;
#define	sc_txdescs	sc_control_data->gcd_txdescs
#define	sc_rxdescs	sc_control_data->gcd_rxdescs

	int			sc_txfree;		/* number of free Tx descriptors */
	int			sc_txnext;		/* next ready Tx descriptor */

	u_int32_t		sc_tdctl_ch;		/* conditional desc chaining */
	u_int32_t		sc_tdctl_er;		/* conditional desc end-of-ring */

	u_int32_t		sc_setup_fsls;	/* FS|LS on setup descriptor */

	int			sc_rxfifosize;

	u_int32_t		sc_rx_fifo_wr_ptr;
	u_int32_t		sc_rx_fifo_rd_ptr;
	struct timeout		sc_rx_watchdog;

	/* ========== */
	int			sc_inited;
	int			sc_debug;

	/* Special hardware hooks */
	void	(*sc_hwreset)(struct gem_softc *);
	void	(*sc_hwinit)(struct gem_softc *);
};

#define	GEM_DMA_READ(_sc, _a) \
	(((_sc)->sc_pci) ? lemtoh64(_a) : bemtoh64(_a))
#define	GEM_DMA_WRITE(_sc, _a, _v) \
	(((_sc)->sc_pci) ? htolem64((_a), (_v)) : htobem64((_a), (_v)))

/*
 * This macro determines if a change to media-related OPMODE bits requires
 * a chip reset.
 */
#define	GEM_MEDIA_NEEDSRESET(sc, newbits)				\
	(((sc)->sc_opmode & OPMODE_MEDIA_BITS) !=			\
	 ((newbits) & OPMODE_MEDIA_BITS))

#define	GEM_CDTXADDR(sc, x)	((sc)->sc_cddma + GEM_CDTXOFF((x)))
#define	GEM_CDRXADDR(sc, x)	((sc)->sc_cddma + GEM_CDRXOFF((x)))

#define	GEM_CDSPADDR(sc)	((sc)->sc_cddma + GEM_CDSPOFF)

#define	GEM_CDTXSYNC(sc, x, n, ops)					\
do {									\
	int __x, __n;							\
									\
	__x = (x);							\
	__n = (n);							\
									\
	/* If it will wrap around, sync to the end of the ring. */	\
	if ((__x + __n) > GEM_NTXDESC) {				\
		bus_dmamap_sync((sc)->sc_dmatag, (sc)->sc_cddmamap,	\
		    GEM_CDTXOFF(__x), sizeof(struct gem_desc) *		\
		    (GEM_NTXDESC - __x), (ops));			\
		__n -= (GEM_NTXDESC - __x);				\
		__x = 0;						\
	}								\
									\
	/* Now sync whatever is left. */				\
	bus_dmamap_sync((sc)->sc_dmatag, (sc)->sc_cddmamap,		\
	    GEM_CDTXOFF(__x), sizeof(struct gem_desc) * __n, (ops));	\
} while (0)

#define	GEM_CDRXSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmatag, (sc)->sc_cddmamap,		\
	    GEM_CDRXOFF((x)), sizeof(struct gem_desc), (ops))

#define	GEM_CDSPSYNC(sc, ops)						\
	bus_dmamap_sync((sc)->sc_dmatag, (sc)->sc_cddmamap,		\
	    GEM_CDSPOFF, GEM_SETUP_PACKET_LEN, (ops))

#define	GEM_INIT_RXDESC(sc, x)						\
do {									\
	struct gem_rxsoft *__rxs = &sc->sc_rxsoft[(x)];			\
	struct gem_desc *__rxd = &sc->sc_rxdescs[(x)];			\
	struct mbuf *__m = __rxs->rxs_mbuf;				\
									\
	GEM_DMA_WRITE((sc), &__rxd->gd_addr,				\
	    __rxs->rxs_dmamap->dm_segs[0].ds_addr);			\
	GEM_DMA_WRITE((sc), &__rxd->gd_flags,				\
		(((__m->m_ext.ext_size)<<GEM_RD_BUFSHIFT)		\
	    & GEM_RD_BUFSIZE) | GEM_RD_OWN);				\
	GEM_CDRXSYNC((sc), (x), BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE); \
} while (0)

#ifdef _KERNEL
int	gem_intr(void *);

int	gem_mediachange(struct ifnet *);
void	gem_mediastatus(struct ifnet *, struct ifmediareq *);

void	gem_config(struct gem_softc *);
void	gem_unconfig(struct gem_softc *);
void	gem_reset(struct gem_softc *);
int	gem_intr(void *);
#endif /* _KERNEL */

#endif
