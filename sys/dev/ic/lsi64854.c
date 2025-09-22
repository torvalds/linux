/*	$OpenBSD: lsi64854.c,v 1.12 2022/10/16 01:22:39 jsg Exp $	*/
/*	$NetBSD: lsi64854.c,v 1.18 2001/06/04 20:56:51 mrg Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/cpu.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/lsi64854reg.h>
#include <dev/ic/lsi64854var.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>

void	lsi64854_reset(struct lsi64854_softc *);
int	lsi64854_setup(struct lsi64854_softc *, caddr_t *, size_t *,
			     int, size_t *);
int	lsi64854_setup_pp(struct lsi64854_softc *, caddr_t *, size_t *,
			     int, size_t *);
int	lsi64854_scsi_intr(void *);
int	lsi64854_pp_intr(void *);

#ifdef DEBUG
#define LDB_SCSI	1
#define LDB_ENET	2
#define LDB_PP		4
#define LDB_ANY		0xff
int lsi64854debug = 0;
#define DPRINTF(a,x) do { if (lsi64854debug & (a)) printf x ; } while (0)
#else
#define DPRINTF(a,x)
#endif

#define MAX_DMA_SZ	(16*1024*1024)

/*
 * Finish attaching this DMA device.
 * Front-end must fill in these fields:
 *	sc_bustag
 *	sc_dmatag
 *	sc_regs
 *	sc_burst
 *	sc_channel (one of SCSI, ENET, PP)
 *	sc_client (one of SCSI, ENET, PP `soft_c' pointers)
 */
int
lsi64854_attach(struct lsi64854_softc *sc)
{
	u_int32_t csr;
	int rc;

	/* Indirect functions */
	switch (sc->sc_channel) {
	case L64854_CHANNEL_SCSI:
		sc->intr = lsi64854_scsi_intr;
		sc->setup = lsi64854_setup;
		break;
	case L64854_CHANNEL_ENET:
		sc->intr = lsi64854_enet_intr;
		sc->setup = lsi64854_setup;
		break;
	case L64854_CHANNEL_PP:
		sc->intr = lsi64854_pp_intr;
		sc->setup = lsi64854_setup_pp;
		break;
	default:
		printf("%s: unknown channel\n", sc->sc_dev.dv_xname);
	}
	sc->reset = lsi64854_reset;

	/* Allocate a dmamap */
	if ((rc = bus_dmamap_create(sc->sc_dmatag, MAX_DMA_SZ, 1, MAX_DMA_SZ,
			      0, BUS_DMA_WAITOK, &sc->sc_dmamap)) != 0) {
		printf(": dma map create failed\n");
		return (rc);
	}

	printf(": dma rev ");
	csr = L64854_GCSR(sc);
	sc->sc_rev = csr & L64854_DEVID;
	switch (sc->sc_rev) {
	case DMAREV_0:
		printf("0");
		break;
	case DMAREV_ESC:
		printf("esc");
		break;
	case DMAREV_1:
		printf("1");
		break;
	case DMAREV_PLUS:
		printf("1+");
		break;
	case DMAREV_2:
		printf("2");
		break;
	case DMAREV_HME:
		printf("fas");
		break;
	default:
		printf("unknown (0x%x)", sc->sc_rev);
	}

	DPRINTF(LDB_ANY, (", burst 0x%x, csr 0x%x", sc->sc_burst, csr));
	printf("\n");

	return (0);
}

/*
 * DMAWAIT  waits while condition is true
 */
#define DMAWAIT(SC, COND, MSG, DONTPANIC) do if (COND) {		\
	int count = 500000;						\
	while ((COND) && --count > 0) DELAY(1);				\
	if (count == 0) {						\
		printf("%s: line %d: CSR = 0x%lx\n", __FILE__, __LINE__, \
			(u_long)L64854_GCSR(SC));			\
		if (DONTPANIC)						\
			printf(MSG);					\
		else							\
			panic(MSG);					\
	}								\
} while (0)

