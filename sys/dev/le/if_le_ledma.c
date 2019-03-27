/*	$NetBSD: if_le_ledma.c,v 1.26 2005/12/11 12:23:44 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum; Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center; Paul Kranenburg.
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
#include <sys/socket.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <sparc64/sbus/lsi64854reg.h>
#include <sparc64/sbus/lsi64854var.h>

#include <dev/le/lancereg.h>
#include <dev/le/lancevar.h>
#include <dev/le/am7990var.h>

#define	LEDMA_ALIGNMENT	8		/* ring desc. alignmet for NCR92C990 */
#define	LEDMA_BOUNDARY	(16*1024*1024)	/* must not cross 16MB boundary */
#define	LEDMA_MEMSIZE	(16*1024)	/* LANCE memory size */
#define	LEREG1_RDP	0		/* Register Data Port */
#define	LEREG1_RAP	2		/* Register Address Port */

struct le_dma_softc {
	struct am7990_softc	sc_am7990;	/* glue to MI code */

	struct resource		*sc_rres;

	struct resource		*sc_ires;
	void			*sc_ih;

	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmam;
	bus_addr_t		sc_laddr;	/* LANCE DMA address */

	struct lsi64854_softc	*sc_dma;	/* pointer to DMA engine */
};

static device_probe_t le_dma_probe;
static device_attach_t le_dma_attach;
static device_detach_t le_dma_detach;
static device_resume_t le_dma_resume;
static device_suspend_t le_dma_suspend;

static device_method_t le_dma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		le_dma_probe),
	DEVMETHOD(device_attach,	le_dma_attach),
	DEVMETHOD(device_detach,	le_dma_detach),
	/* We can just use the suspend method here. */
	DEVMETHOD(device_shutdown,	le_dma_suspend),
	DEVMETHOD(device_suspend,	le_dma_suspend),
	DEVMETHOD(device_resume,	le_dma_resume),

	{ 0, 0 }
};

DEFINE_CLASS_0(le, le_dma_driver, le_dma_methods, sizeof(struct le_dma_softc));
DRIVER_MODULE(le, dma, le_dma_driver, le_devclass, 0, 0);
MODULE_DEPEND(le, dma, 1, 1, 1);
MODULE_DEPEND(le, ether, 1, 1, 1);

/*
 * Media types supported
 */
static const int le_dma_supmedia[] = {
	IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_T, 0, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_5, 0, 0)
};

static void le_dma_wrcsr(struct lance_softc *, uint16_t, uint16_t);
static uint16_t le_dma_rdcsr(struct lance_softc *, uint16_t);
static void le_dma_setutp(struct lance_softc *);
static void le_dma_setaui(struct lance_softc *);
static int le_dma_supmediachange(struct lance_softc *);
static void le_dma_supmediastatus(struct lance_softc *, struct ifmediareq *);
static void le_dma_hwreset(struct lance_softc *);
static int le_dma_hwintr(struct lance_softc *);
static void le_dma_nocarrier(struct lance_softc *);
static bus_dmamap_callback_t le_dma_dma_callback;

static void
le_dma_wrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)sc;

	bus_write_2(lesc->sc_rres, LEREG1_RAP, port);
	bus_barrier(lesc->sc_rres, LEREG1_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	bus_write_2(lesc->sc_rres, LEREG1_RDP, val);
}

static uint16_t
le_dma_rdcsr(struct lance_softc *sc, uint16_t port)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)sc;

	bus_write_2(lesc->sc_rres, LEREG1_RAP, port);
	bus_barrier(lesc->sc_rres, LEREG1_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	return (bus_read_2(lesc->sc_rres, LEREG1_RDP));
}

static void
le_dma_setutp(struct lance_softc *sc)
{
	struct lsi64854_softc *dma = ((struct le_dma_softc *)sc)->sc_dma;

	L64854_SCSR(dma, L64854_GCSR(dma) | E_TP_AUI);
	DELAY(20000);	/* We must not touch the LANCE chip for 20ms. */
}

static void
le_dma_setaui(struct lance_softc *sc)
{
	struct lsi64854_softc *dma = ((struct le_dma_softc *)sc)->sc_dma;

	L64854_SCSR(dma, L64854_GCSR(dma) & ~E_TP_AUI);
	DELAY(20000);	/* We must not touch the LANCE chip for 20ms. */
}

