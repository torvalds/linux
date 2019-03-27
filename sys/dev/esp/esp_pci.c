/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2011 Marius Strobl <marius@FreeBSD.org>
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
 */

/*	$NetBSD: pcscp.c,v 1.45 2010/11/13 13:52:08 uebayasi Exp $	*/

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
 * esp_pci.c: device dependent code for AMD Am53c974 (PCscsi-PCI)
 * written by Izumi Tsutsui <tsutsui@NetBSD.org>
 *
 * Technical manual available at
 * http://www.amd.com/files/connectivitysolutions/networking/archivednetworking/19113.pdf
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/esp/ncr53c9xreg.h>
#include <dev/esp/ncr53c9xvar.h>

#include <dev/esp/am53c974reg.h>

#define	PCI_DEVICE_ID_AMD53C974	0x20201022

struct esp_pci_softc {
	struct ncr53c9x_softc	sc_ncr53c9x;	/* glue to MI code */
	device_t		sc_dev;

	struct resource *sc_res[2];
#define	ESP_PCI_RES_INTR	0
#define	ESP_PCI_RES_IO		1

	bus_dma_tag_t		sc_pdmat;

	bus_dma_tag_t		sc_xferdmat;	/* DMA tag for transfers */
	bus_dmamap_t		sc_xferdmam;	/* DMA map for transfers */

	void			*sc_ih;		/* interrupt handler */

	size_t			sc_dmasize;	/* DMA size */
	void			**sc_dmaaddr;	/* DMA address */
	size_t			*sc_dmalen;	/* DMA length */
	int			sc_active;	/* DMA state */
	int			sc_datain;	/* DMA Data Direction */
};

static struct resource_spec esp_pci_res_spec[] = {
	{ SYS_RES_IRQ, 0, RF_SHAREABLE | RF_ACTIVE },	/* ESP_PCI_RES_INTR */
	{ SYS_RES_IOPORT, PCIR_BAR(0), RF_ACTIVE },	/* ESP_PCI_RES_IO */
	{ -1, 0 }
};

#define	READ_DMAREG(sc, reg)						\
	bus_read_4((sc)->sc_res[ESP_PCI_RES_IO], (reg))
#define	WRITE_DMAREG(sc, reg, var)					\
	bus_write_4((sc)->sc_res[ESP_PCI_RES_IO], (reg), (var))

#define	READ_ESPREG(sc, reg)						\
	bus_read_1((sc)->sc_res[ESP_PCI_RES_IO], (reg) << 2)
#define	WRITE_ESPREG(sc, reg, val)					\
	bus_write_1((sc)->sc_res[ESP_PCI_RES_IO], (reg) << 2, (val))

static int	esp_pci_probe(device_t);
static int	esp_pci_attach(device_t);
static int	esp_pci_detach(device_t);
static int	esp_pci_suspend(device_t);
static int	esp_pci_resume(device_t);

static device_method_t esp_pci_methods[] = {
	DEVMETHOD(device_probe,		esp_pci_probe),
	DEVMETHOD(device_attach,	esp_pci_attach),
	DEVMETHOD(device_detach,	esp_pci_detach),
	DEVMETHOD(device_suspend,	esp_pci_suspend),
	DEVMETHOD(device_resume,	esp_pci_resume),

	DEVMETHOD_END
};

static driver_t esp_pci_driver = {
	"esp",
	esp_pci_methods,
	sizeof(struct esp_pci_softc)
};

DRIVER_MODULE(esp, pci, esp_pci_driver, esp_devclass, 0, 0);
MODULE_DEPEND(esp, pci, 1, 1, 1);

/*
 * Functions and the switch for the MI code
 */
static void	esp_pci_dma_go(struct ncr53c9x_softc *);
static int	esp_pci_dma_intr(struct ncr53c9x_softc *);
static int	esp_pci_dma_isactive(struct ncr53c9x_softc *);