#define DMA_DRAIN(sc, dontpanic) do {					\
	u_int32_t csr;							\
	/*								\
	 * DMA rev0 & rev1: we are not allowed to touch the DMA "flush"	\
	 *     and "drain" bits while it is still thinking about a	\
	 *     request.							\
	 * other revs: D_ESC_R_PEND bit reads as 0			\
	 */								\
	DMAWAIT(sc, L64854_GCSR(sc) & D_ESC_R_PEND, "R_PEND", dontpanic);\
	if (sc->sc_rev != DMAREV_HME) {                                 \
	        /*							\
	         * Select drain bit based on revision			\
	         * also clears errors and D_TC flag			\
	         */							\
	        csr = L64854_GCSR(sc);					\
	        if (sc->sc_rev == DMAREV_1 || sc->sc_rev == DMAREV_0)	\
		        csr |= D_ESC_DRAIN;				\
	        else							\
		        csr |= L64854_INVALIDATE;			\
									\
	        L64854_SCSR(sc,csr);					\
	}								\
	/*								\
	 * Wait for draining to finish					\
	 *  rev0 & rev1 call this PACKCNT				\
	 */								\
	DMAWAIT(sc, L64854_GCSR(sc) & L64854_DRAINING, "DRAINING", dontpanic);\
} while(0)

#define DMA_FLUSH(sc, dontpanic) do {					\
	u_int32_t csr;							\
	/*								\
	 * DMA rev0 & rev1: we are not allowed to touch the DMA "flush"	\
	 *     and "drain" bits while it is still thinking about a	\
	 *     request.							\
	 * other revs: D_ESC_R_PEND bit reads as 0			\
	 */								\
	DMAWAIT(sc, L64854_GCSR(sc) & D_ESC_R_PEND, "R_PEND", dontpanic);\
	csr = L64854_GCSR(sc);					\
	csr &= ~(L64854_WRITE|L64854_EN_DMA); /* no-ops on ENET */	\
	csr |= L64854_INVALIDATE;	 	/* XXX FAS ? */		\
	L64854_SCSR(sc,csr);						\
} while(0)

void
lsi64854_reset(struct lsi64854_softc *sc)
{
	u_int32_t csr;

	DMA_FLUSH(sc, 1);
	csr = L64854_GCSR(sc);

	DPRINTF(LDB_ANY, ("lsi64854_reset: csr 0x%x\n", csr));

	/*
	 * XXX is sync needed?
	 */
	if (sc->sc_dmamap->dm_nsegs > 0)
		bus_dmamap_unload(sc->sc_dmatag, sc->sc_dmamap);

	if (sc->sc_rev == DMAREV_HME)
		L64854_SCSR(sc, csr | D_HW_RESET_FAS366);


	csr |= L64854_RESET;		/* reset DMA */
	L64854_SCSR(sc, csr);
	DELAY(200);			/* > 10 Sbus clocks(?) */

	/*DMAWAIT1(sc); why was this here? */
	csr = L64854_GCSR(sc);
	csr &= ~L64854_RESET;		/* de-assert reset line */
	L64854_SCSR(sc, csr);
	DELAY(5);			/* allow a few ticks to settle */

	csr = L64854_GCSR(sc);
	csr |= L64854_INT_EN;		/* enable interrupts */
	if (sc->sc_rev > DMAREV_1 && sc->sc_channel == L64854_CHANNEL_SCSI) {
		if (sc->sc_rev == DMAREV_HME)
			csr |= D_TWO_CYCLE;
		else
			csr |= D_FASTER;
	}

	/* Set burst */
	switch (sc->sc_rev) {
	case DMAREV_HME:
	case DMAREV_2:
		csr &= ~L64854_BURST_SIZE;
		if (sc->sc_burst == 32) {
			csr |= L64854_BURST_32;
		} else if (sc->sc_burst == 16) {
			csr |= L64854_BURST_16;
		} else {
			csr |= L64854_BURST_0;
		}
		break;
	case DMAREV_ESC:
		csr |= D_ESC_AUTODRAIN;	/* Auto-drain */
		if (sc->sc_burst == 32) {
			csr &= ~D_ESC_BURST;
		} else
			csr |= D_ESC_BURST;
		break;
	default:
		break;
	}
	L64854_SCSR(sc, csr);

	if (sc->sc_rev == DMAREV_HME) {
		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_ADDR, 0);
		sc->sc_dmactl = csr;
	}
	sc->sc_active = 0;

	DPRINTF(LDB_ANY, ("lsi64854_reset: done, csr 0x%x\n", csr));
}


