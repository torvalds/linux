/* $OpenBSD: asc_tcds.c,v 1.10 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: asc_tcds.c,v 1.5 2001/11/15 09:48:19 lukem Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

/*
 * Copyright (c) 1994 Peter Galbavy.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>
#include <dev/tc/ascvar.h>

#include <machine/bus.h>

#include <dev/tc/tcvar.h>
#include <dev/tc/tcdsreg.h>
#include <dev/tc/tcdsvar.h>

struct asc_tcds_softc {
	struct asc_softc asc;

	struct tcds_slotconfig *sc_tcds;
};

int  asc_tcds_match (struct device *, void *, void *);
void asc_tcds_attach(struct device *, struct device *, void *);

/* Linkup to the rest of the kernel */
const struct cfattach asc_tcds_ca = {
	sizeof(struct asc_tcds_softc), asc_tcds_match, asc_tcds_attach
};

/*
 * Functions and the switch for the MI code.
 */
int	tcds_dma_isintr(struct ncr53c9x_softc *);
void	tcds_dma_reset(struct ncr53c9x_softc *);
int	tcds_dma_intr(struct ncr53c9x_softc *);
int	tcds_dma_setup(struct ncr53c9x_softc *, caddr_t *,
	    size_t *, int, size_t *);
void	tcds_dma_go(struct ncr53c9x_softc *);
void	tcds_dma_stop(struct ncr53c9x_softc *);
int	tcds_dma_isactive(struct ncr53c9x_softc *);
void	tcds_clear_latched_intr(struct ncr53c9x_softc *);

struct ncr53c9x_glue asc_tcds_glue = {
	asc_read_reg,
	asc_write_reg,
	tcds_dma_isintr,
	tcds_dma_reset,
	tcds_dma_intr,
	tcds_dma_setup,
	tcds_dma_go,
	tcds_dma_stop,
	tcds_dma_isactive,
	tcds_clear_latched_intr,
};

int
asc_tcds_match(struct device *parent, void *cf, void *aux)
{

	/* We always exist. */
	return 1;
}

#define DMAMAX(a)	(NBPG - ((a) & (NBPG - 1)))

/*
 * Attach this instance, and then all the sub-devices
 */
void
asc_tcds_attach(struct device *parent, struct device *self, void *aux)
{
	struct tcdsdev_attach_args *tcdsdev = aux;
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)self;
	struct ncr53c9x_softc *sc = &asc->asc.sc_ncr53c9x;
	int error;

	/*
	 * Set up glue for MI code early; we use some of it here.
	 */
	sc->sc_glue = &asc_tcds_glue;

	asc->asc.sc_bst = tcdsdev->tcdsda_bst;
	asc->asc.sc_bsh = tcdsdev->tcdsda_bsh;
	asc->sc_tcds = tcdsdev->tcdsda_sc;

	/*
	 * The TCDS ASIC cannot DMA across 8k boundaries, and this
	 * driver is written such that each DMA segment gets a new
	 * call to tcds_dma_setup().  Thus, the DMA map only needs
	 * to support 8k transfers.
	 */
	asc->asc.sc_dmat = tcdsdev->tcdsda_dmat;
	if ((error = bus_dmamap_create(asc->asc.sc_dmat, NBPG, 1, NBPG,
	    NBPG, BUS_DMA_NOWAIT, &asc->asc.sc_dmamap)) < 0) {
		printf("failed to create dma map, error = %d\n", error);
	}

	sc->sc_id = tcdsdev->tcdsda_id;
	sc->sc_freq = tcdsdev->tcdsda_freq;

	/* gimme MHz */
	sc->sc_freq /= 1000000;

	tcds_intr_establish(parent, tcdsdev->tcdsda_chip, ncr53c9x_intr, sc,
	    self->dv_xname);

	/*
	 * XXX More of this should be in ncr53c9x_attach(), but
	 * XXX should we really poke around the chip that much in
	 * XXX the MI code?  Think about this more...
	 */

	/*
	 * Set up static configuration info.
	 */
	sc->sc_cfg1 = sc->sc_id | NCRCFG1_PARENB;
	sc->sc_cfg2 = NCRCFG2_SCSI2;
	sc->sc_cfg3 = NCRCFG3_CDB;
	if (sc->sc_freq > 25)
		sc->sc_cfg3 |= NCRF9XCFG3_FCLK;
	sc->sc_rev = tcdsdev->tcdsda_variant;
	if (tcdsdev->tcdsda_fast) {
		sc->sc_features |= NCR_F_FASTSCSI;
		sc->sc_cfg3_fscsi = NCRF9XCFG3_FSCSI;
	}

	/*
	 * XXX minsync and maxxfer _should_ be set up in MI code,
	 * XXX but it appears to have some dependency on what sort
	 * XXX of DMA we're hooked up to, etc.
	 */

	/*
	 * This is the value used to start sync negotiations
	 * Note that the NCR register "SYNCTP" is programmed
	 * in "clocks per byte", and has a minimum value of 4.
	 * The SCSI period used in negotiation is one-fourth
	 * of the time (in nanoseconds) needed to transfer one byte.
	 * Since the chip's clock is given in MHz, we have the following
	 * formula: 4 * period = (1000 / freq) * 4
	 */
	sc->sc_minsync = (1000 / sc->sc_freq) * tcdsdev->tcdsda_period / 4;

	sc->sc_maxxfer = 64 * 1024;

	/* Do the common parts of attachment. */
	ncr53c9x_attach(sc);
}