static int	esp_pci_dma_isintr(struct ncr53c9x_softc *);
static void	esp_pci_dma_reset(struct ncr53c9x_softc *);
static int	esp_pci_dma_setup(struct ncr53c9x_softc *, void **, size_t *,
		    int, size_t *);
static void	esp_pci_dma_stop(struct ncr53c9x_softc *);
static void	esp_pci_write_reg(struct ncr53c9x_softc *, int, uint8_t);
static uint8_t	esp_pci_read_reg(struct ncr53c9x_softc *, int);
static void	esp_pci_xfermap(void *arg, bus_dma_segment_t *segs, int nseg,
		    int error);

static struct ncr53c9x_glue esp_pci_glue = {
	esp_pci_read_reg,
	esp_pci_write_reg,
	esp_pci_dma_isintr,
	esp_pci_dma_reset,
	esp_pci_dma_intr,
	esp_pci_dma_setup,
	esp_pci_dma_go,
	esp_pci_dma_stop,
	esp_pci_dma_isactive,
};

static int
esp_pci_probe(device_t dev)
{

	if (pci_get_devid(dev) == PCI_DEVICE_ID_AMD53C974) {
		device_set_desc(dev, "AMD Am53C974 Fast-SCSI");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

/*
 * Attach this instance, and then all the sub-devices
 */
static int
esp_pci_attach(device_t dev)
{
	struct esp_pci_softc *esc;
	struct ncr53c9x_softc *sc;
	int error;

	esc = device_get_softc(dev);
	sc = &esc->sc_ncr53c9x;

	NCR_LOCK_INIT(sc);

	esc->sc_dev = dev;
	sc->sc_glue = &esp_pci_glue;

	pci_enable_busmaster(dev);

	error = bus_alloc_resources(dev, esp_pci_res_spec, esc->sc_res);
	if (error != 0) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, esp_pci_res_spec, esc->sc_res);
		return (error);
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, BUS_SPACE_UNRESTRICTED,
	    BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL, &esc->sc_pdmat);
	if (error != 0) {
		device_printf(dev, "cannot create parent DMA tag\n");
		goto fail_res;
	}

	/*
	 * XXX More of this should be in ncr53c9x_attach(), but
	 * XXX should we really poke around the chip that much in
	 * XXX the MI code?  Think about this more...
	 */

	/*
	 * Set up static configuration info.
	 *
	 * XXX we should read the configuration from the EEPROM.
	 */
	sc->sc_id = 7;
	sc->sc_cfg1 = sc->sc_id | NCRCFG1_PARENB;
	sc->sc_cfg2 = NCRCFG2_SCSI2 | NCRCFG2_FE;
	sc->sc_cfg3 = NCRAMDCFG3_IDM | NCRAMDCFG3_FCLK;
	sc->sc_cfg4 = NCRAMDCFG4_GE12NS | NCRAMDCFG4_RADE;
	sc->sc_rev = NCR_VARIANT_AM53C974;
	sc->sc_features = NCR_F_FASTSCSI | NCR_F_DMASELECT;
	sc->sc_cfg3_fscsi = NCRAMDCFG3_FSCSI;
	sc->sc_freq = 40; /* MHz */

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

	sc->sc_maxxfer = DFLTPHYS;	/* see below */
	sc->sc_maxoffset = 15;
	sc->sc_extended_geom = 1;

#define	MDL_SEG_SIZE	0x1000	/* 4kbyte per segment */

	/*
	 * Create the DMA tag and map for the data transfers.
	 *
	 * Note: given that bus_dma(9) only adheres to the requested alignment
	 * for the first segment (and that also only for bus_dmamem_alloc()ed
	 * DMA maps) we can't use the Memory Descriptor List.  However, also
	 * when not using the MDL, the maximum transfer size apparently is
	 * limited to 4k so we have to split transfers up, which plain sucks.
	 */
	error = bus_dma_tag_create(esc->sc_pdmat, PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MDL_SEG_SIZE, 1, MDL_SEG_SIZE, BUS_DMA_ALLOCNOW,
	    busdma_lock_mutex, &sc->sc_lock, &esc->sc_xferdmat);
	if (error != 0) {
		device_printf(dev, "cannot create transfer DMA tag\n");
		goto fail_pdmat;
	}
	error = bus_dmamap_create(esc->sc_xferdmat, 0, &esc->sc_xferdmam);
	if (error != 0) {
		device_printf(dev, "cannot create transfer DMA map\n");
		goto fail_xferdmat;
	}

	error = bus_setup_intr(dev, esc->sc_res[ESP_PCI_RES_INTR],
	    INTR_MPSAFE | INTR_TYPE_CAM, NULL, ncr53c9x_intr, sc,
	    &esc->sc_ih);
	if (error != 0) {
		device_printf(dev, "cannot set up interrupt\n");
		goto fail_xferdmam;
	}

	/* Do the common parts of attachment. */
	sc->sc_dev = esc->sc_dev;
	error = ncr53c9x_attach(sc);
	if (error != 0) {
		device_printf(esc->sc_dev, "ncr53c9x_attach failed\n");
		goto fail_intr;
	}

	return (0);

 fail_intr:
	 bus_teardown_intr(esc->sc_dev, esc->sc_res[ESP_PCI_RES_INTR],
	    esc->sc_ih);
 fail_xferdmam:
	bus_dmamap_destroy(esc->sc_xferdmat, esc->sc_xferdmam);
 fail_xferdmat:
	bus_dma_tag_destroy(esc->sc_xferdmat);
 fail_pdmat:
	bus_dma_tag_destroy(esc->sc_pdmat);
 fail_res:
	bus_release_resources(dev, esp_pci_res_spec, esc->sc_res);
	NCR_LOCK_DESTROY(sc);

	return (error);
}

