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

/* PMU for Rockchip RK30xx */

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

#include "rk30xx_pmu.h"

struct rk30_pmu_softc {
	struct resource		*res;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

static struct rk30_pmu_softc *rk30_pmu_sc = NULL;

#define	pmu_read_4(sc, reg)		\
	bus_space_read_4((sc)->bst, (sc)->bsh, (reg))
#define	pmu_write_4(sc, reg, val)	\
	bus_space_write_4((sc)->bst, (sc)->bsh, (reg), (val))

static int
rk30_pmu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "rockchip,rk30xx-pmu")) {
		device_set_desc(dev, "RK30XX PMU");
		return(BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
rk30_pmu_attach(device_t dev)
{
	struct rk30_pmu_softc *sc = device_get_softc(dev);
	int rid = 0;

	if (rk30_pmu_sc)
		return (ENXIO);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->res) {
		device_printf(dev, "could not allocate resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	rk30_pmu_sc = sc;

	return (0);
}

static device_method_t rk30_pmu_methods[] = {
	DEVMETHOD(device_probe,		rk30_pmu_probe),
	DEVMETHOD(device_attach,	rk30_pmu_attach),
	{ 0, 0 }
};

static driver_t rk30_pmu_driver = {
	"rk30_pmu",
	rk30_pmu_methods,
	sizeof(struct rk30_pmu_softc),
};

static devclass_t rk30_pmu_devclass;

DRIVER_MODULE(rk30_pmu, simplebus, rk30_pmu_driver, rk30_pmu_devclass, 0, 0);

void
rk30_pmu_gpio_pud(uint32_t pin, uint32_t state)
{
	uint32_t offset;

	offset = PMU_GPIO0A_PULL + ((pin / 8) * 4);
	pin = (pin % 8) * 2;
	pmu_write_4(rk30_pmu_sc, offset, (0x3 << (16 + pin)) | (state << pin));
}