void
tcds_dma_reset(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;

	/* TCDS SCSI disable/reset/enable. */
	tcds_scsi_reset(asc->sc_tcds);			/* XXX */

	if (asc->asc.sc_flags & ASC_MAPLOADED)
		bus_dmamap_unload(asc->asc.sc_dmat, asc->asc.sc_dmamap);
	asc->asc.sc_flags &= ~(ASC_DMAACTIVE|ASC_MAPLOADED);
}

/*
 * start a dma transfer or keep it going
 */
int
tcds_dma_setup(struct ncr53c9x_softc *sc, caddr_t *addr, size_t *len,
    int ispullup /* DMA into main memory */, size_t *dmasize)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;
	struct tcds_slotconfig *tcds = asc->sc_tcds;
	size_t size;
	u_int32_t dic;

	NCR_DMA(("tcds_dma %d: start %d@%p,%s\n", tcds->sc_slot,
		(int)*asc->asc.sc_dmalen, *asc->asc.sc_dmaaddr,
		(ispullup) ? "IN" : "OUT"));

	/*
	 * the rules say we cannot transfer more than the limit
	 * of this DMA chip (64k) and we cannot cross a 8k boundary.
	 */
	size = min(*dmasize, DMAMAX((size_t)*addr));
	asc->asc.sc_dmaaddr = addr;
	asc->asc.sc_dmalen = len;
	asc->asc.sc_flags = (ispullup) ? ASC_ISPULLUP : 0;
	*dmasize = asc->asc.sc_dmasize = size;

	NCR_DMA(("dma_start: dmasize = %d\n", (int)size));

	if (size == 0)
		return 0;

	if (bus_dmamap_load(asc->asc.sc_dmat, asc->asc.sc_dmamap, *addr, size,
	    NULL, BUS_DMA_NOWAIT | (ispullup ? BUS_DMA_READ : BUS_DMA_WRITE))) {
		/*
		 * XXX Should return an error, here, but the upper-layer
		 * XXX doesn't check the return value!
		 */
		panic("tcds_dma_setup: dmamap load failed");
	}

	/* synchronize dmamap contents with memory image */
	bus_dmamap_sync(asc->asc.sc_dmat, asc->asc.sc_dmamap, 0, size,
		(ispullup) ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	/* load address, set/clear unaligned transfer and read/write bits. */
	bus_space_write_4(tcds->sc_bst, tcds->sc_bsh, tcds->sc_sda,
	    asc->asc.sc_dmamap->dm_segs[0].ds_addr >> 2);
	dic = bus_space_read_4(tcds->sc_bst, tcds->sc_bsh, tcds->sc_dic);
	dic &= ~TCDS_DIC_ADDRMASK;
	dic |= asc->asc.sc_dmamap->dm_segs[0].ds_addr & TCDS_DIC_ADDRMASK;
	if (ispullup)
		dic |= TCDS_DIC_WRITE;
	else
		dic &= ~TCDS_DIC_WRITE;
	bus_space_write_4(tcds->sc_bst, tcds->sc_bsh, tcds->sc_dic, dic);

	asc->asc.sc_flags |= ASC_MAPLOADED;
	return 0;
}

void
tcds_dma_go(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;

	/* mark unit as DMA-active */
	asc->asc.sc_flags |= ASC_DMAACTIVE;

	/* start DMA */
	tcds_dma_enable(asc->sc_tcds, 1);
}

void
tcds_dma_stop(struct ncr53c9x_softc *sc)
{
#if 0
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;
#endif

	/*
	 * XXX STOP DMA HERE!
	 */
}

/*
 * Pseudo (chained) interrupt from the asc driver to kick the
 * current running DMA transfer. Called from ncr53c9x_intr()
 * for now.
 *
 * return 1 if it was a DMA continue.
 */
