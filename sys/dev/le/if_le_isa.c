/*	$NetBSD: if_le_isa.c,v 1.41 2005/12/24 20:27:41 perry Exp $	*/

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

#include <isa/isavar.h>

#include <dev/le/lancereg.h>
#include <dev/le/lancevar.h>
#include <dev/le/am7990var.h>

#define	LE_ISA_MEMSIZE	(16*1024)
#define	PCNET_RDP	0x10
#define	PCNET_RAP	0x12

struct le_isa_softc {
	struct am7990_softc	sc_am7990;	/* glue to MI code */

	bus_size_t		sc_rap;		/* offsets to LANCE... */
	bus_size_t		sc_rdp;		/* ...registers */

	struct resource		*sc_rres;

	struct resource		*sc_dres;

	struct resource		*sc_ires;
	void			*sc_ih;

	bus_dma_tag_t		sc_pdmat;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmam;
};

static device_probe_t le_isa_probe;
static device_attach_t le_isa_attach;
static device_detach_t le_isa_detach;
static device_resume_t le_isa_resume;
static device_suspend_t le_isa_suspend;

static device_method_t le_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		le_isa_probe),
	DEVMETHOD(device_attach,	le_isa_attach),
	DEVMETHOD(device_detach,	le_isa_detach),
	/* We can just use the suspend method here. */
	DEVMETHOD(device_shutdown,	le_isa_suspend),
	DEVMETHOD(device_suspend,	le_isa_suspend),
	DEVMETHOD(device_resume,	le_isa_resume),

	{ 0, 0 }
};

struct le_isa_param {
	const char	*name;
	u_long		iosize;
	bus_size_t	rap;
	bus_size_t	rdp;
	bus_size_t	macstart;
	int		macstride;
} static const le_isa_params[] = {
	{ "BICC Isolan", 24, 0xe, 0xc, 0, 2 },
	{ "Novell NE2100", 16, 0x12, 0x10, 0, 1 }
};

static struct isa_pnp_id le_isa_ids[] = {
	{ 0x0322690e, "Cabletron E2200 Single Chip" },	/* CSI2203 */
	{ 0x0110490a, "Boca LANCard Combo" },		/* BRI1001 */
	{ 0x0100a60a, "Melco Inc. LGY-IV" },		/* BUF0001 */
	{ 0xd880d041, "Novell NE2100" },		/* PNP80D8 */
	{ 0x0082d041, "Cabletron E2100 Series DNI" },	/* PNP8200 */
	{ 0x3182d041, "AMD AM1500T/AM2100" },		/* PNP8231 */
	{ 0x8c82d041, "AMD PCnet-ISA" },		/* PNP828C */
	{ 0x8d82d041, "AMD PCnet-32" },			/* PNP828D */
	{ 0xcefaedfe, "Racal InterLan EtherBlaster" },	/* _WMFACE */
	{ 0, NULL }
};

static void le_isa_wrcsr(struct lance_softc *, uint16_t, uint16_t);
static uint16_t le_isa_rdcsr(struct lance_softc *, uint16_t);
static bus_dmamap_callback_t le_isa_dma_callback;
static int le_isa_probe_legacy(device_t, const struct le_isa_param *);

static void
le_isa_wrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_isa_softc *lesc = (struct le_isa_softc *)sc;

	bus_write_2(lesc->sc_rres, lesc->sc_rap, port);
	bus_barrier(lesc->sc_rres, lesc->sc_rap, 2, BUS_SPACE_BARRIER_WRITE);
	bus_write_2(lesc->sc_rres, lesc->sc_rdp, val);
}

static uint16_t
le_isa_rdcsr(struct lance_softc *sc, uint16_t port)
{
	struct le_isa_softc *lesc = (struct le_isa_softc *)sc;

	bus_write_2(lesc->sc_rres, lesc->sc_rap, port);
	bus_barrier(lesc->sc_rres, lesc->sc_rap, 2, BUS_SPACE_BARRIER_WRITE);
	return (bus_read_2(lesc->sc_rres, lesc->sc_rdp));
}

static void
le_isa_dma_callback(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct lance_softc *sc = (struct lance_softc *)xsc;

	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("%s: bad DMA segment count", __func__));
	sc->sc_addr = segs[0].ds_addr;
}

static int
le_isa_probe_legacy(device_t dev, const struct le_isa_param *leip)
{
	struct le_isa_softc *lesc;
	struct lance_softc *sc;
	int error, i;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	i = 0;
	lesc->sc_rres = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &i,
	    leip->iosize, RF_ACTIVE);
	if (lesc->sc_rres == NULL)
		return (ENXIO);
	lesc->sc_rap = leip->rap;
	lesc->sc_rdp = leip->rdp;

	/* Stop the chip and put it in a known state. */
	le_isa_wrcsr(sc, LE_CSR0, LE_C0_STOP);
	DELAY(100);
	if (le_isa_rdcsr(sc, LE_CSR0) != LE_C0_STOP) {
		error = ENXIO;
		goto fail;
	}
	le_isa_wrcsr(sc, LE_CSR3, 0);
	error = 0;

 fail:
	bus_release_resource(dev, SYS_RES_IOPORT,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
	return (error);
}

static int
le_isa_probe(device_t dev)
{
	int i;

	switch (ISA_PNP_PROBE(device_get_parent(dev), dev, le_isa_ids)) {
	case 0:
		return (BUS_PROBE_DEFAULT);
	case ENOENT:
		for (i = 0; i < nitems(le_isa_params); i++) {
			if (le_isa_probe_legacy(dev, &le_isa_params[i]) == 0) {
				device_set_desc(dev, le_isa_params[i].name);
				return (BUS_PROBE_DEFAULT);
			}
		}
		/* FALLTHROUGH */
	case ENXIO:
	default:
		return (ENXIO);
	}
}

