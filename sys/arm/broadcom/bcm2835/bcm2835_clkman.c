/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Poul-Henning Kamp <phk@FreeBSD.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sema.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/broadcom/bcm2835/bcm2835_clkman.h>

static struct ofw_compat_data compat_data[] = {
	{"brcm,bcm2835-cprman",		1},
	{"broadcom,bcm2835-cprman",	1},
	{NULL,				0}
};

struct bcm2835_clkman_softc {
	device_t		sc_dev;

	struct resource *	sc_m_res;
	bus_space_tag_t		sc_m_bst;
	bus_space_handle_t	sc_m_bsh;
};

#define BCM_CLKMAN_WRITE(_sc, _off, _val)              \
    bus_space_write_4(_sc->sc_m_bst, _sc->sc_m_bsh, _off, _val)
#define BCM_CLKMAN_READ(_sc, _off)                     \
    bus_space_read_4(_sc->sc_m_bst, _sc->sc_m_bsh, _off)

#define W_CMCLK(_sc, unit, _val) BCM_CLKMAN_WRITE(_sc, unit, 0x5a000000 | (_val))
#define R_CMCLK(_sc, unit) BCM_CLKMAN_READ(_sc, unit)
#define W_CMDIV(_sc, unit, _val) BCM_CLKMAN_WRITE(_sc, (unit) + 4, 0x5a000000 | (_val))
#define R_CMDIV(_sc,  unit) BCM_CLKMAN_READ(_sc, (unit) + 4)

static int
bcm2835_clkman_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM283x Clock Manager");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm2835_clkman_attach(device_t dev)
{
	struct bcm2835_clkman_softc *sc;
	int rid;

	if (device_get_unit(dev) != 0) {
		device_printf(dev, "only one clk manager supported\n");
		return (ENXIO);
	}

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	rid = 0;
	sc->sc_m_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_m_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	sc->sc_m_bst = rman_get_bustag(sc->sc_m_res);
	sc->sc_m_bsh = rman_get_bushandle(sc->sc_m_res);

	return (bus_generic_attach(dev));
}

uint32_t
bcm2835_clkman_set_frequency(device_t dev, uint32_t unit, uint32_t hz)
{
	struct bcm2835_clkman_softc *sc;
	int i;
	uint32_t u;

	sc = device_get_softc(dev);

	if (unit != BCM_PWM_CLKSRC) {
		device_printf(sc->sc_dev,
		    "Unsupported unit 0x%x", unit);
		return (0);
	}

	W_CMCLK(sc, unit, 6);
	for (i = 0; i < 10; i++) {
		u = R_CMCLK(sc, unit);
		if (!(u&0x80))
			break;
		DELAY(1000);
	}
	if (u & 0x80) {
		device_printf(sc->sc_dev,
		    "Failed to stop clock for unit 0x%x", unit);
		return (0);
	}
	if (hz == 0)
		return (0);

	u = 500000000/hz;
	if (u < 4) {
		device_printf(sc->sc_dev,
		    "Frequency too high for unit 0x%x (max: 125 MHz)",
		    unit);
		return (0);
	}
	if (u > 0xfff) {
		device_printf(sc->sc_dev,
		    "Frequency too low for unit 0x%x (min: 123 kHz)",
		    unit);
		return (0);
	}
	hz = 500000000/u;
	W_CMDIV(sc, unit, u << 12);

	W_CMCLK(sc, unit, 0x16);
	for (i = 0; i < 10; i++) {
		u = R_CMCLK(sc, unit);
		if ((u&0x80))
			break;
		DELAY(1000);
	}
	if (!(u & 0x80)) {
		device_printf(sc->sc_dev,
		    "Failed to start clock for unit 0x%x", unit);
		return (0);
	}
	return (hz);
}

static int
bcm2835_clkman_detach(device_t dev)
{
	struct bcm2835_clkman_softc *sc;

	bus_generic_detach(dev);

	sc = device_get_softc(dev);
	if (sc->sc_m_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_m_res);

	return (0);
}

static device_method_t bcm2835_clkman_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm2835_clkman_probe),
	DEVMETHOD(device_attach,	bcm2835_clkman_attach),
	DEVMETHOD(device_detach,	bcm2835_clkman_detach),

	DEVMETHOD_END
};

static devclass_t bcm2835_clkman_devclass;
static driver_t bcm2835_clkman_driver = {
	"bcm2835_clkman",
	bcm2835_clkman_methods,
	sizeof(struct bcm2835_clkman_softc),
};

DRIVER_MODULE(bcm2835_clkman, simplebus, bcm2835_clkman_driver,
    bcm2835_clkman_devclass, 0, 0);
MODULE_VERSION(bcm2835_clkman, 1);