static int
esp_pci_detach(device_t dev)
{
	struct ncr53c9x_softc *sc;
	struct esp_pci_softc *esc;
	int error;

	esc = device_get_softc(dev);
	sc = &esc->sc_ncr53c9x;

	bus_teardown_intr(esc->sc_dev, esc->sc_res[ESP_PCI_RES_INTR],
	    esc->sc_ih);
	error = ncr53c9x_detach(sc);
	if (error != 0)
		return (error);
	bus_dmamap_destroy(esc->sc_xferdmat, esc->sc_xferdmam);
	bus_dma_tag_destroy(esc->sc_xferdmat);
	bus_dma_tag_destroy(esc->sc_pdmat);
	bus_release_resources(dev, esp_pci_res_spec, esc->sc_res);
	NCR_LOCK_DESTROY(sc);

	return (0);
}

static int
esp_pci_suspend(device_t dev)
{

	return (ENXIO);
}

static int
esp_pci_resume(device_t dev)
{

	return (ENXIO);
}

static void
esp_pci_xfermap(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)arg;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: bad transfer segment count %d", __func__,
	    nsegs));
	KASSERT(segs[0].ds_len <= MDL_SEG_SIZE,
	    ("%s: bad transfer segment length %ld", __func__,
	    (long)segs[0].ds_len));

	/* Program the DMA Starting Physical Address. */
	WRITE_DMAREG(esc, DMA_SPA, segs[0].ds_addr);
}

/*
 * Glue functions
 */

static uint8_t
esp_pci_read_reg(struct ncr53c9x_softc *sc, int reg)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	return (READ_ESPREG(esc, reg));
}

static void
esp_pci_write_reg(struct ncr53c9x_softc *sc, int reg, uint8_t v)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	WRITE_ESPREG(esc, reg, v);
}

static int
esp_pci_dma_isintr(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	return (READ_ESPREG(esc, NCR_STAT) & NCRSTAT_INT) != 0;
}

static void
esp_pci_dma_reset(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE);

	esc->sc_active = 0;
}