#define DMAMAX(a)	(MAX_DMA_SZ - ((a) & (MAX_DMA_SZ-1)))
/*
 * setup a dma transfer
 */
int
lsi64854_setup(struct lsi64854_softc *sc, caddr_t *addr, size_t *len,
    int datain, size_t *dmasize)
{
	u_int32_t csr;

	DMA_FLUSH(sc, 0);

#if 0
	DMACSR(sc) &= ~D_INT_EN;
#endif
	sc->sc_dmaaddr = addr;
	sc->sc_dmalen = len;

	/*
	 * the rules say we cannot transfer more than the limit
	 * of this DMA chip (64k for old and 16Mb for new),
	 * and we cannot cross a 16Mb boundary.
	 */
	*dmasize = sc->sc_dmasize =
		min(*dmasize, DMAMAX((size_t) *sc->sc_dmaaddr));

	DPRINTF(LDB_ANY, ("dma_setup: dmasize = %ld\n", (long)sc->sc_dmasize));

	/*
	 * XXX what length? 
	 */
	if (sc->sc_rev == DMAREV_HME) {

		L64854_SCSR(sc, sc->sc_dmactl | L64854_RESET);
		L64854_SCSR(sc, sc->sc_dmactl);

		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_CNT, *dmasize);
	}

	/* Program the DMA address */
	if (sc->sc_dmasize) {
		sc->sc_dvmaaddr = *sc->sc_dmaaddr;
		if (bus_dmamap_load(sc->sc_dmatag, sc->sc_dmamap,
				*sc->sc_dmaaddr, sc->sc_dmasize,
				NULL /* kernel address */,   
		                BUS_DMA_NOWAIT | BUS_DMA_STREAMING))
			panic("%s: cannot allocate DVMA address",
			      sc->sc_dev.dv_xname);
		bus_dmamap_sync(sc->sc_dmatag, sc->sc_dmamap, 0, sc->sc_dmasize,
				datain
					? BUS_DMASYNC_PREREAD
					: BUS_DMASYNC_PREWRITE);
		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_ADDR,
				  sc->sc_dmamap->dm_segs[0].ds_addr);
	}

	if (sc->sc_rev == DMAREV_ESC) {
		/* DMA ESC chip bug work-around */
		long bcnt = sc->sc_dmasize;
		long eaddr = bcnt + (long)*sc->sc_dmaaddr;
		if ((eaddr & PGOFSET) != 0)
			bcnt = roundup(bcnt, PAGE_SIZE);
		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_CNT,
				  bcnt);
	}

	/* Setup DMA control register */
	csr = L64854_GCSR(sc);

	if (datain)
		csr |= L64854_WRITE;
	else
		csr &= ~L64854_WRITE;
	csr |= L64854_INT_EN;

	if (sc->sc_rev == DMAREV_HME) {
		csr |= (D_DSBL_SCSI_DRN | D_EN_DMA);
	}

	L64854_SCSR(sc, csr);

	return (0);
}

/*
 * Pseudo (chained) interrupt from the esp driver to kick the
 * current running DMA transfer. Called from ncr53c9x_intr()
 * for now.
 *
 * return 1 if it was a DMA continue.
 */