int
tcds_dma_intr(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;
	struct tcds_slotconfig *tcds = asc->sc_tcds;
	int trans, resid;
	u_int32_t tcl, tcm;
	u_int32_t dud, dudmask, *addr;
	bus_addr_t pa;

	NCR_DMA(("tcds_dma %d: intr", tcds->sc_slot));

	if (tcds_scsi_iserr(tcds))
		return 0;

	/* This is an "assertion" :) */
	if ((asc->asc.sc_flags & ASC_DMAACTIVE) == 0)
		panic("tcds_dma_intr: DMA wasn't active");

	/* DMA has stopped */
	tcds_dma_enable(tcds, 0);
	asc->asc.sc_flags &= ~ASC_DMAACTIVE;

	if (asc->asc.sc_dmasize == 0) {
		/* A "Transfer Pad" operation completed */
		tcl = NCR_READ_REG(sc, NCR_TCL);
		tcm = NCR_READ_REG(sc, NCR_TCM);
		NCR_DMA(("dma_intr: discarded %d bytes (tcl=%d, tcm=%d)\n",
		    tcl | (tcm << 8), tcl, tcm));
		return 0;
	}

	resid = 0;
	if ((asc->asc.sc_flags & ASC_ISPULLUP) == 0 &&
	    (resid = (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF)) != 0) {
		NCR_DMA(("dma_intr: empty esp FIFO of %d ", resid));
		DELAY(1);
	}

	resid += (tcl = NCR_READ_REG(sc, NCR_TCL));
	resid += (tcm = NCR_READ_REG(sc, NCR_TCM)) << 8;

	trans = asc->asc.sc_dmasize - resid;
	if (trans < 0) {			/* transferred < 0 ? */
		printf("tcds_dma %d: xfer (%d) > req (%d)\n",
		    tcds->sc_slot, trans, (int)asc->asc.sc_dmasize);
		trans = asc->asc.sc_dmasize;
	}

	NCR_DMA(("dma_intr: tcl=%d, tcm=%d; trans=%d, resid=%d\n",
	    tcl, tcm, trans, resid));

	*asc->asc.sc_dmalen -= trans;
	*asc->asc.sc_dmaaddr += trans;

	bus_dmamap_sync(asc->asc.sc_dmat, asc->asc.sc_dmamap,
			0, asc->asc.sc_dmamap->dm_mapsize,
			(asc->asc.sc_flags & ASC_ISPULLUP)
				? BUS_DMASYNC_POSTREAD
				: BUS_DMASYNC_POSTWRITE);

	/*
	 * Clean up unaligned DMAs into main memory.
	 */
	if (asc->asc.sc_flags & ASC_ISPULLUP) {
		/* Handle unaligned starting address, length. */
		dud = bus_space_read_4(tcds->sc_bst,
		    tcds->sc_bsh, tcds->sc_dud0);
		if ((dud & TCDS_DUD0_VALIDBITS) != 0) {
			addr = (u_int32_t *)
			    ((paddr_t)*asc->asc.sc_dmaaddr & ~0x3);
			dudmask = 0;
			if (dud & TCDS_DUD0_VALID00)
				panic("tcds_dma: dud0 byte 0 valid");
			if (dud & TCDS_DUD0_VALID01)
				dudmask |= TCDS_DUD_BYTE01;
			if (dud & TCDS_DUD0_VALID10)
				dudmask |= TCDS_DUD_BYTE10;
#ifdef DIAGNOSTIC
			if (dud & TCDS_DUD0_VALID11)
				dudmask |= TCDS_DUD_BYTE11;
#endif
			NCR_DMA(("dud0 at %p dudmask 0x%x\n",
			    addr, dudmask));
			*addr = (*addr & ~dudmask) | (dud & dudmask);
		}
		dud = bus_space_read_4(tcds->sc_bst,
		    tcds->sc_bsh, tcds->sc_dud1);
		if ((dud & TCDS_DUD1_VALIDBITS) != 0) {
			pa = bus_space_read_4(tcds->sc_bst, tcds->sc_bsh,
			    tcds->sc_sda) << 2;
			dudmask = 0;
			if (dud & TCDS_DUD1_VALID00)
				dudmask |= TCDS_DUD_BYTE00;
			if (dud & TCDS_DUD1_VALID01)
				dudmask |= TCDS_DUD_BYTE01;
			if (dud & TCDS_DUD1_VALID10)
				dudmask |= TCDS_DUD_BYTE10;
#ifdef DIAGNOSTIC
			if (dud & TCDS_DUD1_VALID11)
				panic("tcds_dma: dud1 byte 3 valid");
#endif
			NCR_DMA(("dud1 at 0x%lx dudmask 0x%x\n",
			    pa, dudmask));
			/* XXX Fix TC_PHYS_TO_UNCACHED() */
#if defined(__alpha__)
			addr = (u_int32_t *)ALPHA_PHYS_TO_K0SEG(pa);
#elif defined(__mips__)
			addr = (u_int32_t *)MIPS_PHYS_TO_KSEG1(pa);
#else
#error TURBOchannel only exists on DECs, folks...
#endif
			*addr = (*addr & ~dudmask) | (dud & dudmask);
		}
		/* XXX deal with saved residual byte? */
	}

	bus_dmamap_unload(asc->asc.sc_dmat, asc->asc.sc_dmamap);
	asc->asc.sc_flags &= ~ASC_MAPLOADED;

	return 0;
}

/*
 * Glue functions.
 */
int
tcds_dma_isintr(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;
	int x;

	x = tcds_scsi_isintr(asc->sc_tcds, 1);

	/* XXX */
	return x;
}

int
tcds_dma_isactive(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;

	return !!(asc->asc.sc_flags & ASC_DMAACTIVE);
}

void
tcds_clear_latched_intr(struct ncr53c9x_softc *sc)
{
	struct asc_tcds_softc *asc = (struct asc_tcds_softc *)sc;

	/* Clear the TCDS interrupt bit. */
	(void)tcds_scsi_isintr(asc->sc_tcds, 1);
}