static int
esp_pci_dma_intr(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;
	bus_dma_tag_t xferdmat;
	bus_dmamap_t xferdmam;
	size_t dmasize;
	int datain, i, resid, trans;
	uint32_t dmastat;
	char *p = NULL;

	xferdmat = esc->sc_xferdmat;
	xferdmam = esc->sc_xferdmam;
	datain = esc->sc_datain;

	dmastat = READ_DMAREG(esc, DMA_STAT);

	if ((dmastat & DMASTAT_ERR) != 0) {
		/* XXX not tested... */
		WRITE_DMAREG(esc, DMA_CMD, DMACMD_ABORT | (datain != 0 ?
		    DMACMD_DIR : 0));

		device_printf(esc->sc_dev, "DMA error detected; Aborting.\n");
		bus_dmamap_sync(xferdmat, xferdmam, datain != 0 ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(xferdmat, xferdmam);
		return (-1);
	}

	if ((dmastat & DMASTAT_ABT) != 0) {
		/* XXX what should be done? */
		device_printf(esc->sc_dev, "DMA aborted.\n");
		WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | (datain != 0 ?
		    DMACMD_DIR : 0));
		esc->sc_active = 0;
		return (0);
	}

	KASSERT(esc->sc_active != 0, ("%s: DMA wasn't active", __func__));

	/* DMA has stopped. */

	esc->sc_active = 0;

	dmasize = esc->sc_dmasize;
	if (dmasize == 0) {
		/* A "Transfer Pad" operation completed. */
		NCR_DMA(("%s: discarded %d bytes (tcl=%d, tcm=%d)\n",
		    __func__, READ_ESPREG(esc, NCR_TCL) |
		    (READ_ESPREG(esc, NCR_TCM) << 8),
		    READ_ESPREG(esc, NCR_TCL), READ_ESPREG(esc, NCR_TCM)));
		return (0);
	}

	resid = 0;
	/*
	 * If a transfer onto the SCSI bus gets interrupted by the device
	 * (e.g. for a SAVEPOINTER message), the data in the FIFO counts
	 * as residual since the ESP counter registers get decremented as
	 * bytes are clocked into the FIFO.
	 */
	if (datain == 0 &&
	    (resid = (READ_ESPREG(esc, NCR_FFLAG) & NCRFIFO_FF)) != 0)
		NCR_DMA(("%s: empty esp FIFO of %d ", __func__, resid));

	if ((sc->sc_espstat & NCRSTAT_TC) == 0) {
		/*
		 * "Terminal count" is off, so read the residue
		 * out of the ESP counter registers.
		 */
		if (datain != 0) {
			resid = READ_ESPREG(esc, NCR_FFLAG) & NCRFIFO_FF;
			while (resid > 1)
				resid =
				    READ_ESPREG(esc, NCR_FFLAG) & NCRFIFO_FF;
			WRITE_DMAREG(esc, DMA_CMD, DMACMD_BLAST | DMACMD_DIR);

			for (i = 0; i < 0x8000; i++) /* XXX 0x8000 ? */
				if ((READ_DMAREG(esc, DMA_STAT) &
				    DMASTAT_BCMP) != 0)
					break;

			/* See the below comments... */
			if (resid != 0)
				p = *esc->sc_dmaaddr;
		}

		resid += READ_ESPREG(esc, NCR_TCL) |
		    (READ_ESPREG(esc, NCR_TCM) << 8) |
		    (READ_ESPREG(esc, NCR_TCH) << 16);
	} else
		while ((dmastat & DMASTAT_DONE) == 0)
			dmastat = READ_DMAREG(esc, DMA_STAT);

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | (datain != 0 ?
	    DMACMD_DIR : 0));

	/* Sync the transfer buffer. */
	bus_dmamap_sync(xferdmat, xferdmam, datain != 0 ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(xferdmat, xferdmam);

	trans = dmasize - resid;

	/*
	 * From the technical manual notes:
	 *
	 * "In some odd byte conditions, one residual byte will be left
	 *  in the SCSI FIFO, and the FIFO flags will never count to 0.
	 *  When this happens, the residual byte should be retrieved
	 *  via PIO following completion of the BLAST operation."
	 */
	if (p != NULL) {
		p += trans;
		*p = READ_ESPREG(esc, NCR_FIFO);
		trans++;
	}

	if (trans < 0) {			/* transferred < 0 ? */
#if 0
		/*
		 * This situation can happen in perfectly normal operation
		 * if the ESP is reselected while using DMA to select
		 * another target.  As such, don't print the warning.
		 */
		device_printf(dev, "xfer (%d) > req (%d)\n", trans, dmasize);
#endif
		trans = dmasize;
	}

	NCR_DMA(("%s: tcl=%d, tcm=%d, tch=%d; trans=%d, resid=%d\n", __func__,
	    READ_ESPREG(esc, NCR_TCL), READ_ESPREG(esc, NCR_TCM),
	    READ_ESPREG(esc, NCR_TCH), trans, resid));

	*esc->sc_dmalen -= trans;
	*esc->sc_dmaaddr = (char *)*esc->sc_dmaaddr + trans;

	return (0);
}

