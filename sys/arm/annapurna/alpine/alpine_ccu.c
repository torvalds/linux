/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define	AL_CCU_SNOOP_CONTROL_IOFAB_0_OFFSET	0x4000
#define	AL_CCU_SNOOP_CONTROL_IOFAB_1_OFFSET	0x5000
#define	AL_CCU_SPECULATION_CONTROL_OFFSET	0x4

static struct resource_spec al_ccu_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

struct al_ccu_softc {
	struct resource	*res;
};

static int al_ccu_probe(device_t dev);
static int al_ccu_attach(device_t dev);
static int al_ccu_detach(device_t dev);

static device_method_t al_ccu_methods[] = {
	DEVMETHOD(device_probe,		al_ccu_probe),
	DEVMETHOD(device_attach,	al_ccu_attach),
	DEVMETHOD(device_detach,	al_ccu_detach),

	{ 0, 0 }
};

static driver_t al_ccu_driver = {
	"ccu",
	al_ccu_methods,
	sizeof(struct al_ccu_softc)
};

static devclass_t al_ccu_devclass;

EARLY_DRIVER_MODULE(al_ccu, simplebus, al_ccu_driver,
    al_ccu_devclass, 0, 0, BUS_PASS_CPU + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(al_ccu, ofwbus, al_ccu_driver,
    al_ccu_devclass, 0, 0, BUS_PASS_CPU + BUS_PASS_ORDER_MIDDLE);

static int
al_ccu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "annapurna-labs,al-ccu"))
		return (ENXIO);

	device_set_desc(dev, "Alpine CCU");

	return (BUS_PROBE_DEFAULT);
}

static int
al_ccu_attach(device_t dev)
{
	struct al_ccu_softc *sc;
	int err;

	sc = device_get_softc(dev);

	err = bus_alloc_resources(dev, al_ccu_spec, &sc->res);
	if (err != 0) {
		device_printf(dev, "could not allocate resources\n");
		return (err);
	}

	/* Enable cache snoop */
	bus_write_4(sc->res, AL_CCU_SNOOP_CONTROL_IOFAB_0_OFFSET, 1);
	bus_write_4(sc->res, AL_CCU_SNOOP_CONTROL_IOFAB_1_OFFSET, 1);

	/* Disable speculative fetches from masters */
	bus_write_4(sc->res, AL_CCU_SPECULATION_CONTROL_OFFSET, 7);

	return (0);
}

static int
al_ccu_detach(device_t dev)
{
	struct al_ccu_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resources(dev, al_ccu_spec, &sc->res);

	return (0);
}
