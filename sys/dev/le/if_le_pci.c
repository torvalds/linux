/*	$NetBSD: if_le_pci.c,v 1.43 2005/12/11 12:22:49 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-3-Clause
 *
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
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

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/le/lancereg.h>
#include <dev/le/lancevar.h>
#include <dev/le/am79900var.h>

#define	AMD_VENDOR	0x1022
#define	AMD_PCNET_PCI	0x2000
#define	AMD_PCNET_HOME	0x2001
#define	PCNET_MEMSIZE	(32*1024)
#define	PCNET_PCI_RDP	0x10
#define	PCNET_PCI_RAP	0x12
#define	PCNET_PCI_BDP	0x16

struct le_pci_softc {
	struct am79900_softc	sc_am79900;	/* glue to MI code */

	struct resource		*sc_rres;

	struct resource		*sc_ires;
	void			*sc_ih;

	bus_dma_tag_t		sc_pdmat;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmam;
};

static device_probe_t le_pci_probe;
static device_attach_t le_pci_attach;
static device_detach_t le_pci_detach;
static device_resume_t le_pci_resume;
static device_suspend_t le_pci_suspend;

static device_method_t le_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		le_pci_probe),
	DEVMETHOD(device_attach,	le_pci_attach),
	DEVMETHOD(device_detach,	le_pci_detach),
	/* We can just use the suspend method here. */
	DEVMETHOD(device_shutdown,	le_pci_suspend),
	DEVMETHOD(device_suspend,	le_pci_suspend),
	DEVMETHOD(device_resume,	le_pci_resume),

	{ 0, 0 }
};

DEFINE_CLASS_0(le, le_pci_driver, le_pci_methods, sizeof(struct le_pci_softc));
DRIVER_MODULE(le, pci, le_pci_driver, le_devclass, 0, 0);
MODULE_DEPEND(le, ether, 1, 1, 1);

static const int le_home_supmedia[] = {
	IFM_MAKEWORD(IFM_ETHER, IFM_HPNA_1, 0, 0)
};

static const int le_pci_supmedia[] = {
	IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, IFM_FDX, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_T, 0, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_T, IFM_FDX, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_5, 0, 0),
	IFM_MAKEWORD(IFM_ETHER, IFM_10_5, IFM_FDX, 0)
};

static void le_pci_wrbcr(struct lance_softc *, uint16_t, uint16_t);
static uint16_t le_pci_rdbcr(struct lance_softc *, uint16_t);
static void le_pci_wrcsr(struct lance_softc *, uint16_t, uint16_t);
static uint16_t le_pci_rdcsr(struct lance_softc *, uint16_t);
static int le_pci_mediachange(struct lance_softc *);
static void le_pci_hwreset(struct lance_softc *);
static bus_dmamap_callback_t le_pci_dma_callback;

static void
le_pci_wrbcr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_pci_softc *lesc = (struct le_pci_softc *)sc;

	bus_write_2(lesc->sc_rres, PCNET_PCI_RAP, port);
	bus_barrier(lesc->sc_rres, PCNET_PCI_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	bus_write_2(lesc->sc_rres, PCNET_PCI_BDP, val);
}