static int
esp_pci_dma_setup(struct ncr53c9x_softc *sc, void **addr, size_t *len,
    int datain, size_t *dmasize)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;
	int error;

	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | (datain != 0 ? DMACMD_DIR :
	    0));

	*dmasize = esc->sc_dmasize = ulmin(*dmasize, MDL_SEG_SIZE);
	esc->sc_dmaaddr = addr;
	esc->sc_dmalen = len;
	esc->sc_datain = datain;

	/*
	 * There's no need to set up DMA for a "Transfer Pad" operation.
	 */
	if (*dmasize == 0)
		return (0);

	/* Set the transfer length. */
	WRITE_DMAREG(esc, DMA_STC, *dmasize);

	/*
	 * Load the transfer buffer and program the DMA address.
	 * Note that the NCR53C9x core can't handle EINPROGRESS so we set
	 * BUS_DMA_NOWAIT.
	 */
	error = bus_dmamap_load(esc->sc_xferdmat, esc->sc_xferdmam,
	    *esc->sc_dmaaddr, *dmasize, esp_pci_xfermap, sc, BUS_DMA_NOWAIT);

	return (error);
}

static void
esp_pci_dma_go(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;
	int datain;

	datain = esc->sc_datain;

	/* No DMA transfer for a "Transfer Pad" operation */
	if (esc->sc_dmasize == 0)
		return;

	/* Sync the transfer buffer. */
	bus_dmamap_sync(esc->sc_xferdmat, esc->sc_xferdmam, datain != 0 ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	/* Set the DMA engine to the IDLE state. */
	/* XXX DMA Transfer Interrupt Enable bit is broken? */
	WRITE_DMAREG(esc, DMA_CMD, DMACMD_IDLE | /* DMACMD_INTE | */
	    (datain != 0 ? DMACMD_DIR : 0));

	/* Issue a DMA start command. */
	WRITE_DMAREG(esc, DMA_CMD, DMACMD_START | /* DMACMD_INTE | */
	    (datain != 0 ? DMACMD_DIR : 0));

	esc->sc_active = 1;
}

static void
esp_pci_dma_stop(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	/* DMA stop */
	/* XXX what should we do here ? */
	WRITE_DMAREG(esc, DMA_CMD,
	    DMACMD_ABORT | (esc->sc_datain != 0 ? DMACMD_DIR : 0));
	bus_dmamap_unload(esc->sc_xferdmat, esc->sc_xferdmam);

	esc->sc_active = 0;
}

static int
esp_pci_dma_isactive(struct ncr53c9x_softc *sc)
{
	struct esp_pci_softc *esc = (struct esp_pci_softc *)sc;

	/* XXX should we check esc->sc_active? */
	if ((READ_DMAREG(esc, DMA_CMD) & DMACMD_CMD) != DMACMD_IDLE)
		return (1);

	return (0);
}