static int
le_dma_supmediachange(struct lance_softc *sc)
{
	struct ifmedia *ifm = &sc->sc_media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	/*
	 * Switch to the selected media. If autoselect is set, we don't
	 * really have to do anything. We'll switch to the other media
	 * when we detect loss of carrier.
	 */
	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_10_T:
		le_dma_setutp(sc);
		break;

	case IFM_10_5:
		le_dma_setaui(sc);
		break;

	case IFM_AUTO:
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

static void
le_dma_supmediastatus(struct lance_softc *sc, struct ifmediareq *ifmr)
{
	struct lsi64854_softc *dma = ((struct le_dma_softc *)sc)->sc_dma;

	/*
	 * Notify the world which media we're currently using.
	 */
	if (L64854_GCSR(dma) & E_TP_AUI)
		ifmr->ifm_active = IFM_MAKEWORD(IFM_ETHER, IFM_10_T, 0, 0);
	else
		ifmr->ifm_active = IFM_MAKEWORD(IFM_ETHER, IFM_10_5, 0, 0);
}

static void
le_dma_hwreset(struct lance_softc *sc)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)sc;
	struct lsi64854_softc *dma = lesc->sc_dma;
	uint32_t aui_bit, csr;

	/*
	 * Reset DMA channel.
	 */
	csr = L64854_GCSR(dma);
	aui_bit = csr & E_TP_AUI;
	DMA_RESET(dma);

	/* Write bits 24-31 of Lance address. */
	bus_write_4(dma->sc_res, L64854_REG_ENBAR,
	    lesc->sc_laddr & 0xff000000);

	DMA_ENINTR(dma);

	/*
	 * Disable E-cache invalidates on chip writes.
	 * Retain previous cable selection bit.
	 */
	csr = L64854_GCSR(dma);
	csr |= (E_DSBL_WR_INVAL | aui_bit);
	L64854_SCSR(dma, csr);
	DELAY(20000);	/* We must not touch the LANCE chip for 20ms. */
}

static int
le_dma_hwintr(struct lance_softc *sc)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)sc;
	struct lsi64854_softc *dma = lesc->sc_dma;

	return (DMA_INTR(dma));
}

static void
le_dma_nocarrier(struct lance_softc *sc)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)sc;

	/*
	 * Check if the user has requested a certain cable type, and
	 * if so, honor that request.
	 */

	if (L64854_GCSR(lesc->sc_dma) & E_TP_AUI) {
		switch (IFM_SUBTYPE(sc->sc_media.ifm_media)) {
		case IFM_10_5:
		case IFM_AUTO:
			if_printf(sc->sc_ifp, "lost carrier on UTP port, "
			    "switching to AUI port\n");
			le_dma_setaui(sc);
		}
	} else {
		switch (IFM_SUBTYPE(sc->sc_media.ifm_media)) {
		case IFM_10_T:
		case IFM_AUTO:
			if_printf(sc->sc_ifp, "lost carrier on AUI port, "
			    "switching to UTP port\n");
			le_dma_setutp(sc);
		}
	}
}

static void
le_dma_dma_callback(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct le_dma_softc *lesc = (struct le_dma_softc *)xsc;

	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("%s: bad DMA segment count", __func__));
	lesc->sc_laddr = segs[0].ds_addr;
}

