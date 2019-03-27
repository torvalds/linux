/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2006 Marius Strobl <marius@FreeBSD.org>
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
#include <net/if_media.h>

#include <dev/le/lancereg.h>
#include <dev/le/lancevar.h>
#include <dev/le/am7990reg.h>
#include <dev/le/am7990var.h>

/*
 * LANCE registers
 */
#define	LEREG1_RDP	0	/* Register Data port */
#define	LEREG1_RAP	2	/* Register Address port */

struct le_lebuffer_softc {
	struct am7990_softc	sc_am7990;	/* glue to MI code */

	struct resource		*sc_bres;

	struct resource		*sc_rres;

	struct resource		*sc_ires;
	void			*sc_ih;
};

static devclass_t le_lebuffer_devclass;

static device_probe_t le_lebuffer_probe;
static device_attach_t le_lebuffer_attach;
static device_detach_t le_lebuffer_detach;
static device_resume_t le_buffer_resume;
static device_suspend_t le_buffer_suspend;

static device_method_t le_lebuffer_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		le_lebuffer_probe),
	DEVMETHOD(device_attach,	le_lebuffer_attach),
	DEVMETHOD(device_detach,	le_lebuffer_detach),
	/* We can just use the suspend method here. */
	DEVMETHOD(device_shutdown,	le_buffer_suspend),
	DEVMETHOD(device_suspend,	le_buffer_suspend),
	DEVMETHOD(device_resume,	le_buffer_resume),

	{ 0, 0 }
};

DEFINE_CLASS_0(le, le_lebuffer_driver, le_lebuffer_methods,
    sizeof(struct le_lebuffer_softc));
DRIVER_MODULE(le, lebuffer, le_lebuffer_driver, le_lebuffer_devclass, 0, 0);
MODULE_DEPEND(le, ether, 1, 1, 1);
MODULE_DEPEND(le, lebuffer, 1, 1, 1);

/*
 * Media types supported
 */
static const int le_lebuffer_media[] = {
	IFM_MAKEWORD(IFM_ETHER, IFM_10_T, 0, 0)
};
#define	NLEMEDIA nitems(le_lebuffer_media)

static void le_lebuffer_wrcsr(struct lance_softc *, uint16_t, uint16_t);
static uint16_t le_lebuffer_rdcsr(struct lance_softc *, uint16_t);
static void le_lebuffer_copytodesc(struct lance_softc *, void *, int, int);
static void le_lebuffer_copyfromdesc(struct lance_softc *, void *, int, int);
static void le_lebuffer_copytobuf(struct lance_softc *, void *, int, int);
static void le_lebuffer_copyfrombuf(struct lance_softc *, void *, int, int);
static void le_lebuffer_zerobuf(struct lance_softc *, int, int);

static void
le_lebuffer_wrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;

	bus_write_2(lesc->sc_rres, LEREG1_RAP, port);
	bus_barrier(lesc->sc_rres, LEREG1_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	bus_write_2(lesc->sc_rres, LEREG1_RDP, val);
}

static uint16_t
le_lebuffer_rdcsr(struct lance_softc *sc, uint16_t port)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;

	bus_write_2(lesc->sc_rres, LEREG1_RAP, port);
	bus_barrier(lesc->sc_rres, LEREG1_RAP, 2, BUS_SPACE_BARRIER_WRITE);
	return (bus_read_2(lesc->sc_rres, LEREG1_RDP));
}

/*
 * It turns out that using bus_space(9) to access the buffers and the
 * descriptors yields way more throughput than accessing them via the
 * KVA returned by rman_get_virtual(9). The descriptor rings can be
 * accessed using 8-bit up to 64-bit operations while the buffers can
 * be only accessed using 8-bit and 16-bit operations.
 * NB:	For whatever reason setting LE_C3_BSWP has no effect with at
 *	least the 501-2981 (although their 'busmaster-regval' property
 *	indicates to set LE_C3_BSWP also for these cards), so we need
 *	to manually byte swap access to the buffers, i.e. the accesses
 *	going through the RX/TX FIFOs.
 */

static void
le_lebuffer_copytodesc(struct lance_softc *sc, void *fromv, int off, int len)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;
	caddr_t from = fromv;

	for (; len >= 8; len -= 8, off += 8, from += 8)
		bus_write_8(lesc->sc_bres, off, be64dec(from));
	for (; len >= 4; len -= 4, off += 4, from += 4)
		bus_write_4(lesc->sc_bres, off, be32dec(from));
	for (; len >= 2; len -= 2, off += 2, from += 2)
		bus_write_2(lesc->sc_bres, off, be16dec(from));
	if (len == 1)
		bus_write_1(lesc->sc_bres, off, *from);
}

static void
le_lebuffer_copyfromdesc(struct lance_softc *sc, void *tov, int off, int len)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;
	caddr_t to = tov;

	for (; len >= 8; len -= 8, off += 8, to += 8)
		be64enc(to,
		    bus_read_8(lesc->sc_bres, off));
	for (; len >= 4; len -= 4, off += 4, to += 4)
		be32enc(to,
		    bus_read_4(lesc->sc_bres, off));
	for (; len >= 2; len -= 2, off += 2, to += 2)
		be16enc(to,
		    bus_read_2(lesc->sc_bres, off));
	if (len == 1)
		*to = bus_read_1(lesc->sc_bres, off);
}

static void
le_lebuffer_copytobuf(struct lance_softc *sc, void *fromv, int off, int len)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;
	caddr_t from = fromv;

	for (; len >= 2; len -= 2, off += 2, from += 2)
		bus_write_2(lesc->sc_bres, off, le16dec(from));
	if (len == 1)
		bus_write_1(lesc->sc_bres, off + 1, *from);
}

