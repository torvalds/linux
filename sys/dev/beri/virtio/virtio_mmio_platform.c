/*-
 * Copyright (c) 2014-2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/*
 * BERI interface for Virtio MMIO bus.
 *
 * This driver provides interrupt-engine for software-implemented
 * Virtio MMIO backend.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/cache.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/beri/virtio/virtio_mmio_platform.h>
#include <dev/virtio/mmio/virtio_mmio.h>
#include <dev/altera/pio/pio.h>

#include "virtio_mmio_if.h"
#include "pio_if.h"

static void platform_intr(void *arg);

struct virtio_mmio_platform_softc {
	struct resource		*res[1];
	void			*ih;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	void			(*intr_handler)(void *);
	void			*ih_user;
	device_t		pio_recv;
	device_t		pio_send;
	int			use_pio;
};

static int
setup_pio(struct virtio_mmio_platform_softc *sc, char *name, device_t *dev)
{
	phandle_t pio_node;
	struct fdt_ic *ic;
	phandle_t xref;
	phandle_t node;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if (OF_searchencprop(node, name, &xref,
		sizeof(xref)) == -1) {
		return (ENXIO);
	}

	pio_node = OF_node_from_xref(xref);
	SLIST_FOREACH(ic, &fdt_ic_list_head, fdt_ics) {
		if (ic->iph == pio_node) {
			*dev = ic->dev;
			PIO_CONFIGURE(*dev, PIO_OUT_ALL,
					PIO_UNMASK_ALL);
			return (0);
		}
	}

	return (ENXIO);
}

static int
virtio_mmio_platform_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "beri,virtio_mmio_platform"))
		return (ENXIO);

	device_set_desc(dev, "Virtio MMIO platform");
	return (BUS_PROBE_DEFAULT);
}

static int
virtio_mmio_platform_attach(device_t dev)
{
	struct virtio_mmio_platform_softc *sc;
	struct fdt_ic *fic;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->use_pio = 1;

	if ((setup_pio(sc, "pio-send", &sc->pio_send) != 0) ||
	    (setup_pio(sc, "pio-recv", &sc->pio_recv) != 0))
		sc->use_pio = 0;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	fic = malloc(sizeof(*fic), M_DEVBUF, M_WAITOK|M_ZERO);
	fic->iph = node;
	fic->dev = dev;
	SLIST_INSERT_HEAD(&fdt_ic_list_head, fic, fdt_ics);

	return (0);
}

static int
platform_prewrite(device_t dev, size_t offset, int val)
{
	struct virtio_mmio_platform_softc *sc;

	sc = device_get_softc(dev);

	switch (offset) {
	case (VIRTIO_MMIO_QUEUE_NOTIFY):
		mips_dcache_wbinv_all();
		break;
	default:
		break;
	}

	return (0);
}

static int
platform_note(device_t dev, size_t offset, int val)
{
	struct virtio_mmio_platform_softc *sc;
	int note;
	int i;

	sc = device_get_softc(dev);

	switch (offset) {
	case (VIRTIO_MMIO_QUEUE_NOTIFY):
		if (val == 0)
			note = Q_NOTIFY;
		else if (val == 1)
			note = Q_NOTIFY1;
		else
			note = 0;
		break;
	case (VIRTIO_MMIO_QUEUE_PFN):
		note = Q_PFN;
		break;
	case (VIRTIO_MMIO_QUEUE_SEL):
		note = Q_SEL;
		break;
	default:
		note = 0;
	}

	if (note) {
		mips_dcache_wbinv_all();

		if (!sc->use_pio)
			return (0);

		PIO_SET(sc->pio_send, note, 1);

		/* 
		 * Wait until host ack the request.
		 * Usually done within few cycles.
		 * TODO: bad
		 */

		for (i = 100; i > 0; i--) {
			if (PIO_READ(sc->pio_send) == 0)
				break;
		}

		if (i == 0)
			device_printf(sc->dev, "Warning: host busy\n");
	}

	return (0);
}

static void
platform_intr(void *arg)
{
	struct virtio_mmio_platform_softc *sc;
	int reg;

	sc = arg;

	if (sc->use_pio) {
		/* Read pending */
		reg = PIO_READ(sc->pio_recv);

		/* Ack */
		PIO_SET(sc->pio_recv, reg, 0);
	}

	/* Writeback, invalidate cache */
	mips_dcache_wbinv_all();

	if (sc->intr_handler != NULL)
		sc->intr_handler(sc->ih_user);
}

static int
platform_setup_intr(device_t dev, device_t mmio_dev,
			void *intr_handler, void *ih_user)
{
	struct virtio_mmio_platform_softc *sc;
	int rid;

	sc = device_get_softc(dev);

	sc->intr_handler = intr_handler;
	sc->ih_user = ih_user;

	if (sc->use_pio) {
		PIO_SETUP_IRQ(sc->pio_recv, platform_intr, sc);
		return (0);
	}

	rid = 0;
	sc->res[0] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		RF_ACTIVE);
	if (!sc->res[0]) {
		device_printf(dev, "Can't allocate interrupt\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->res[0], INTR_TYPE_MISC | INTR_MPSAFE,
		NULL, platform_intr, sc, &sc->ih)) {
		device_printf(dev, "Can't setup the interrupt\n");
		return (ENXIO);
	}

	return (0);
}

static int
platform_poll(device_t dev)
{

	mips_dcache_wbinv_all();

	return (0);
}

static device_method_t virtio_mmio_platform_methods[] = {
	DEVMETHOD(device_probe,		virtio_mmio_platform_probe),
	DEVMETHOD(device_attach,	virtio_mmio_platform_attach),

	/* virtio_mmio_if.h */
	DEVMETHOD(virtio_mmio_prewrite,		platform_prewrite),
	DEVMETHOD(virtio_mmio_note,		platform_note),
	DEVMETHOD(virtio_mmio_poll,		platform_poll),
	DEVMETHOD(virtio_mmio_setup_intr,	platform_setup_intr),
	DEVMETHOD_END
};

static driver_t virtio_mmio_platform_driver = {
	"virtio_mmio_platform",
	virtio_mmio_platform_methods,
	sizeof(struct virtio_mmio_platform_softc),
};

static devclass_t virtio_mmio_platform_devclass;

DRIVER_MODULE(virtio_mmio_platform, simplebus, virtio_mmio_platform_driver,
	virtio_mmio_platform_devclass, 0, 0);
