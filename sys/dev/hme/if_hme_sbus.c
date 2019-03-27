/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
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
 *
 *	from: NetBSD: if_hme_sbus.c,v 1.19 2004/03/17 17:04:58 pk Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SBus front-end device driver for the HME ethernet device.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <sparc64/sbus/sbusvar.h>

#include <dev/hme/if_hmereg.h>
#include <dev/hme/if_hmevar.h>

#include "miibus_if.h"

struct hme_sbus_softc {
	struct	hme_softc	hsc_hme;	/* HME device */
	struct	resource	*hsc_seb_res;
	struct	resource	*hsc_etx_res;
	struct	resource	*hsc_erx_res;
	struct	resource	*hsc_mac_res;
	struct	resource	*hsc_mif_res;
	struct	resource	*hsc_ires;
	void			*hsc_ih;
};

static int hme_sbus_probe(device_t);
static int hme_sbus_attach(device_t);
static int hme_sbus_detach(device_t);
static int hme_sbus_suspend(device_t);
static int hme_sbus_resume(device_t);

static device_method_t hme_sbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		hme_sbus_probe),
	DEVMETHOD(device_attach,	hme_sbus_attach),
	DEVMETHOD(device_detach,	hme_sbus_detach),
	DEVMETHOD(device_suspend,	hme_sbus_suspend),
	DEVMETHOD(device_resume,	hme_sbus_resume),
	/* Can just use the suspend method here. */
	DEVMETHOD(device_shutdown,	hme_sbus_suspend),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	hme_mii_readreg),
	DEVMETHOD(miibus_writereg,	hme_mii_writereg),
	DEVMETHOD(miibus_statchg,	hme_mii_statchg),

	DEVMETHOD_END
};

static driver_t hme_sbus_driver = {
	"hme",
	hme_sbus_methods,
	sizeof(struct hme_sbus_softc)
};

DRIVER_MODULE(hme, sbus, hme_sbus_driver, hme_devclass, 0, 0);
MODULE_DEPEND(hme, sbus, 1, 1, 1);
MODULE_DEPEND(hme, ether, 1, 1, 1);

static int
hme_sbus_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (strcmp(name, "SUNW,qfe") == 0 ||
	    strcmp(name, "SUNW,hme") == 0) {
		device_set_desc(dev, "Sun HME 10/100 Ethernet");
		return (0);
	}
	return (ENXIO);
}