static uint16_t
le_pci_rdbcr(struct lance_softc *sc, uint16_t port)
{
	struct le_pci_softc *lesc = (struct le_pci_softc *)sc;

	bus_write_2(lesc->sc_rres, PCNET_PCI_RAP, port);
	bus_barrier(lesc->sc_rres, PCNET_PCI_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	return (bus_read_2(lesc->sc_rres, PCNET_PCI_BDP));
}

static void
le_pci_wrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_pci_softc *lesc = (struct le_pci_softc *)sc;

	bus_write_2(lesc->sc_rres, PCNET_PCI_RAP, port);
	bus_barrier(lesc->sc_rres, PCNET_PCI_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	bus_write_2(lesc->sc_rres, PCNET_PCI_RDP, val);
}

static uint16_t
le_pci_rdcsr(struct lance_softc *sc, uint16_t port)
{
	struct le_pci_softc *lesc = (struct le_pci_softc *)sc;

	bus_write_2(lesc->sc_rres, PCNET_PCI_RAP, port);
	bus_barrier(lesc->sc_rres, PCNET_PCI_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	return (bus_read_2(lesc->sc_rres, PCNET_PCI_RDP));
}

static int
le_pci_mediachange(struct lance_softc *sc)
{
	struct ifmedia *ifm = &sc->sc_media;
	uint16_t reg;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (IFM_SUBTYPE(ifm->ifm_media) == IFM_HPNA_1)
		le_pci_wrbcr(sc, LE_BCR49,
		    (le_pci_rdbcr(sc, LE_BCR49) & ~LE_B49_PHYSEL) | 0x1);
	else if (IFM_SUBTYPE(ifm->ifm_media) == IFM_AUTO)
		le_pci_wrbcr(sc, LE_BCR2,
		    le_pci_rdbcr(sc, LE_BCR2) | LE_B2_ASEL);
	else {
		le_pci_wrbcr(sc, LE_BCR2,
		    le_pci_rdbcr(sc, LE_BCR2) & ~LE_B2_ASEL);

		reg = le_pci_rdcsr(sc, LE_CSR15);
		reg &= ~LE_C15_PORTSEL(LE_PORTSEL_MASK);
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_10_T)
			reg |= LE_C15_PORTSEL(LE_PORTSEL_10T);
		else
			reg |= LE_C15_PORTSEL(LE_PORTSEL_AUI);
		le_pci_wrcsr(sc, LE_CSR15, reg);
	}

	reg = le_pci_rdbcr(sc, LE_BCR9);
	if (IFM_OPTIONS(ifm->ifm_media) & IFM_FDX) {
		reg |= LE_B9_FDEN;
		/*
		 * Allow FDX on AUI only if explicitly chosen,
		 * not in autoselect mode.
		 */
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_10_5)
			reg |= LE_B9_AUIFD;
		else
			reg &= ~LE_B9_AUIFD;
	} else
		reg &= ~LE_B9_FDEN;
	le_pci_wrbcr(sc, LE_BCR9, reg);

	return (0);
}

static void
le_pci_hwreset(struct lance_softc *sc)
{

	/*
	 * Chip is stopped. Set software style to PCnet-PCI (32-bit).
	 * Actually, am79900.c implements ILACC support (hence its
	 * name) but unfortunately VMware does not. As far as this
	 * driver is concerned that should not make a difference
	 * though, as the settings used have the same meaning for
	 * both, ILACC and PCnet-PCI (note that there would be a
	 * difference for the ADD_FCS/NO_FCS bit if used).
	 */
	le_pci_wrbcr(sc, LE_BCR20, LE_B20_SSTYLE_PCNETPCI2);
}

static void
le_pci_dma_callback(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct lance_softc *sc = (struct lance_softc *)xsc;

	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("%s: bad DMA segment count", __func__));
	sc->sc_addr = segs[0].ds_addr;
}

static int
le_pci_probe(device_t dev)
{

	if (pci_get_vendor(dev) != AMD_VENDOR)
		return (ENXIO);

	switch (pci_get_device(dev)) {
	case AMD_PCNET_PCI:
		device_set_desc(dev, "AMD PCnet-PCI");
		/* Let pcn(4) win. */
		return (BUS_PROBE_LOW_PRIORITY);
	case AMD_PCNET_HOME:
		device_set_desc(dev, "AMD PCnet-Home");
		/* Let pcn(4) win. */
		return (BUS_PROBE_LOW_PRIORITY);
	default:
		return (ENXIO);
	}
}

