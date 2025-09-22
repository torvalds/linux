/*	$OpenBSD: smc83c170var.h,v 1.5 2022/01/09 05:42:42 jsg Exp $	*/
/*	$NetBSD: smc83c170var.h,v 1.9 2005/02/04 02:10:37 perry Exp $	*/

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

#ifndef _DEV_IC_SMC83C170VAR_H_
#define _DEV_IC_SMC83C170VAR_H_

#include <sys/timeout.h>

/*
 * Misc. definitions for the Standard Microsystems Corp. 83C170
 * Ethernet PCI Integrated Controller (EPIC/100) driver.
 */

/*
 * Transmit descriptor list size.
 */
#define	EPIC_NTXDESC		128
#define	EPIC_NTXDESC_MASK	(EPIC_NTXDESC - 1)
#define	EPIC_NEXTTX(x)		((x + 1) & EPIC_NTXDESC_MASK)

/*
 * Receive descriptor list size.
 */
#define	EPIC_NRXDESC		64
#define	EPIC_NRXDESC_MASK	(EPIC_NRXDESC - 1)
#define	EPIC_NEXTRX(x)		((x + 1) & EPIC_NRXDESC_MASK)

/*
 * Control structures are DMA'd to the EPIC chip.  We allocate them in
 * a single clump that maps to a single DMA segment to make several things
 * easier.
 */
struct epic_control_data {
	/*
	 * The transmit descriptors.
	 */
	struct epic_txdesc ecd_txdescs[EPIC_NTXDESC];

	/*
	 * The receive descriptors.
	 */
	struct epic_rxdesc ecd_rxdescs[EPIC_NRXDESC];

	/*
	 * The transmit fraglists.
	 */
	struct epic_fraglist ecd_txfrags[EPIC_NTXDESC];
};

#define	EPIC_CDOFF(x)	offsetof(struct epic_control_data, x)
#define	EPIC_CDTXOFF(x)	EPIC_CDOFF(ecd_txdescs[(x)])
#define	EPIC_CDRXOFF(x)	EPIC_CDOFF(ecd_rxdescs[(x)])
#define	EPIC_CDFLOFF(x)	EPIC_CDOFF(ecd_txfrags[(x)])

/*
 * Software state for transmit and receive descriptors.
 */
struct epic_descsoft {
	struct mbuf *ds_mbuf;		/* head of mbuf chain */
	bus_dmamap_t ds_dmamap;		/* our DMA map */
};

/*
 * Software state per device.
 */
struct epic_softc {
	struct device sc_dev;		/* generic device information */
	bus_space_tag_t sc_st;		/* bus space tag */
	bus_space_handle_t sc_sh;	/* bus space handle */
	bus_dma_tag_t sc_dmat;		/* bus DMA tag */
	struct arpcom sc_arpcom;	/* ethernet common data */

	int sc_hwflags;			/* info about board */
#define EPIC_HAS_BNC		0x01	/* BNC on serial interface */
#define EPIC_HAS_MII_FIBER	0x02	/* fiber on MII lxtphy */
#define EPIC_DUPLEXLED_ON_694	0x04	/* duplex LED by software */

	struct mii_data sc_mii;		/* MII/media information */
	struct timeout sc_mii_timeout;	/* MII timeout */

	bus_dmamap_t sc_cddmamap;	/* control data DMA map */
#define	sc_cddma	sc_cddmamap->dm_segs[0].ds_addr
	bus_dmamap_t sc_nulldmamap;	/* DMA map for the pad buffer */
#define sc_nulldma	sc_nulldmamap->dm_segs[0].ds_addr

	/*
	 * Software state for transmit and receive descriptors.
	 */
	struct epic_descsoft sc_txsoft[EPIC_NTXDESC];
	struct epic_descsoft sc_rxsoft[EPIC_NRXDESC];

	/*
	 * Control data structures.
	 */
	struct epic_control_data *sc_control_data;

	int	sc_txpending;		/* number of TX requests pending */
	int	sc_txdirty;		/* first dirty TX descriptor */
	int	sc_txlast;		/* last used TX descriptor */

	int	sc_rxptr;		/* next ready RX descriptor */

	uint64_t	sc_serinst;	/* ifmedia instance for serial mode */
};

#define	EPIC_CDTXADDR(sc, x)	((sc)->sc_cddma + EPIC_CDTXOFF((x)))
#define	EPIC_CDRXADDR(sc, x)	((sc)->sc_cddma + EPIC_CDRXOFF((x)))
#define	EPIC_CDFLADDR(sc, x)	((sc)->sc_cddma + EPIC_CDFLOFF((x)))

#define	EPIC_CDTX(sc, x)	(&(sc)->sc_control_data->ecd_txdescs[(x)])
#define	EPIC_CDRX(sc, x)	(&(sc)->sc_control_data->ecd_rxdescs[(x)])
#define	EPIC_CDFL(sc, x)	(&(sc)->sc_control_data->ecd_txfrags[(x)])

#define	EPIC_DSTX(sc, x)	(&(sc)->sc_txsoft[(x)])
#define	EPIC_DSRX(sc, x)	(&(sc)->sc_rxsoft[(x)])

#define	EPIC_CDTXSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    EPIC_CDTXOFF((x)), sizeof(struct epic_txdesc), (ops))

#define	EPIC_CDRXSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    EPIC_CDRXOFF((x)), sizeof(struct epic_rxdesc), (ops))

#define	EPIC_CDFLSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    EPIC_CDFLOFF((x)), sizeof(struct epic_fraglist), (ops))

#define	EPIC_INIT_RXDESC(sc, x)						\
do {									\
	struct epic_descsoft *__ds = EPIC_DSRX((sc), (x));		\
	struct epic_rxdesc *__rxd = EPIC_CDRX((sc), (x));		\
	struct mbuf *__m = __ds->ds_mbuf;				\
									\
	/*								\
	 * Note we scoot the packet forward 2 bytes in the buffer	\
	 * so that the payload after the Ethernet header is aligned	\
	 * to a 4 byte boundary.					\
	 */								\
	__m->m_data = __m->m_ext.ext_buf + 2;				\
	__rxd->er_bufaddr = __ds->ds_dmamap->dm_segs[0].ds_addr + 2;	\
	__rxd->er_control = RXCTL_BUFLENGTH(__m->m_ext.ext_size - 2);	\
	__rxd->er_rxstatus = ER_RXSTAT_OWNER;				\
	__rxd->er_nextdesc = EPIC_CDRXADDR((sc), EPIC_NEXTRX((x)));	\
	EPIC_CDRXSYNC((sc), (x), BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE); \
} while (/* CONSTCOND */ 0)

#ifdef _KERNEL
void	epic_attach(struct epic_softc *, const char *);
int	epic_intr(void *);
#endif /* _KERNEL */

#endif /* _DEV_IC_SMC83C170VAR_H_ */