static int
hme_sbus_attach(device_t dev)
{
	struct hme_sbus_softc *hsc;
	struct hme_softc *sc;
	u_long start, count;
	uint32_t burst;
	int i, error = 0;

	hsc = device_get_softc(dev);
	sc = &hsc->hsc_hme;
	mtx_init(&sc->sc_lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	/*
	 * Map five register banks:
	 *
	 *	bank 0: HME SEB registers
	 *	bank 1: HME ETX registers
	 *	bank 2: HME ERX registers
	 *	bank 3: HME MAC registers
	 *	bank 4: HME MIF registers
	 *
	 */
	i = 0;
	hsc->hsc_seb_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (hsc->hsc_seb_res == NULL) {
		device_printf(dev, "cannot map SEB registers\n");
		error = ENXIO;
		goto fail_mtx_res;
	}
	sc->sc_sebt = rman_get_bustag(hsc->hsc_seb_res);
	sc->sc_sebh = rman_get_bushandle(hsc->hsc_seb_res);

	i = 1;
	hsc->hsc_etx_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (hsc->hsc_etx_res == NULL) {
		device_printf(dev, "cannot map ETX registers\n");
		error = ENXIO;
		goto fail_seb_res;
	}
	sc->sc_etxt = rman_get_bustag(hsc->hsc_etx_res);
	sc->sc_etxh = rman_get_bushandle(hsc->hsc_etx_res);

	i = 2;
	hsc->hsc_erx_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (hsc->hsc_erx_res == NULL) {
		device_printf(dev, "cannot map ERX registers\n");
		error = ENXIO;
		goto fail_etx_res;
	}
	sc->sc_erxt = rman_get_bustag(hsc->hsc_erx_res);
	sc->sc_erxh = rman_get_bushandle(hsc->hsc_erx_res);

	i = 3;
	hsc->hsc_mac_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (hsc->hsc_mac_res == NULL) {
		device_printf(dev, "cannot map MAC registers\n");
		error = ENXIO;
		goto fail_erx_res;
	}
	sc->sc_mact = rman_get_bustag(hsc->hsc_mac_res);
	sc->sc_mach = rman_get_bushandle(hsc->hsc_mac_res);

	/*
	 * At least on some HMEs, the MIF registers seem to be inside the MAC
	 * range, so try to kludge around it.
	 */
	i = 4;
	hsc->hsc_mif_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &i, RF_ACTIVE);
	if (hsc->hsc_mif_res == NULL) {
		if (bus_get_resource(dev, SYS_RES_MEMORY, i,
		    &start, &count) != 0) {
			device_printf(dev, "cannot get MIF registers\n");
			error = ENXIO;
			goto fail_mac_res;
		}
		if (start < rman_get_start(hsc->hsc_mac_res) ||
		    start + count - 1 > rman_get_end(hsc->hsc_mac_res)) {
			device_printf(dev, "cannot move MIF registers to MAC "
			    "bank\n");
			error = ENXIO;
			goto fail_mac_res;
		}
		sc->sc_mift = sc->sc_mact;
		bus_space_subregion(sc->sc_mact, sc->sc_mach,
		    start - rman_get_start(hsc->hsc_mac_res), count,
		    &sc->sc_mifh);
	} else {
		sc->sc_mift = rman_get_bustag(hsc->hsc_mif_res);
		sc->sc_mifh = rman_get_bushandle(hsc->hsc_mif_res);
	}

	i = 0;
	hsc->hsc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &i, RF_SHAREABLE | RF_ACTIVE);
	if (hsc->hsc_ires == NULL) {
		device_printf(dev, "could not allocate interrupt\n");
		error = ENXIO;
		goto fail_mif_res;
	}

	OF_getetheraddr(dev, sc->sc_enaddr);

	burst = sbus_get_burstsz(dev);
	/* Translate into plain numerical format */
	if ((burst & SBUS_BURST_64))
		sc->sc_burst = 64;
	else if ((burst & SBUS_BURST_32))
		sc->sc_burst = 32;
	else if ((burst & SBUS_BURST_16))
		sc->sc_burst = 16;
	else
		 sc->sc_burst = 0;

	sc->sc_dev = dev;
	sc->sc_flags = 0;

	if ((error = hme_config(sc)) != 0) {
		device_printf(dev, "could not be configured\n");
		goto fail_ires;
	}

	if ((error = bus_setup_intr(dev, hsc->hsc_ires, INTR_TYPE_NET |
	    INTR_MPSAFE, NULL, hme_intr, sc, &hsc->hsc_ih)) != 0) {
		device_printf(dev, "couldn't establish interrupt\n");
		hme_detach(sc);
		goto fail_ires;
	}
	return (0);

fail_ires:
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(hsc->hsc_ires), hsc->hsc_ires);
fail_mif_res:
	if (hsc->hsc_mif_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(hsc->hsc_mif_res), hsc->hsc_mif_res);
	}
fail_mac_res:
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_mac_res), hsc->hsc_mac_res);
fail_erx_res:
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_erx_res), hsc->hsc_erx_res);
fail_etx_res:
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_etx_res), hsc->hsc_etx_res);
fail_seb_res:
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_seb_res), hsc->hsc_seb_res);
fail_mtx_res:
	mtx_destroy(&sc->sc_lock);
	return (error);
}

static int
hme_sbus_detach(device_t dev)
{
	struct hme_sbus_softc *hsc;
	struct hme_softc *sc;

	hsc = device_get_softc(dev);
	sc = &hsc->hsc_hme;
	bus_teardown_intr(dev, hsc->hsc_ires, hsc->hsc_ih);
	hme_detach(sc);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(hsc->hsc_ires), hsc->hsc_ires);
	if (hsc->hsc_mif_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(hsc->hsc_mif_res), hsc->hsc_mif_res);
	}
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_mac_res), hsc->hsc_mac_res);
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_erx_res), hsc->hsc_erx_res);
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_etx_res), hsc->hsc_etx_res);
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(hsc->hsc_seb_res), hsc->hsc_seb_res);
	mtx_destroy(&sc->sc_lock);
	return (0);
}

static int
hme_sbus_suspend(device_t dev)
{
	struct hme_sbus_softc *hsc;

	hsc = device_get_softc(dev);
	hme_suspend(&hsc->hsc_hme);
	return (0);
}

static int
hme_sbus_resume(device_t dev)
{
	struct hme_sbus_softc *hsc;

	hsc = device_get_softc(dev);
	hme_resume(&hsc->hsc_hme);
	return (0);
}
