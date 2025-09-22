/*	$OpenBSD: hmevar.h,v 1.17 2014/11/27 14:53:42 brad Exp $	*/
/*	$NetBSD: hmevar.h,v 1.6 2000/09/28 10:56:57 tsutsui Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/timeout.h>

#define	HME_TX_RING_SIZE	64
#define	HME_RX_RING_SIZE	64
#define	HME_RX_RING_MAX		256
#define	HME_TX_RING_MAX		256
#define	HME_RX_PKTSIZE		1600
#define	HME_TX_NSEGS		16

struct hme_sxd {
	struct mbuf *sd_mbuf;		/* descriptor mbuf */
	bus_dmamap_t sd_map;		/* descriptor dmamap */
};

struct hme_ring {
	/* Ring Descriptors */
	caddr_t		rb_membase;	/* Packet buffer: CPU address */
	bus_addr_t	rb_dmabase;	/* Packet buffer: DMA address */
	caddr_t		rb_txd;		/* Transmit descriptors */
	bus_addr_t	rb_txddma;	/* DMA address of same */
	caddr_t		rb_rxd;		/* Receive descriptors */
	bus_addr_t	rb_rxddma;	/* DMA address of same */
};

struct hme_softc {
	struct device	sc_dev;		/* boilerplate device view */
	struct arpcom	sc_arpcom;	/* Ethernet common part */
	struct mii_data	sc_mii;		/* MII media control */
#define sc_media	sc_mii.mii_media/* shorthand */
	struct timeout	sc_tick_ch;	/* tick callout */

	/* The following bus handles are to be provided by the bus front-end */
	bus_space_tag_t	sc_bustag;	/* bus tag */
	bus_dma_tag_t	sc_dmatag;	/* bus dma tag */
	bus_dmamap_t	sc_dmamap;	/* bus dma handle */
	bus_space_handle_t sc_seb;	/* HME Global registers */
	bus_space_handle_t sc_erx;	/* HME ERX registers */
	bus_space_handle_t sc_etx;	/* HME ETX registers */
	bus_space_handle_t sc_mac;	/* HME MAC registers */
	bus_space_handle_t sc_mif;	/* HME MIF registers */
	int		sc_burst;	/* DVMA burst size in effect */
	int		sc_phys[2];	/* MII instance -> PHY map */

	int		sc_pci;		/* XXXXX -- PCI buses are LE. */

	/* Ring descriptor */
	struct hme_ring		sc_rb;

	int			sc_debug;

	struct hme_sxd sc_txd[HME_TX_RING_MAX], sc_rxd[HME_RX_RING_MAX];
	bus_dmamap_t	sc_rxmap_spare;
	int	sc_tx_cnt, sc_tx_prod, sc_tx_cons;
	struct if_rxring sc_rx_ring;
	int	sc_rx_prod, sc_rx_cons;
	u_int32_t sc_tcvr;
};


void	hme_config(struct hme_softc *);
void	hme_unconfig(struct hme_softc *);
void	hme_reset(struct hme_softc *);
int	hme_intr(void *);
