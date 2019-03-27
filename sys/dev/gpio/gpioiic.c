/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2010 Luiz Otavio O Souza
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#endif

#include <dev/gpio/gpiobusvar.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "gpiobus_if.h"
#include "iicbb_if.h"

#define	GPIOIIC_SCL_DFLT	0
#define	GPIOIIC_SDA_DFLT	1
#define	GPIOIIC_MIN_PINS	2

struct gpioiic_softc 
{
	device_t	sc_dev;
	device_t	sc_busdev;
	int		scl_pin;
	int		sda_pin;
};

static int gpioiic_probe(device_t);
static int gpioiic_attach(device_t);

/* iicbb interface */
static void gpioiic_reset_bus(device_t);
static void gpioiic_setsda(device_t, int);
static void gpioiic_setscl(device_t, int);
static int gpioiic_getsda(device_t);
static int gpioiic_getscl(device_t);
static int gpioiic_reset(device_t, u_char, u_char, u_char *);

static int
gpioiic_probe(device_t dev)
{
	struct gpiobus_ivar *devi;

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "gpioiic"))
		return (ENXIO);
#endif
	devi = GPIOBUS_IVAR(dev);
	if (devi->npins < GPIOIIC_MIN_PINS) {
		device_printf(dev,
		    "gpioiic needs at least %d GPIO pins (only %d given).\n",
		    GPIOIIC_MIN_PINS, devi->npins);
		return (ENXIO);
	}
	device_set_desc(dev, "GPIO I2C bit-banging driver");

	return (BUS_PROBE_DEFAULT);
}

static int
gpioiic_attach(device_t dev)
{
	device_t		bitbang;
#ifdef FDT
	phandle_t		node;
	pcell_t			pin;
#endif
	struct gpiobus_ivar	*devi;
	struct gpioiic_softc	*sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_busdev = device_get_parent(dev);
	if (resource_int_value(device_get_name(dev),
		device_get_unit(dev), "scl", &sc->scl_pin))
		sc->scl_pin = GPIOIIC_SCL_DFLT;
	if (resource_int_value(device_get_name(dev),
		device_get_unit(dev), "sda", &sc->sda_pin))
		sc->sda_pin = GPIOIIC_SDA_DFLT;

#ifdef FDT
	if ((node = ofw_bus_get_node(dev)) == -1)
		return (ENXIO);
	if (OF_getencprop(node, "scl", &pin, sizeof(pin)) > 0)
		sc->scl_pin = (int)pin;
	if (OF_getencprop(node, "sda", &pin, sizeof(pin)) > 0)
		sc->sda_pin = (int)pin;
#endif

	if (sc->scl_pin < 0 || sc->scl_pin > 1)
		sc->scl_pin = GPIOIIC_SCL_DFLT;
	if (sc->sda_pin < 0 || sc->sda_pin > 1)
		sc->sda_pin = GPIOIIC_SDA_DFLT;

	devi = GPIOBUS_IVAR(dev);
	device_printf(dev, "SCL pin: %d, SDA pin: %d\n",
	    devi->pins[sc->scl_pin], devi->pins[sc->sda_pin]);

	/* add generic bit-banging code */
	bitbang = device_add_child(dev, "iicbb", -1);
	device_probe_and_attach(bitbang);

	return (0);
}

/*
 * Reset bus by setting SDA first and then SCL. 
 * Must always be called with gpio bus locked.
 */
static void
gpioiic_reset_bus(device_t dev)
{
	struct gpioiic_softc		*sc = device_get_softc(dev);

	GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->sda_pin,
	    GPIO_PIN_INPUT);
	GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->scl_pin,
	    GPIO_PIN_INPUT);
}

static void
gpioiic_setsda(device_t dev, int val)
{
	struct gpioiic_softc		*sc = device_get_softc(dev);

	if (val == 0) {
		GPIOBUS_PIN_SET(sc->sc_busdev, sc->sc_dev, sc->sda_pin, 0);
		GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->sda_pin,
		    GPIO_PIN_OUTPUT);
	} else {
		GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->sda_pin,
		    GPIO_PIN_INPUT);
	}
}

static void
gpioiic_setscl(device_t dev, int val)
{
	struct gpioiic_softc		*sc = device_get_softc(dev);

	if (val == 0) {
		GPIOBUS_PIN_SET(sc->sc_busdev, sc->sc_dev, sc->scl_pin, 0);
		GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->scl_pin,
		    GPIO_PIN_OUTPUT);
	} else {
		GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->scl_pin,
		    GPIO_PIN_INPUT);
	}
}

static int
gpioiic_getscl(device_t dev)
{
	struct gpioiic_softc		*sc = device_get_softc(dev);
	unsigned int			val;

	GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->scl_pin,
	    GPIO_PIN_INPUT);
	GPIOBUS_PIN_GET(sc->sc_busdev, sc->sc_dev, sc->scl_pin, &val);

	return ((int)val);
}

static int
gpioiic_getsda(device_t dev)
{
	struct gpioiic_softc		*sc = device_get_softc(dev);
	unsigned int			val;

	GPIOBUS_PIN_SETFLAGS(sc->sc_busdev, sc->sc_dev, sc->sda_pin,
	    GPIO_PIN_INPUT);
	GPIOBUS_PIN_GET(sc->sc_busdev, sc->sc_dev, sc->sda_pin, &val);

	return ((int)val);
}

static int
gpioiic_reset(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct gpioiic_softc		*sc;

	sc = device_get_softc(dev);
	gpioiic_reset_bus(sc->sc_dev);

	return (IIC_ENOADDR);
}

#ifdef FDT
static phandle_t
gpioiic_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the iicbb, which needs our own node. */
	return (ofw_bus_get_node(bus));
}
#endif

static devclass_t gpioiic_devclass;

static device_method_t gpioiic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gpioiic_probe),
	DEVMETHOD(device_attach,	gpioiic_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),

	/* iicbb interface */
	DEVMETHOD(iicbb_setsda,		gpioiic_setsda),
	DEVMETHOD(iicbb_setscl,		gpioiic_setscl),
	DEVMETHOD(iicbb_getsda,		gpioiic_getsda),
	DEVMETHOD(iicbb_getscl,		gpioiic_getscl),
	DEVMETHOD(iicbb_reset,		gpioiic_reset),

#ifdef FDT
	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_node,	gpioiic_get_node),
#endif

	DEVMETHOD_END
};

static driver_t gpioiic_driver = {
	"gpioiic",
	gpioiic_methods,
	sizeof(struct gpioiic_softc),
};

DRIVER_MODULE(gpioiic, gpiobus, gpioiic_driver, gpioiic_devclass, 0, 0);
DRIVER_MODULE(iicbb, gpioiic, iicbb_driver, iicbb_devclass, 0, 0);
MODULE_DEPEND(gpioiic, iicbb, IICBB_MINVER, IICBB_PREFVER, IICBB_MAXVER);
MODULE_DEPEND(gpioiic, gpiobus, 1, 1, 1);
