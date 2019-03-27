/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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

/*
 * Ingenic JZ4780 NAND and External Memory Controller (NEMC) driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct jz4780_dme_softc {
	device_t		dev;
	struct resource		*res[2];
};

static struct resource_spec jz4780_dme_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_MEMORY, 1, RF_ACTIVE },
	{ -1, 0 }
};

static int jz4780_dme_probe(device_t dev);
static int jz4780_dme_attach(device_t dev);
static int jz4780_dme_detach(device_t dev);

static int
jz4780_dme_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "davicom,dm9000"))
		return (ENXIO);

	device_set_desc(dev, "Davicom DM9000C 10/100BaseTX");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_dme_attach(device_t dev)
{
	struct jz4780_dme_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_dme_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	return (0);
}

static int
jz4780_dme_detach(device_t dev)
{
	struct jz4780_dme_softc *sc = device_get_softc(dev);

	bus_release_resources(dev, jz4780_dme_spec, sc->res);
	return (0);
}

static device_method_t jz4780_dme_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_dme_probe),
	DEVMETHOD(device_attach,	jz4780_dme_attach),
	DEVMETHOD(device_detach,	jz4780_dme_detach),

	DEVMETHOD_END
};

static driver_t jz4780_dme_driver = {
	"dme",
	jz4780_dme_methods,
	sizeof(struct jz4780_dme_softc),
};

static devclass_t jz4780_dme_devclass;

DRIVER_MODULE(jz4780_dme, simplebus, jz4780_dme_driver,
    jz4780_dme_devclass, 0, 0);