static int
le_isa_attach(device_t dev)
{
	struct le_isa_softc *lesc;
	struct lance_softc *sc;
	bus_size_t macstart, rap, rdp;
	int error, i, j, macstride;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	LE_LOCK_INIT(sc, device_get_nameunit(dev));

	j = 0;
	switch (ISA_PNP_PROBE(device_get_parent(dev), dev, le_isa_ids)) {
	case 0:
		lesc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		    &j, RF_ACTIVE);
		rap = PCNET_RAP;
		rdp = PCNET_RDP;
		macstart = 0;
		macstride = 1;
		break;
	case ENOENT:
		for (i = 0; i < nitems(le_isa_params); i++) {
			if (le_isa_probe_legacy(dev, &le_isa_params[i]) == 0) {
				lesc->sc_rres = bus_alloc_resource_anywhere(dev,
				    SYS_RES_IOPORT, &j,
				    le_isa_params[i].iosize, RF_ACTIVE);
				rap = le_isa_params[i].rap;
				rdp = le_isa_params[i].rdp;
				macstart = le_isa_params[i].macstart;
				macstride = le_isa_params[i].macstride;
				goto found;
			}
		}
		/* FALLTHROUGH */
	case ENXIO:
	default:
		device_printf(dev, "cannot determine chip\n");
		error = ENXIO;
		goto fail_mtx;
	}

 found:
	if (lesc->sc_rres == NULL) {
		device_printf(dev, "cannot allocate registers\n");
		error = ENXIO;
		goto fail_mtx;
	}
	lesc->sc_rap = rap;
	lesc->sc_rdp = rdp;

	i = 0;
	if ((lesc->sc_dres = bus_alloc_resource_any(dev, SYS_RES_DRQ,
	    &i, RF_ACTIVE)) == NULL) {
		device_printf(dev, "cannot allocate DMA channel\n");
		error = ENXIO;
		goto fail_rres;
	}

	i = 0;
	if ((lesc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &i, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "cannot allocate interrupt\n");
		error = ENXIO;
		goto fail_dres;
	}

	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_24BIT,	/* lowaddr */
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

	sc->sc_memsize = LE_ISA_MEMSIZE;
	/*
	 * For Am79C90, Am79C961 and Am79C961A the init block must be 2-byte
	 * aligned and the ring descriptors must be 8-byte aligned.
	 */
	error = bus_dma_tag_create(
	    lesc->sc_pdmat,		/* parent */
	    8, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_24BIT,	/* lowaddr */
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
	    sc->sc_memsize, le_isa_dma_callback, sc, 0);
	if (error != 0 || sc->sc_addr == 0) {
		device_printf(dev, "cannot load DMA buffer map\n");
		goto fail_dmem;
	}

	isa_dmacascade(rman_get_start(lesc->sc_dres));

	sc->sc_flags = 0;
	sc->sc_conf3 = 0;

	/*
	 * Extract the physical MAC address from the ROM.
	 */
	for (i = 0; i < sizeof(sc->sc_enaddr); i++)
		sc->sc_enaddr[i] = bus_read_1(lesc->sc_rres,
		    macstart + i * macstride);

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_isa_rdcsr;
	sc->sc_wrcsr = le_isa_wrcsr;
	sc->sc_hwreset = NULL;
	sc->sc_hwinit = NULL;
	sc->sc_hwintr = NULL;
	sc->sc_nocarrier = NULL;
	sc->sc_mediachange = NULL;
	sc->sc_mediastatus = NULL;
	sc->sc_supmedia = NULL;

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
 fail_pdtag:
	bus_dma_tag_destroy(lesc->sc_pdmat);
 fail_ires:
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
 fail_dres:
	bus_release_resource(dev, SYS_RES_DRQ,
	    rman_get_rid(lesc->sc_dres), lesc->sc_dres);
 fail_rres:
	bus_release_resource(dev, SYS_RES_IOPORT,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
 fail_mtx:
	LE_LOCK_DESTROY(sc);
	return (error);
}

static int
le_isa_detach(device_t dev)
{
	struct le_isa_softc *lesc;
	struct lance_softc *sc;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	bus_teardown_intr(dev, lesc->sc_ires, lesc->sc_ih);
	am7990_detach(&lesc->sc_am7990);
	bus_dmamap_unload(lesc->sc_dmat, lesc->sc_dmam);
	bus_dmamem_free(lesc->sc_dmat, sc->sc_mem, lesc->sc_dmam);
	bus_dma_tag_destroy(lesc->sc_dmat);
	bus_dma_tag_destroy(lesc->sc_pdmat);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
	bus_release_resource(dev, SYS_RES_DRQ,
	    rman_get_rid(lesc->sc_dres), lesc->sc_dres);
	bus_release_resource(dev, SYS_RES_IOPORT,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
	LE_LOCK_DESTROY(sc);

	return (0);
}

static int
le_isa_suspend(device_t dev)
{
	struct le_isa_softc *lesc;

	lesc = device_get_softc(dev);

	lance_suspend(&lesc->sc_am7990.lsc);

	return (0);
}

static int
le_isa_resume(device_t dev)
{
	struct le_isa_softc *lesc;

	lesc = device_get_softc(dev);

	lance_resume(&lesc->sc_am7990.lsc);

	return (0);
}

DEFINE_CLASS_0(le, le_isa_driver, le_isa_methods, sizeof(struct le_isa_softc));
DRIVER_MODULE(le, isa, le_isa_driver, le_devclass, 0, 0);
MODULE_DEPEND(le, ether, 1, 1, 1);
ISA_PNP_INFO(le_isa_ids);