static int
le_pci_attach(device_t dev)
{
	struct le_pci_softc *lesc;
	struct lance_softc *sc;
	int error, i;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am79900.lsc;

	LE_LOCK_INIT(sc, device_get_nameunit(dev));

	pci_enable_busmaster(dev);

	i = PCIR_BAR(0);
	lesc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
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

	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &lesc->sc_pdmat);
	if (error != 0) {
		device_printf(dev, "cannot allocate parent DMA tag\n");
		goto fail_ires;
	}

	sc->sc_memsize = PCNET_MEMSIZE;
	/*
	 * For Am79C970A, Am79C971 and Am79C978 the init block must be 2-byte
	 * aligned and the ring descriptors must be 16-byte aligned when using
	 * a 32-bit software style.
	 */
	error = bus_dma_tag_create(
	    lesc->sc_pdmat,		/* parent */
	    16, 0,			/* alignment, boundary */
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
		goto fail_pdtag;
	}

	error = bus_dmamem_alloc(lesc->sc_dmat, (void **)&sc->sc_mem,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT, &lesc->sc_dmam);
	if (error != 0) {
		device_printf(dev, "cannot allocate DMA buffer memory\n");
		goto fail_dtag;
	}

	sc->sc_addr = 0;
	error = bus_dmamap_load(lesc->sc_dmat, lesc->sc_dmam, sc->sc_mem,
	    sc->sc_memsize, le_pci_dma_callback, sc, 0);
	if (error != 0 || sc->sc_addr == 0) {
		device_printf(dev, "cannot load DMA buffer map\n");
		goto fail_dmem;
	}

	sc->sc_flags = LE_BSWAP;
	sc->sc_conf3 = 0;

	sc->sc_mediastatus = NULL;
	switch (pci_get_device(dev)) {
	case AMD_PCNET_HOME:
		sc->sc_mediachange = le_pci_mediachange;
		sc->sc_supmedia = le_home_supmedia;
		sc->sc_nsupmedia = sizeof(le_home_supmedia) / sizeof(int);
		sc->sc_defaultmedia = le_home_supmedia[0];
		break;
	default:
		sc->sc_mediachange = le_pci_mediachange;
		sc->sc_supmedia = le_pci_supmedia;
		sc->sc_nsupmedia = sizeof(le_pci_supmedia) / sizeof(int);
		sc->sc_defaultmedia = le_pci_supmedia[0];
	}

	/*
	 * Extract the physical MAC address from the ROM.
	 */
	bus_read_region_1(lesc->sc_rres, 0, sc->sc_enaddr,
	    sizeof(sc->sc_enaddr));

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_pci_rdcsr;
	sc->sc_wrcsr = le_pci_wrcsr;
	sc->sc_hwreset = le_pci_hwreset;
	sc->sc_hwinit = NULL;
	sc->sc_hwintr = NULL;
	sc->sc_nocarrier = NULL;

	error = am79900_config(&lesc->sc_am79900, device_get_name(dev),
	    device_get_unit(dev));
	if (error != 0) {
		device_printf(dev, "cannot attach Am79900\n");
		goto fail_dmap;
	}

	error = bus_setup_intr(dev, lesc->sc_ires, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, am79900_intr, sc, &lesc->sc_ih);
	if (error != 0) {
		device_printf(dev, "cannot set up interrupt\n");
		goto fail_am79900;
	}

	return (0);

 fail_am79900:
	am79900_detach(&lesc->sc_am79900);
 fail_dmap:
	bus_dmamap_unload(lesc->sc_dmat, lesc->sc_dmam);
 fail_dmem:
	bus_dmamem_free(lesc->sc_dmat, sc->sc_mem, lesc->sc_dmam);
 fail_dtag:
	bus_dma_tag_destroy(lesc->sc_dmat);
 fail_pdtag:
	bus_dma_tag_destroy(lesc->sc_pdmat);
 fail_ires:
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
 fail_rres:
	bus_release_resource(dev, SYS_RES_IOPORT,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
 fail_mtx:
	LE_LOCK_DESTROY(sc);
	return (error);
}

static int
le_pci_detach(device_t dev)
{
	struct le_pci_softc *lesc;
	struct lance_softc *sc;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am79900.lsc;

	bus_teardown_intr(dev, lesc->sc_ires, lesc->sc_ih);
	am79900_detach(&lesc->sc_am79900);
	bus_dmamap_unload(lesc->sc_dmat, lesc->sc_dmam);
	bus_dmamem_free(lesc->sc_dmat, sc->sc_mem, lesc->sc_dmam);
	bus_dma_tag_destroy(lesc->sc_dmat);
	bus_dma_tag_destroy(lesc->sc_pdmat);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
	bus_release_resource(dev, SYS_RES_IOPORT,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
	LE_LOCK_DESTROY(sc);

	return (0);
}

static int
le_pci_suspend(device_t dev)
{
	struct le_pci_softc *lesc;

	lesc = device_get_softc(dev);

	lance_suspend(&lesc->sc_am79900.lsc);

	return (0);
}

static int
le_pci_resume(device_t dev)
{
	struct le_pci_softc *lesc;

	lesc = device_get_softc(dev);

	lance_resume(&lesc->sc_am79900.lsc);

	return (0);
}