static void
le_lebuffer_copyfrombuf(struct lance_softc *sc, void *tov, int off, int len)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;
	caddr_t to = tov;

	for (; len >= 2; len -= 2, off += 2, to += 2)
		le16enc(to,
		    bus_read_2(lesc->sc_bres, off));
	if (len == 1)
		*to = bus_read_1(lesc->sc_bres, off + 1);
}

static void
le_lebuffer_zerobuf(struct lance_softc *sc, int off, int len)
{
	struct le_lebuffer_softc *lesc = (struct le_lebuffer_softc *)sc;

	for (; len >= 2; len -= 2, off += 2)
		bus_write_2(lesc->sc_bres, off, 0);
	if (len == 1)
		bus_write_1(lesc->sc_bres, off + 1, 0);
}

static int
le_lebuffer_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "le") == 0) {
		device_set_desc(dev, "LANCE Ethernet");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
le_lebuffer_attach(device_t dev)
{
	struct le_lebuffer_softc *lesc;
	struct lance_softc *sc;
	int error, i;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	LE_LOCK_INIT(sc, device_get_nameunit(dev));

	/*
	 * The "register space" of the parent is just a buffer where the
	 * the LANCE descriptor rings and the RX/TX buffers can be stored.
	 */
	i = 0;
	lesc->sc_bres = bus_alloc_resource_any(device_get_parent(dev),
	    SYS_RES_MEMORY, &i, RF_ACTIVE);
	if (lesc->sc_bres == NULL) {
		device_printf(dev, "cannot allocate LANCE buffer\n");
		error = ENXIO;
		goto fail_mtx;
	}

	/* Allocate LANCE registers. */
	i = 0;
	lesc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (lesc->sc_rres == NULL) {
		device_printf(dev, "cannot allocate LANCE registers\n");
		error = ENXIO;
		goto fail_bres;
	}

	/* Allocate LANCE interrupt. */
	i = 0;
	if ((lesc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &i, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "cannot allocate interrupt\n");
		error = ENXIO;
		goto fail_rres;
	}

	/*
	 * LANCE view is offset by buffer location.
	 * Note that we don't use sc->sc_mem.
	 */
	sc->sc_addr = 0;
	sc->sc_memsize = rman_get_size(lesc->sc_bres);
	sc->sc_flags = 0;

	/* That old black magic... */
	if (OF_getprop(ofw_bus_get_node(dev), "busmaster-regval",
	    &sc->sc_conf3, sizeof(sc->sc_conf3)) == -1)
		sc->sc_conf3 = LE_C3_ACON | LE_C3_BCON;
	/*
	 * Make sure LE_C3_BSWP is cleared so that for cards where
	 * that flag actually works le_lebuffer_copy{from,to}buf()
	 * don't fail...
	 */
	sc->sc_conf3 &= ~LE_C3_BSWP;

	OF_getetheraddr(dev, sc->sc_enaddr);

	sc->sc_copytodesc = le_lebuffer_copytodesc;
	sc->sc_copyfromdesc = le_lebuffer_copyfromdesc;
	sc->sc_copytobuf = le_lebuffer_copytobuf;
	sc->sc_copyfrombuf = le_lebuffer_copyfrombuf;
	sc->sc_zerobuf = le_lebuffer_zerobuf;

	sc->sc_rdcsr = le_lebuffer_rdcsr;
	sc->sc_wrcsr = le_lebuffer_wrcsr;
	sc->sc_hwreset = NULL;
	sc->sc_hwinit = NULL;
	sc->sc_hwintr = NULL;
	sc->sc_nocarrier = NULL;
	sc->sc_mediachange = NULL;
	sc->sc_mediastatus = NULL;
	sc->sc_supmedia = le_lebuffer_media;
	sc->sc_nsupmedia = NLEMEDIA;
	sc->sc_defaultmedia = le_lebuffer_media[0];

	error = am7990_config(&lesc->sc_am7990, device_get_name(dev),
	    device_get_unit(dev));
	if (error != 0) {
		device_printf(dev, "cannot attach Am7990\n");
		goto fail_ires;
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
 fail_ires:
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
 fail_rres:
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
 fail_bres:
	bus_release_resource(device_get_parent(dev), SYS_RES_MEMORY,
	    rman_get_rid(lesc->sc_bres), lesc->sc_bres);
 fail_mtx:
	LE_LOCK_DESTROY(sc);
	return (error);
}

static int
le_lebuffer_detach(device_t dev)
{
	struct le_lebuffer_softc *lesc;
	struct lance_softc *sc;

	lesc = device_get_softc(dev);
	sc = &lesc->sc_am7990.lsc;

	bus_teardown_intr(dev, lesc->sc_ires, lesc->sc_ih);
	am7990_detach(&lesc->sc_am7990);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(lesc->sc_ires), lesc->sc_ires);
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(lesc->sc_rres), lesc->sc_rres);
	bus_release_resource(device_get_parent(dev), SYS_RES_MEMORY,
	    rman_get_rid(lesc->sc_bres), lesc->sc_bres);
	LE_LOCK_DESTROY(sc);

	return (0);
}

static int
le_buffer_suspend(device_t dev)
{
	struct le_lebuffer_softc *lesc;

	lesc = device_get_softc(dev);

	lance_suspend(&lesc->sc_am7990.lsc);

	return (0);
}

static int
le_buffer_resume(device_t dev)
{
	struct le_lebuffer_softc *lesc;

	lesc = device_get_softc(dev);

	lance_resume(&lesc->sc_am7990.lsc);

	return (0);
}
