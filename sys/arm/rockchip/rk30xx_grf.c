/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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

/* General Register File for Rockchip RK30xx */

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
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include "rk30xx_grf.h"

struct rk30_grf_softc {
	struct resource		*res;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

static struct rk30_grf_softc *rk30_grf_sc = NULL;

#define	grf_read_4(sc, reg)		\
	bus_space_read_4((sc)->bst, (sc)->bsh, (reg))
#define	grf_write_4(sc, reg, val)	\
	bus_space_write_4((sc)->bst, (sc)->bsh, (reg), (val))

static int
rk30_grf_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "rockchip,rk30xx-grf")) {
		device_set_desc(dev, "RK30XX General Register File");
		return(BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
rk30_grf_attach(device_t dev)
{
	struct rk30_grf_softc *sc = device_get_softc(dev);
	int rid = 0;

	if (rk30_grf_sc)
		return (ENXIO);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->res) {
		device_printf(dev, "could not allocate resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	rk30_grf_sc = sc;

	return (0);
}

static device_method_t rk30_grf_methods[] = {
	DEVMETHOD(device_probe,		rk30_grf_probe),
	DEVMETHOD(device_attach,	rk30_grf_attach),
	{ 0, 0 }
};

static driver_t rk30_grf_driver = {
	"rk30_grf",
	rk30_grf_methods,
	sizeof(struct rk30_grf_softc),
};

static devclass_t rk30_grf_devclass;

DRIVER_MODULE(rk30_grf, simplebus, rk30_grf_driver, rk30_grf_devclass, 0, 0);

void
rk30_grf_gpio_pud(uint32_t bank, uint32_t pin, uint32_t state)
{
	uint32_t offset;

	offset = GRF_GPIO0B_PULL - 4 + (bank * 16) + ((pin / 8) * 4);
	pin = (7 - (pin % 8)) * 2;
	grf_write_4(rk30_grf_sc, offset, (0x3 << (16 + pin)) | (state << pin));
}

