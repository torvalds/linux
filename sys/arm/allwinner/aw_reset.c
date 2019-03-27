/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Allwinner module software reset registers
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/hwreset/hwreset.h>

#include "hwreset_if.h"

#define	RESET_OFFSET(index)	((index / 32) * 4)
#define	RESET_SHIFT(index)	(index % 32)

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun6i-a31-ahb1-reset",	1 },
	{ "allwinner,sun6i-a31-clock-reset",	1 },
	{ NULL,					0 }
};

struct aw_reset_softc {
	struct resource		*res;
	struct mtx		mtx;
};

static struct resource_spec aw_reset_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	RESET_READ(sc, reg)		bus_read_4((sc)->res, (reg))
#define	RESET_WRITE(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static int
aw_reset_assert(device_t dev, intptr_t id, bool reset)
{
	struct aw_reset_softc *sc;
	uint32_t reg_value;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mtx);
	reg_value = RESET_READ(sc, RESET_OFFSET(id));
	if (reset)
		reg_value &= ~(1 << RESET_SHIFT(id));
	else
		reg_value |= (1 << RESET_SHIFT(id));
	RESET_WRITE(sc, RESET_OFFSET(id), reg_value);
	mtx_unlock(&sc->mtx);

	return (0);
}

static int
aw_reset_is_asserted(device_t dev, intptr_t id, bool *reset)
{
	struct aw_reset_softc *sc;
	uint32_t reg_value;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mtx);
	reg_value = RESET_READ(sc, RESET_OFFSET(id));
	mtx_unlock(&sc->mtx);

	*reset = (reg_value & (1 << RESET_SHIFT(id))) != 0 ? false : true;

	return (0);
}

static int
aw_reset_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Module Resets");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_reset_attach(device_t dev)
{
	struct aw_reset_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, aw_reset_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	hwreset_register_ofw_provider(dev);

	return (0);
}

static device_method_t aw_reset_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_reset_probe),
	DEVMETHOD(device_attach,	aw_reset_attach),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	aw_reset_assert),
	DEVMETHOD(hwreset_is_asserted,	aw_reset_is_asserted),

	DEVMETHOD_END
};

static driver_t aw_reset_driver = {
	"aw_reset",
	aw_reset_methods,
	sizeof(struct aw_reset_softc),
};

static devclass_t aw_reset_devclass;

EARLY_DRIVER_MODULE(aw_reset, simplebus, aw_reset_driver, aw_reset_devclass,
    0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(aw_reset, 1);