int
lsi64854_scsi_intr(void *arg)
{
	struct lsi64854_softc *sc = arg;
	struct ncr53c9x_softc *nsc = sc->sc_client;
	char bits[64];
	int trans, resid;
	u_int32_t csr;

	csr = L64854_GCSR(sc);

	DPRINTF(LDB_SCSI, ("%s: dmaintr: addr 0x%x, csr %b\n", sc->sc_dev.dv_xname,
	    bus_space_read_4(sc->sc_bustag, sc->sc_regs, L64854_REG_ADDR),
	    csr, DDMACSR_BITS));

	if (csr & (D_ERR_PEND|D_SLAVE_ERR)) {
		snprintf(bits, sizeof(bits), "%b", csr, DDMACSR_BITS);
		printf("%s: error: csr=%s\n", sc->sc_dev.dv_xname, bits);
		csr &= ~D_EN_DMA;	/* Stop DMA */
		/* Invalidate the queue; SLAVE_ERR bit is write-to-clear */
		csr |= D_INVALIDATE|D_SLAVE_ERR;
		L64854_SCSR(sc, csr);
		return (-1);
	}

	/* This is an "assertion" :) */
	if (sc->sc_active == 0)
		panic("dmaintr: DMA wasn't active");

	DMA_DRAIN(sc, 0);

	/* DMA has stopped */
	csr &= ~D_EN_DMA;
	L64854_SCSR(sc, csr);
	sc->sc_active = 0;

	if (sc->sc_dmasize == 0) {
		/* A "Transfer Pad" operation completed */
		DPRINTF(LDB_SCSI, ("dmaintr: discarded %d bytes (tcl=%d, tcm=%d)\n",
		        NCR_READ_REG(nsc, NCR_TCL) |
		                (NCR_READ_REG(nsc, NCR_TCM) << 8),
		        NCR_READ_REG(nsc, NCR_TCL),
		        NCR_READ_REG(nsc, NCR_TCM)));
		return 0;
	}

	resid = 0;
	/*
	 * If a transfer onto the SCSI bus gets interrupted by the device
	 * (e.g. for a SAVEPOINTER message), the data in the FIFO counts
	 * as residual since the NCR53C9X counter registers get decremented
	 * as bytes are clocked into the FIFO.
	 */
	if (!(csr & D_WRITE) &&
	    (resid = (NCR_READ_REG(nsc, NCR_FFLAG) & NCRFIFO_FF)) != 0) {
		DPRINTF(LDB_SCSI, ("dmaintr: empty esp FIFO of %d ", resid));
		if (nsc->sc_rev == NCR_VARIANT_FAS366 &&
		    (NCR_READ_REG(nsc, NCR_CFG3) & NCRFASCFG3_EWIDE))
			resid <<= 1;
	}

	if ((nsc->sc_espstat & NCRSTAT_TC) == 0) {
		/*
		 * `Terminal count' is off, so read the residue
		 * out of the NCR53C9X counter registers.
		 */
		resid += (NCR_READ_REG(nsc, NCR_TCL) |
			  (NCR_READ_REG(nsc, NCR_TCM) << 8) |
			   ((nsc->sc_cfg2 & NCRCFG2_FE)
				? (NCR_READ_REG(nsc, NCR_TCH) << 16)
				: 0));

		if (resid == 0 && sc->sc_dmasize == 65536 &&
		    (nsc->sc_cfg2 & NCRCFG2_FE) == 0)
			/* A transfer of 64K is encoded as `TCL=TCM=0' */
			resid = 65536;
	}

	trans = sc->sc_dmasize - resid;
	if (trans < 0) {			/* transferred < 0 ? */
#if 0
		/*
		 * This situation can happen in perfectly normal operation
		 * if the ESP is reselected while using DMA to select
		 * another target.  As such, don't print the warning.
		 */
		printf("%s: xfer (%d) > req (%d)\n",
		    sc->sc_dev.dv_xname, trans, sc->sc_dmasize);
#endif
		trans = sc->sc_dmasize;
	}

	DPRINTF(LDB_SCSI, ("dmaintr: tcl=%d, tcm=%d, tch=%d; trans=%d, resid=%d\n",
		NCR_READ_REG(nsc, NCR_TCL),
		NCR_READ_REG(nsc, NCR_TCM),
		(nsc->sc_cfg2 & NCRCFG2_FE)
			? NCR_READ_REG(nsc, NCR_TCH) : 0,
		trans, resid));

	if (sc->sc_dmamap->dm_nsegs > 0) {
		bus_dmamap_sync(sc->sc_dmatag, sc->sc_dmamap, 0, sc->sc_dmasize,
				(csr & D_WRITE) != 0
					? BUS_DMASYNC_POSTREAD
					: BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmatag, sc->sc_dmamap);
	}

	*sc->sc_dmalen -= trans;
	*sc->sc_dmaaddr += trans;

#if 0	/* this is not normal operation just yet */
	if (*sc->sc_dmalen == 0 ||
	    nsc->sc_phase != nsc->sc_prevphase)
		return 0;

	/* and again */
	dma_start(sc, sc->sc_dmaaddr, sc->sc_dmalen, DMACSR(sc) & D_WRITE);
	return 1;
#endif
	return 0;
}

