/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2006-2008 Semihalf, Grzegorz Bernacki
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From: FreeBSD: src/sys/powerpc/mpc85xx/ds1553_bus_lbc.c,v 1.2 2009/06/24 15:48:20 raj
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus_subr.h>

#include "ds1553_reg.h"
#include "clock_if.h"

static devclass_t rtc_devclass;

static int rtc_attach(device_t dev);
static int rtc_probe(device_t dev);

static device_method_t rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtc_probe),
	DEVMETHOD(device_attach,	rtc_attach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	ds1553_gettime),
	DEVMETHOD(clock_settime,	ds1553_settime),

	{ 0, 0 }
};

static driver_t rtc_driver = {
	"rtc",
	rtc_methods,
	sizeof(struct ds1553_softc),
};

DRIVER_MODULE(rtc, lbc, rtc_driver, rtc_devclass, 0, 0);

static int
rtc_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "dallas,ds1553"))
		return (ENXIO);

	device_set_desc(dev, "Dallas Semiconductor DS1553 RTC");
	return (BUS_PROBE_DEFAULT);
}

static int
rtc_attach(device_t dev)
{
	struct timespec ts;
	struct ds1553_softc *sc;
	int error;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(struct ds1553_softc));

	mtx_init(&sc->sc_mtx, "rtc_mtx", NULL, MTX_SPIN);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "cannot allocate resources\n");
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	sc->sc_bst = rman_get_bustag(sc->res);
	sc->sc_bsh = rman_get_bushandle(sc->res);

	if ((error = ds1553_attach(dev)) != 0) {
		device_printf(dev, "cannot attach time of day clock\n");
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
		mtx_destroy(&sc->sc_mtx);
		return (error);
	}

	clock_register(dev, 1000000);

	if (bootverbose) {
		ds1553_gettime(dev, &ts);
		device_printf(dev, "current time: %ld.%09ld\n",
		    (long)ts.tv_sec, ts.tv_nsec);
	}

	return (0);
}
