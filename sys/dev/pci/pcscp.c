/*	$OpenBSD: pcscp.c,v 1.22 2024/05/24 06:02:58 jsg Exp $	*/
/*	$NetBSD: pcscp.c,v 1.26 2003/10/19 10:25:42 tsutsui Exp $	*/

/*-
 * Copyright (c) 1997, 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center; Izumi Tsutsui.
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
 * pcscp.c: device dependent code for AMD Am53c974 (PCscsi-PCI)
 * written by Izumi Tsutsui <tsutsui@ceres.dti.ne.jp>
 *
 * Technical manual available at
 * http://www.amd.com/files/connectivitysolutions/networking/archivednetworking/19113.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>

#include <dev/pci/pcscpreg.h>

#define IO_MAP_REG	0x10

struct pcscp_softc {
	struct ncr53c9x_softc sc_ncr53c9x;	/* glue to MI code */

	bus_space_tag_t sc_st;		/* bus space tag */
	bus_space_handle_t sc_sh;	/* bus space handle */
	void *sc_ih;			/* interrupt cookie */

	bus_dma_tag_t sc_dmat;		/* DMA tag */

	bus_dmamap_t sc_xfermap;	/* DMA map for transfers */

	u_int32_t *sc_mdladdr;		/* MDL array */
	bus_dmamap_t sc_mdldmap;	/* MDL DMA map */

	int	sc_active;		/* DMA state */
	int	sc_datain;		/* DMA Data Direction */
	size_t	sc_dmasize;		/* DMA size */
	char	**sc_dmaaddr;		/* DMA address */
	size_t	*sc_dmalen;		/* DMA length */
};

#define	READ_DMAREG(sc, reg) \
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))
#define	WRITE_DMAREG(sc, reg, var) \
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (var))

#define	PCSCP_READ_REG(sc, reg) \
	bus_space_read_1((sc)->sc_st, (sc)->sc_sh, (reg) << 2)
#define	PCSCP_WRITE_REG(sc, reg, val) \
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, (reg) << 2, (val))

int	pcscp_match(struct device *, void *, void *); 
void	pcscp_attach(struct device *, struct device *, void *);  

const struct cfattach pcscp_ca = {
	sizeof(struct pcscp_softc), pcscp_match, pcscp_attach
};

struct cfdriver pcscp_cd = {
	NULL, "pcscp", DV_DULL
};

/*
 * Functions and the switch for the MI code.
 */

u_char	pcscp_read_reg(struct ncr53c9x_softc *, int);
void	pcscp_write_reg(struct ncr53c9x_softc *, int, u_char);
int	pcscp_dma_isintr(struct ncr53c9x_softc *);
void	pcscp_dma_reset(struct ncr53c9x_softc *);
int	pcscp_dma_intr(struct ncr53c9x_softc *);
int	pcscp_dma_setup(struct ncr53c9x_softc *, caddr_t *,
			       size_t *, int, size_t *);
void	pcscp_dma_go(struct ncr53c9x_softc *);
void	pcscp_dma_stop(struct ncr53c9x_softc *);
int	pcscp_dma_isactive(struct ncr53c9x_softc *);

struct ncr53c9x_glue pcscp_glue = {
	pcscp_read_reg,
	pcscp_write_reg,
	pcscp_dma_isintr,
	pcscp_dma_reset,
	pcscp_dma_intr,
	pcscp_dma_setup,
	pcscp_dma_go,
	pcscp_dma_stop,
	pcscp_dma_isactive,
	NULL,			/* gl_clear_latched_intr */
};

int
pcscp_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_AMD)
		return 0;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_AMD_PCSCSI_PCI:
		return 1;
	}
	return 0;
}

/*
 * Attach this instance, and then all the sub-devices
 */