static int
le_dma_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "le") == 0) {
		device_set_desc(dev, "LANCE Ethernet");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
le_dma_attach(device_t dev)
{
	struct le_dma_softc *lesc;
	struct lsi64854_softc *dma;
	struct lance_softc *sc;
	int error, i;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	LE_LOCK_INIT(sc, device_get_nameunit(dev));

	/*
	 * Establish link to `ledma' device.
	 * XXX hackery.
	 */
	dma = (struct lsi64854_softc *)device_get_softc(device_get_parent(dev));
	lesc->sc_dma = dma;
	lesc->sc_dma->sc_client = lesc;

	i = 0;
	lesc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (lesc->sc_rres == NULL) {
		device_printf(dev, "cannot allocate registers\n");
		error = ENXIO;
		goto fail_mtx;
	}

	i = 0;
	if ((lesc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &i, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "cannot allocate interrupt\n");
		error = ENXIO;
		goto fail_rres;
	}

	/* Attach the DMA engine. */
	error = lsi64854_attach(dma);
	if (error != 0) {
		device_printf(dev, "lsi64854_attach failed\n");
		goto fail_ires;
	}

	sc->sc_memsize = LEDMA_MEMSIZE;
	error = bus_dma_tag_create(
	    dma->sc_parent_dmat,	/* parent */
	    LEDMA_ALIGNMENT,		/* alignment */
	    LEDMA_BOUNDARY,		/* boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    sc->sc_memsize,		/* maxsize */
	    1,				/* nsegments */
	    sc->sc_memsize,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &lesc->sc_dmat);
	if (error != 0) {
		device_printf(dev, "cannot allocate buffer DMA tag\n");
		goto fail_lsi;
	}

	error = bus_dmamem_alloc(lesc->sc_dmat, (void **)&sc->sc_mem,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT, &lesc->sc_dmam);
	if (error != 0) {
		device_printf(dev, "cannot allocate DMA buffer memory\n");
		goto fail_dtag;
	}

	lesc->sc_laddr = 0;
	error = bus_dmamap_load(lesc->sc_dmat, lesc->sc_dmam, sc->sc_mem,
	    sc->sc_memsize, le_dma_dma_callback, lesc, 0);
	if (error != 0 || lesc->sc_laddr == 0) {
		device_printf(dev, "cannot load DMA buffer map\n");
		goto fail_dmem;
	}

	sc->sc_addr = lesc->sc_laddr & 0xffffff;
	sc->sc_flags = 0;
	sc->sc_conf3 = LE_C3_BSWP | LE_C3_ACON | LE_C3_BCON;

	sc->sc_mediachange = le_dma_supmediachange;
	sc->sc_mediastatus = le_dma_supmediastatus;
	sc->sc_supmedia = le_dma_supmedia;
	sc->sc_nsupmedia = nitems(le_dma_supmedia);
	sc->sc_defaultmedia = le_dma_supmedia[0];

	OF_getetheraddr(dev, sc->sc_enaddr);

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_dma_rdcsr;
	sc->sc_wrcsr = le_dma_wrcsr;
	sc->sc_hwreset = le_dma_hwreset;
	sc->sc_hwintr = le_dma_hwintr;
	sc->sc_nocarrier = le_dma_nocarrier;

	error = am7990_config(&lesc->sc_am7990, device_get_name(dev),
	    device_get_unit(dev));
	if (error != 0) {
		device_printf(dev, "cannot attach Am7990\n");
		goto fail_dmap;
	}

	error = bus_setup_intr(dev, lesc->sc_ires, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, am7990_intr, sc, &lesc->sc_ih);
	if (error != 0) {
		device_printf(dev, "cannot set up interrupt\n");
		goto fail_am7990;
	}

	return (0);

 fail_am7990:
	am7990_detach(&lesc->sc_am7990);
 fail_dmap:
	bus_dmamap_unload(lesc->sc_dmat, lesc->sc_dmam);
 fail_dmem:
	bus_dmamem_free(lesc->sc_dmat, sc->sc_mem, lesc->sc_dmam);
 fail_dtag:
	bus_dma_tag_destroy(lesc->sc_dmat);
 fail_lsi:
	lsi64854_detach(dma);
 fail_ires:
	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(lesc->sc_ires),
	    lesc->sc_ires);
 fail_rres:
	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(lesc->sc_rres),
	    lesc->sc_rres);
 fail_mtx:
	LE_LOCK_DESTROY(sc);
	return (error);
}

static int
le_dma_detach(device_t dev)
{
	struct le_dma_softc *lesc;
	struct lance_softc *sc;
	int error;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	bus_teardown_intr(dev, lesc->sc_ires, lesc->sc_ih);
	am7990_detach(&lesc->sc_am7990);
	bus_dmamap_unload(lesc->sc_dmat, lesc->sc_dmam);
	bus_dmamem_free(lesc->sc_dmat, sc->sc_mem, lesc->sc_dmam);
	bus_dma_tag_destroy(lesc->sc_dmat);
	error = lsi64854_detach(lesc->sc_dma);
	if (error != 0)
		return (error);
	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(lesc->sc_ires),
	    lesc->sc_ires);
	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(lesc->sc_rres),
	    lesc->sc_rres);
	LE_LOCK_DESTROY(sc);

	return (0);
}

static int
le_dma_suspend(device_t dev)
{
	struct le_dma_softc *lesc;

	lesc = device_get_softc(dev);

	lance_suspend(&lesc->sc_am7990.lsc);

	return (0);
}

static int
le_dma_resume(device_t dev)
{
	struct le_dma_softc *lesc;

	lesc = device_get_softc(dev);

	lance_resume(&lesc->sc_am7990.lsc);

	return (0);
}
