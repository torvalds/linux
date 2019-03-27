/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm64/coresight/coresight.h>

#include "coresight_if.h"

#define	EDPCSR				0x0a0
#define	EDCIDSR				0x0a4
#define	EDVIDSR				0x0a8
#define	EDPCSR_HI			0x0ac
#define	EDOSLAR				0x300
#define	EDPRCR				0x310
#define	 EDPRCR_COREPURQ		(1 << 3)
#define	 EDPRCR_CORENPDRQ		(1 << 0)
#define	EDPRSR				0x314
#define	EDDEVID1			0xfc4
#define	EDDEVID				0xfc8

static struct ofw_compat_data compat_data[] = {
	{ "arm,coresight-cpu-debug",		1 },
	{ NULL,					0 }
};

struct debug_softc {
	struct resource			*res;
	struct coresight_platform_data	*pdata;
};

static struct resource_spec debug_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
debug_init(device_t dev)
{
	struct debug_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	/* Unlock Coresight */
	bus_write_4(sc->res, CORESIGHT_LAR, CORESIGHT_UNLOCK);

	/* Unlock Debug */
	bus_write_4(sc->res, EDOSLAR, 0);

	/* Already initialized? */
	reg = bus_read_4(sc->res, EDPRCR);
	if (reg & EDPRCR_CORENPDRQ)
		return (0);

	/* Enable power */
	reg |= EDPRCR_COREPURQ;
	bus_write_4(sc->res, EDPRCR, reg);

	do {
		reg = bus_read_4(sc->res, EDPRSR);
	} while ((reg & EDPRCR_CORENPDRQ) == 0);

	return (0);
}

static int
debug_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Coresight CPU Debug");

	return (BUS_PROBE_DEFAULT);
}

static int
debug_attach(device_t dev)
{
	struct coresight_desc desc;
	struct debug_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, debug_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	sc->pdata = coresight_get_platform_data(dev);
	desc.pdata = sc->pdata;
	desc.dev = dev;
	desc.dev_type = CORESIGHT_CPU_DEBUG;
	coresight_register(&desc);

	return (0);
}

static device_method_t debug_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		debug_probe),
	DEVMETHOD(device_attach,	debug_attach),

	/* Coresight interface */
	DEVMETHOD(coresight_init,	debug_init),
	DEVMETHOD_END
};

static driver_t debug_driver = {
	"debug",
	debug_methods,
	sizeof(struct debug_softc),
};

static devclass_t debug_devclass;

EARLY_DRIVER_MODULE(debug, simplebus, debug_driver, debug_devclass,
    0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_LATE);
MODULE_VERSION(debug, 1);