void
pcscp_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct pcscp_softc *esc = (void *)self;
	struct ncr53c9x_softc *sc = &esc->sc_ncr53c9x;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	pci_intr_handle_t ih;
	const char *intrstr;
	bus_dma_segment_t seg;
	int error, rseg;

	if (pci_mapreg_map(pa, IO_MAP_REG, PCI_MAPREG_TYPE_IO, 0,
	     &iot, &ioh, NULL, NULL, 0)) {
		printf("%s: unable to map registers\n", sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_glue = &pcscp_glue;

	esc->sc_st = iot;
	esc->sc_sh = ioh;
	esc->sc_dmat = pa->pa_dmat;

	/*
	 * XXX More of this should be in ncr53c9x_attach(), but
	 * XXX should we really poke around the chip that much in
	 * XXX the MI code?  Think about this more...
	 */

	/*
	 * Set up static configuration info.
	 */

	/*
	 * XXX should read configuration from EEPROM?
	 *
	 * MI ncr53c9x driver does not support configuration
	 * per each target device, though...
	 */
	sc->sc_id = 7;
	sc->sc_cfg1 = sc->sc_id | NCRCFG1_PARENB;
	sc->sc_cfg2 = NCRCFG2_SCSI2 | NCRCFG2_FE;
	sc->sc_cfg3 = NCRAMDCFG3_IDM | NCRAMDCFG3_FCLK;
	sc->sc_cfg4 = NCRAMDCFG4_GE12NS | NCRAMDCFG4_RADE;
	sc->sc_rev = NCR_VARIANT_AM53C974;
	sc->sc_features = NCR_F_FASTSCSI;
	sc->sc_cfg3_fscsi = NCRAMDCFG3_FSCSI;
	sc->sc_freq = 40; /* MHz */

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

	sc->sc_minsync = 1000 / sc->sc_freq; 

	/* Really no limit, but since we want to fit into the TCR... */
	sc->sc_maxxfer = 16 * 1024 * 1024;

	/*
	 * Create the DMA maps for the data transfers.
         */

#define MDL_SEG_SIZE	0x1000 /* 4kbyte per segment */
#define MDL_SEG_OFFSET	0x0FFF
#define MDL_SIZE	(MAXPHYS / MDL_SEG_SIZE + 1) /* no hardware limit? */

	if (bus_dmamap_create(esc->sc_dmat, MAXPHYS, MDL_SIZE, MDL_SEG_SIZE,
	    MDL_SEG_SIZE, BUS_DMA_NOWAIT, &esc->sc_xfermap)) {
		printf("%s: can't create dma maps\n", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Allocate and map memory for the MDL.
	 */

	if ((error = bus_dmamem_alloc(esc->sc_dmat,
	    sizeof(u_int32_t) * MDL_SIZE, PAGE_SIZE, 0, &seg, 1, &rseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to allocate memory for the MDL, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_0;
	}
	if ((error = bus_dmamem_map(esc->sc_dmat, &seg, rseg,
	    sizeof(u_int32_t) * MDL_SIZE , (caddr_t *)&esc->sc_mdladdr,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map the MDL memory, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}
	if ((error = bus_dmamap_create(esc->sc_dmat, 
	    sizeof(u_int32_t) * MDL_SIZE, 1, sizeof(u_int32_t) * MDL_SIZE,
	    0, BUS_DMA_NOWAIT, &esc->sc_mdldmap)) != 0) {
		printf("%s: unable to map_create for the MDL, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_2;
	}
	if ((error = bus_dmamap_load(esc->sc_dmat, esc->sc_mdldmap,
	     esc->sc_mdladdr, sizeof(u_int32_t) * MDL_SIZE,
	     NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load for the MDL, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/* map and establish interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail_4;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	esc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
	    ncr53c9x_intr, esc, sc->sc_dev.dv_xname);
	if (esc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_4;
	}
	if (intrstr != NULL)
		printf(": %s\n", intrstr);

	/* Do the common parts of attachment. */
	printf("%s", sc->sc_dev.dv_xname);

	ncr53c9x_attach(sc);

	/* Turn on target selection using the `dma' method */
	sc->sc_features |= NCR_F_DMASELECT;

	return;

fail_4:
	bus_dmamap_unload(esc->sc_dmat, esc->sc_mdldmap);
fail_3:
	bus_dmamap_destroy(esc->sc_dmat, esc->sc_mdldmap);
fail_2:
	bus_dmamem_unmap(esc->sc_dmat, (caddr_t)esc->sc_mdldmap,
	    sizeof(uint32_t) * MDL_SIZE);
fail_1:
	bus_dmamem_free(esc->sc_dmat, &seg, rseg);
fail_0:
	bus_dmamap_destroy(esc->sc_dmat, esc->sc_xfermap);
}

/*
 * Glue functions.
 */

u_char
pcscp_read_reg(struct ncr53c9x_softc *sc, int reg)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	return PCSCP_READ_REG(esc, reg);
}

void
pcscp_write_reg(struct ncr53c9x_softc *sc, int reg, u_char v)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	PCSCP_WRITE_REG(esc, reg, v);
}

int
pcscp_dma_isintr(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	return (PCSCP_READ_REG(esc, NCR_STAT) & NCRSTAT_INT) != 0;
}

void
pcscp_dma_reset(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE);

	esc->sc_active = 0;
}

int
pcscp_dma_intr(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;
	int trans, resid, i;
	bus_dmamap_t dmap = esc->sc_xfermap;
	int datain = esc->sc_datain;
	u_int32_t dmastat;
	char *p = NULL;

	dmastat = READ_DMAREG(esc, DMA_STAT);

	if (dmastat & DMASTAT_ERR) {
		/* XXX not tested... */
		WRITE_DMAREG(esc, DMA_CMD,
		    DMACMD_ABORT | (datain ? DMACMD_DIR : 0));

		printf("%s: error: DMA error detected; Aborting.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamap_unload(esc->sc_dmat, dmap);
		return -1;
	}

	if (dmastat & DMASTAT_ABT) {
		/* XXX What should be done? */
		printf("%s: dma_intr: DMA aborted.\n", sc->sc_dev.dv_xname);
		WRITE_DMAREG(esc, DMA_CMD,
		    DMACMD_IDLE | (datain ? DMACMD_DIR : 0));
		esc->sc_active = 0;
		return 0;
	}

#ifdef DIAGNOSTIC
	/* This is an "assertion" :) */
	if (esc->sc_active == 0)
		panic("pcscp dmaintr: DMA wasn't active");
#endif

	/* DMA has stopped */

	esc->sc_active = 0;

	if (esc->sc_dmasize == 0) {
		/* A "Transfer Pad" operation completed */
		NCR_DMA(("dmaintr: discarded %d bytes (tcl=%d, tcm=%d)\n",
		    PCSCP_READ_REG(esc, NCR_TCL) |
		    (PCSCP_READ_REG(esc, NCR_TCM) << 8),
		    PCSCP_READ_REG(esc, NCR_TCL),
		    PCSCP_READ_REG(esc, NCR_TCM)));
		return 0;
	}

	resid = 0;
	/*
	 * If a transfer onto the SCSI bus gets interrupted by the device
	 * (e.g. for a SAVEPOINTER message), the data in the FIFO counts
	 * as residual since the ESP counter registers get decremented as
	 * bytes are clocked into the FIFO.
	 */
	if (!datain &&
	    (resid = (PCSCP_READ_REG(esc, NCR_FFLAG) & NCRFIFO_FF)) != 0) {
		NCR_DMA(("pcscp_dma_intr: empty esp FIFO of %d ", resid));
	}

	if ((sc->sc_espstat & NCRSTAT_TC) == 0) {
		/*
		 * `Terminal count' is off, so read the residue
		 * out of the ESP counter registers.
		 */
		if (datain) {
			resid = PCSCP_READ_REG(esc, NCR_FFLAG) & NCRFIFO_FF;
			while (resid > 1)
				resid =
				    PCSCP_READ_REG(esc, NCR_FFLAG) & NCRFIFO_FF;
			WRITE_DMAREG(esc, DMA_CMD, DMACMD_BLAST | DMACMD_MDL |
			    (datain ? DMACMD_DIR : 0));

			for (i = 0; i < 0x8000; i++) /* XXX 0x8000 ? */
				if (READ_DMAREG(esc, DMA_STAT) & DMASTAT_BCMP)
					break;

			/* See the below comments... */
			if (resid)
				p = *esc->sc_dmaaddr;
		}
		
		resid += PCSCP_READ_REG(esc, NCR_TCL) |
		    (PCSCP_READ_REG(esc, NCR_TCM) << 8) |
		    (PCSCP_READ_REG(esc, NCR_TCH) << 16);
	} else {
		while ((dmastat & DMASTAT_DONE) == 0)
			dmastat = READ_DMAREG(esc, DMA_STAT);
	}

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | (datain ? DMACMD_DIR : 0));

	/* sync MDL */
	bus_dmamap_sync(esc->sc_dmat, esc->sc_mdldmap,
	    0, sizeof(u_int32_t) * dmap->dm_nsegs, BUS_DMASYNC_POSTWRITE);
	/* sync transfer buffer */
	bus_dmamap_sync(esc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    datain ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(esc->sc_dmat, dmap);

	trans = esc->sc_dmasize - resid;

	/*
	 * From the technical manual notes:
	 *
	 * `In some odd byte conditions, one residual byte will be left
	 *  in the SCSI FIFO, and the FIFO flags will never count to 0.
	 *  When this happens, the residual byte should be retrieved
	 *  via PIO following completion of the BLAST operation.'
	 */
	
	if (p) {
		p += trans;
		*p = PCSCP_READ_REG(esc, NCR_FIFO);
		trans++;
	}

	if (trans < 0) {			/* transferred < 0 ? */
#if 0
		/*
		 * This situation can happen in perfectly normal operation
		 * if the ESP is reselected while using DMA to select
		 * another target.  As such, don't print the warning.
		 */
		printf("%s: xfer (%d) > req (%d)\n",
		    sc->sc_dev.dv_xname, trans, esc->sc_dmasize);
#endif
		trans = esc->sc_dmasize;
	}

	NCR_DMA(("dmaintr: tcl=%d, tcm=%d, tch=%d; trans=%d, resid=%d\n",
	    PCSCP_READ_REG(esc, NCR_TCL),
	    PCSCP_READ_REG(esc, NCR_TCM),
	    PCSCP_READ_REG(esc, NCR_TCH),
	    trans, resid));

	*esc->sc_dmalen -= trans;
	*esc->sc_dmaaddr += trans;

	return 0;
}

int
pcscp_dma_setup(struct ncr53c9x_softc *sc, caddr_t *addr, size_t *len,
    int datain, size_t *dmasize)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;
	bus_dmamap_t dmap = esc->sc_xfermap;
	u_int32_t *mdl;
	int error, nseg, seg;
	bus_addr_t s_offset, s_addr;

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | (datain ? DMACMD_DIR : 0));

	esc->sc_dmaaddr = addr;
	esc->sc_dmalen = len;
	esc->sc_dmasize = *dmasize;
	esc->sc_datain = datain;

#ifdef DIAGNOSTIC
	if ((*dmasize / MDL_SEG_SIZE) > MDL_SIZE)
		panic("pcscp: transfer size too large");
#endif

	/*
	 * No need to set up DMA in `Transfer Pad' operation.
	 * (case of *dmasize == 0)
	 */
	if (*dmasize == 0)
		return 0;

	error = bus_dmamap_load(esc->sc_dmat, dmap, *esc->sc_dmaaddr,
	    *esc->sc_dmalen, NULL,
	    ((sc->sc_nexus->xs->flags & SCSI_NOSLEEP) ?
	    BUS_DMA_NOWAIT : BUS_DMA_WAITOK) | BUS_DMA_STREAMING |
	    ((sc->sc_nexus->xs->flags & SCSI_DATA_IN) ?
	     BUS_DMA_READ : BUS_DMA_WRITE));
	if (error) {
		printf("%s: unable to load dmamap, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		return error;
	}

	/* set transfer length */
	WRITE_DMAREG(esc, DMA_STC, *dmasize); 

	/* set up MDL */
	mdl = esc->sc_mdladdr;
	nseg = dmap->dm_nsegs;

	/* the first segment is possibly not aligned with 4k MDL boundary */
	s_addr = dmap->dm_segs[0].ds_addr;
	s_offset = s_addr & MDL_SEG_OFFSET;
	s_addr -= s_offset;

	/* set the first MDL and offset */
	WRITE_DMAREG(esc, DMA_SPA, s_offset); 
	*mdl++ = htole32(s_addr);

	/* the rest dmamap segments are aligned with 4k boundary */
	for (seg = 1; seg < nseg; seg++)
		*mdl++ = htole32(dmap->dm_segs[seg].ds_addr);

	return 0;
}

void
pcscp_dma_go(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;
	bus_dmamap_t dmap = esc->sc_xfermap, mdldmap = esc->sc_mdldmap;
	int datain = esc->sc_datain;

	/* No DMA transfer in Transfer Pad operation */
	if (esc->sc_dmasize == 0)
		return;

	/* sync transfer buffer */
	bus_dmamap_sync(esc->sc_dmat, dmap, 0, dmap->dm_mapsize,
	    datain ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	/* sync MDL */
	bus_dmamap_sync(esc->sc_dmat, mdldmap,
	    0, sizeof(u_int32_t) * dmap->dm_nsegs, BUS_DMASYNC_PREWRITE); 

	/* set Starting MDL Address */
	WRITE_DMAREG(esc, DMA_SMDLA, mdldmap->dm_segs[0].ds_addr);

	/* set DMA command register bits */
	/* XXX DMA Transfer Interrupt Enable bit is broken? */
	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | DMACMD_MDL |
	    /* DMACMD_INTE | */
	    (datain ? DMACMD_DIR : 0));

	/* issue DMA start command */
	WRITE_DMAREG(esc, DMA_CMD, DMACMD_START | DMACMD_MDL |
	    /* DMACMD_INTE | */
	    (datain ? DMACMD_DIR : 0));

	esc->sc_active = 1;
}

void
pcscp_dma_stop(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	/* dma stop */
	/* XXX What should we do here ? */
	WRITE_DMAREG(esc, DMA_CMD,
	    DMACMD_ABORT | (esc->sc_datain ? DMACMD_DIR : 0));
	bus_dmamap_unload(esc->sc_dmat, esc->sc_xfermap);

	esc->sc_active = 0;
}

int
pcscp_dma_isactive(struct ncr53c9x_softc *sc)
{
	struct pcscp_softc *esc = (struct pcscp_softc *)sc;

	/* XXX should check esc->sc_active? */
	if ((READ_DMAREG(esc, DMA_CMD) & DMACMD_CMD) != DMACMD_IDLE)
		return 1;
	return 0;
}