/*
 * Pseudo (chained) interrupt to le driver to handle DMA errors.
 */
int
lsi64854_enet_intr(void *arg)
{
	struct lsi64854_softc *sc = arg;
	char bits[64];
	u_int32_t csr;
	static int dodrain = 0;
	int rv;

	csr = L64854_GCSR(sc);

	/* If the DMA logic shows an interrupt, claim it */
	rv = ((csr & E_INT_PEND) != 0) ? 1 : 0;

	if (csr & (E_ERR_PEND|E_SLAVE_ERR)) {
		snprintf(bits, sizeof(bits), "%b", csr, EDMACSR_BITS);
		printf("%s: error: csr=%s\n", sc->sc_dev.dv_xname, bits);
		csr &= ~L64854_EN_DMA;	/* Stop DMA */
		/* Invalidate the queue; SLAVE_ERR bit is write-to-clear */
		csr |= E_INVALIDATE|E_SLAVE_ERR;
		L64854_SCSR(sc, csr);
		DMA_RESET(sc);
		dodrain = 1;
		return (1);
	}

	if (dodrain) {	/* XXX - is this necessary with D_DSBL_WRINVAL on? */
		int i = 10;
		csr |= E_DRAIN;
		L64854_SCSR(sc, csr);
		while (i-- > 0 && (L64854_GCSR(sc) & D_DRAINING))
			delay(1);
	}

	return (rv | (*sc->sc_intrchain)(sc->sc_intrchainarg));
}

/*
 * setup a dma transfer
 */
