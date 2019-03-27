/*-
 * Copyright 2014 John Wehle <john@feith.com>
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
 * Amlogic aml8726 random number generator driver.
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
#include <sys/random.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>


struct aml8726_rng_softc {
	device_t		dev;
	struct resource		*res[1];
	struct callout		co;
	int			ticks;
};

static struct resource_spec aml8726_rng_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_RNG_0_REG			0
#define	AML_RNG_1_REG			4

#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static void
aml8726_rng_harvest(void *arg)
{
	struct aml8726_rng_softc *sc = arg;
	uint32_t rn[2];

	rn[0] = CSR_READ_4(sc, AML_RNG_0_REG);
	rn[1] = CSR_READ_4(sc, AML_RNG_1_REG);

	random_harvest(rn, sizeof(rn), RANDOM_PURE_AML8726);

	callout_reset(&sc->co, sc->ticks, aml8726_rng_harvest, sc);
}

static int
aml8726_rng_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-rng"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 RNG");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_rng_attach(device_t dev)
{
	struct aml8726_rng_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_rng_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	/* Install a periodic collector for the RNG */
	if (hz > 100)
		sc->ticks = hz / 100;
	else
		sc->ticks = 1;

	callout_init(&sc->co, 1);
	callout_reset(&sc->co, sc->ticks, aml8726_rng_harvest, sc);

	return (0);
}

static int
aml8726_rng_detach(device_t dev)
{
	struct aml8726_rng_softc *sc = device_get_softc(dev);

	callout_drain(&sc->co);

	bus_release_resources(dev, aml8726_rng_spec, sc->res);

	return (0);
}

static device_method_t aml8726_rng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_rng_probe),
	DEVMETHOD(device_attach,	aml8726_rng_attach),
	DEVMETHOD(device_detach,	aml8726_rng_detach),

	DEVMETHOD_END
};

static driver_t aml8726_rng_driver = {
	"rng",
	aml8726_rng_methods,
	sizeof(struct aml8726_rng_softc),
};

static devclass_t aml8726_rng_devclass;

DRIVER_MODULE(aml8726_rng, simplebus, aml8726_rng_driver,
    aml8726_rng_devclass, 0, 0);
MODULE_DEPEND(aml8726_rng, random, 1, 1, 1);
