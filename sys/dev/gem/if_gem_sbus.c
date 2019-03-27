/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * Copyright (c) 2007 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: if_gem_pci.c,v 1.7 2001/10/18 15:09:15 thorpej Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SBus bindings for Sun GEM Ethernet controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>

#include <sparc64/sbus/sbusvar.h>

#include <dev/gem/if_gemreg.h>
#include <dev/gem/if_gemvar.h>

#include "miibus_if.h"

static device_probe_t gem_sbus_probe;
static device_attach_t gem_sbus_attach;
static device_detach_t gem_sbus_detach;
static device_suspend_t gem_sbus_suspend;
static device_resume_t gem_sbus_resume;

static device_method_t gem_sbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gem_sbus_probe),
	DEVMETHOD(device_attach,	gem_sbus_attach),
	DEVMETHOD(device_detach,	gem_sbus_detach),
	DEVMETHOD(device_suspend,	gem_sbus_suspend),
	DEVMETHOD(device_resume,	gem_sbus_resume),
	/* Use the suspend handler here, it is all that is required. */
	DEVMETHOD(device_shutdown,	gem_sbus_suspend),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	gem_mii_readreg),
	DEVMETHOD(miibus_writereg,	gem_mii_writereg),
	DEVMETHOD(miibus_statchg,	gem_mii_statchg),

	DEVMETHOD_END
};

static driver_t gem_sbus_driver = {
	"gem",
	gem_sbus_methods,
	sizeof(struct gem_softc)
};

DRIVER_MODULE(gem, sbus, gem_sbus_driver, gem_devclass, 0, 0);
MODULE_DEPEND(gem, sbus, 1, 1, 1);
MODULE_DEPEND(gem, ether, 1, 1, 1);

static int
gem_sbus_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "network") == 0 &&
	    ofw_bus_get_compat(dev) != NULL &&
	    strcmp(ofw_bus_get_compat(dev), "SUNW,sbus-gem") == 0) {
		device_set_desc(dev, "Sun GEM Gigabit Ethernet");
		return (0);
	}

	return (ENXIO);
}

static struct resource_spec gem_sbus_res_spec[] = {
	{ SYS_RES_IRQ, 0, RF_SHAREABLE | RF_ACTIVE },	/* GEM_RES_INTR */
	{ SYS_RES_MEMORY, 1, RF_ACTIVE },		/* GEM_RES_BANK1 */
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },		/* GEM_RES_BANK2 */
	{ -1, 0 }
};

static int
gem_sbus_attach(device_t dev)
{
	struct gem_softc *sc;
	int burst;
	uint32_t val;

	sc = device_get_softc(dev);
	sc->sc_variant = GEM_SUN_GEM;
	sc->sc_dev = dev;
	/* All known SBus models use a SERDES. */
	sc->sc_flags = GEM_SERDES;

	if (bus_alloc_resources(dev, gem_sbus_res_spec, sc->sc_res)) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, gem_sbus_res_spec, sc->sc_res);
		return (ENXIO);
	}

	GEM_LOCK_INIT(sc, device_get_nameunit(dev));

	OF_getetheraddr(dev, sc->sc_enaddr);

	burst = sbus_get_burstsz(dev);
	val = GEM_SBUS_CFG_PARITY;
	if ((burst & SBUS_BURST64_MASK) != 0) {
		val |= GEM_SBUS_CFG_64BIT;
		burst >>= SBUS_BURST64_SHIFT;
	}
	if ((burst & SBUS_BURST_64) != 0)
		val |= GEM_SBUS_CFG_BURST_64;
	else if ((burst & SBUS_BURST_32) != 0)
		val |= GEM_SBUS_CFG_BURST_32;
	else {
		device_printf(dev, "unsupported burst size\n");
		goto fail;
	}
	/* Reset the SBus interface only. */
	(void)GEM_BANK2_READ_4(sc, GEM_SBUS_BIF_RESET);
	DELAY(100);
	GEM_BANK2_WRITE_4(sc, GEM_SBUS_CONFIG, val);

	if (gem_attach(sc) != 0) {
		device_printf(dev, "could not be attached\n");
		goto fail;
	}

	if (bus_setup_intr(dev, sc->sc_res[GEM_RES_INTR], INTR_TYPE_NET |
	    INTR_MPSAFE, NULL, gem_intr, sc, &sc->sc_ih) != 0) {
		device_printf(dev, "failed to set up interrupt\n");
		gem_detach(sc);
		goto fail;
	}
	return (0);

 fail:
	GEM_LOCK_DESTROY(sc);
	bus_release_resources(dev, gem_sbus_res_spec, sc->sc_res);
	return (ENXIO);
}

static int
gem_sbus_detach(device_t dev)
{
	struct gem_softc *sc;

	sc = device_get_softc(dev);
	bus_teardown_intr(dev, sc->sc_res[GEM_RES_INTR], sc->sc_ih);
	gem_detach(sc);
	GEM_LOCK_DESTROY(sc);
	bus_release_resources(dev, gem_sbus_res_spec, sc->sc_res);
	return (0);
}

static int
gem_sbus_suspend(device_t dev)
{

	gem_suspend(device_get_softc(dev));
	return (0);
}

static int
gem_sbus_resume(device_t dev)
{

	gem_resume(device_get_softc(dev));
	return (0);
}