int
lsi64854_setup_pp(struct lsi64854_softc *sc, caddr_t *addr, size_t *len,
    int datain, size_t *dmasize)
{
	u_int32_t csr;

	DMA_FLUSH(sc, 0);

	sc->sc_dmaaddr = addr;
	sc->sc_dmalen = len;

	DPRINTF(LDB_PP, ("%s: pp start %ld@%p,%d\n", sc->sc_dev.dv_xname,
		(long)*sc->sc_dmalen, *sc->sc_dmaaddr, datain ? 1 : 0));

	/*
	 * the rules say we cannot transfer more than the limit
	 * of this DMA chip (64k for old and 16Mb for new),
	 * and we cannot cross a 16Mb boundary.
	 */
	*dmasize = sc->sc_dmasize =
		min(*dmasize, DMAMAX((size_t) *sc->sc_dmaaddr));

	DPRINTF(LDB_PP, ("dma_setup_pp: dmasize = %ld\n", (long)sc->sc_dmasize));

	/* Program the DMA address */
	if (sc->sc_dmasize) {
		sc->sc_dvmaaddr = *sc->sc_dmaaddr;
		if (bus_dmamap_load(sc->sc_dmatag, sc->sc_dmamap,
				*sc->sc_dmaaddr, sc->sc_dmasize,
				NULL /* kernel address */,   
				    BUS_DMA_NOWAIT/*|BUS_DMA_COHERENT*/))
			panic("%s: pp cannot allocate DVMA address",
			      sc->sc_dev.dv_xname);
		bus_dmamap_sync(sc->sc_dmatag, sc->sc_dmamap, 0, sc->sc_dmasize,
				datain
					? BUS_DMASYNC_PREREAD
					: BUS_DMASYNC_PREWRITE);
		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_ADDR,
				  sc->sc_dmamap->dm_segs[0].ds_addr);

		bus_space_write_4(sc->sc_bustag, sc->sc_regs, L64854_REG_CNT,
				  sc->sc_dmasize);
	}

	/* Setup DMA control register */
	csr = L64854_GCSR(sc);
	csr &= ~L64854_BURST_SIZE;
	if (sc->sc_burst == 32) {
		csr |= L64854_BURST_32;
	} else if (sc->sc_burst == 16) {
		csr |= L64854_BURST_16;
	} else {
		csr |= L64854_BURST_0;
	}
	csr |= P_EN_DMA|P_INT_EN|P_EN_CNT;
#if 0
	/* This bit is read-only in PP csr register */
	if (datain)
		csr |= P_WRITE;
	else
		csr &= ~P_WRITE;
#endif
	L64854_SCSR(sc, csr);

	return (0);
}
/*
 * Parallel port DMA interrupt.
 */
int
lsi64854_pp_intr(void *arg)
{
	struct lsi64854_softc *sc = arg;
	int ret, trans, resid = 0;
	u_int32_t csr;

	csr = L64854_GCSR(sc);

	DPRINTF(LDB_PP, ("%s: pp intr: addr 0x%x, csr %b\n", sc->sc_dev.dv_xname,
		 bus_space_read_4(sc->sc_bustag, sc->sc_regs, L64854_REG_ADDR),
		 csr, PDMACSR_BITS));

	if (csr & (P_ERR_PEND|P_SLAVE_ERR)) {
		resid = bus_space_read_4(sc->sc_bustag, sc->sc_regs,
					 L64854_REG_CNT);
		printf("%s: pp error: resid %d csr=%b\n", sc->sc_dev.dv_xname,
		       resid, csr, PDMACSR_BITS);
		csr &= ~P_EN_DMA;	/* Stop DMA */
		/* Invalidate the queue; SLAVE_ERR bit is write-to-clear */
		csr |= P_INVALIDATE|P_SLAVE_ERR;
		L64854_SCSR(sc, csr);
		return (1);
	}

	ret = (csr & P_INT_PEND) != 0;

	if (sc->sc_active != 0) {
		DMA_DRAIN(sc, 0);
		resid = bus_space_read_4(sc->sc_bustag, sc->sc_regs,
					 L64854_REG_CNT);
	}

	/* DMA has stopped */
	csr &= ~D_EN_DMA;
	L64854_SCSR(sc, csr);
	sc->sc_active = 0;

	trans = sc->sc_dmasize - resid;
	if (trans < 0) {			/* transferred < 0 ? */
		trans = sc->sc_dmasize;
	}
	*sc->sc_dmalen -= trans;
	*sc->sc_dmaaddr += trans;

	if (sc->sc_dmamap->dm_nsegs > 0) {
		bus_dmamap_sync(sc->sc_dmatag, sc->sc_dmamap, 0, sc->sc_dmasize,
				(csr & D_WRITE) != 0
					? BUS_DMASYNC_POSTREAD
					: BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmatag, sc->sc_dmamap);
	}

	return (ret != 0);
}
