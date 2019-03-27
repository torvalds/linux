/*-
 * Copyright (c) 2015 Justin Hibbits
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "gpio_if.h"

#define MAXPIN		(31)

#define VALID_PIN(u)	((u) >= 0 && (u) <= MAXPIN)

#define GPIO_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define	GPIO_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define GPIO_LOCK_INIT(sc) \
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->dev),	\
	    "gpio", MTX_DEF)
#define GPIO_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);

#define	GPIO_GPDIR	0x0
#define	GPIO_GPODR	0x4
#define	GPIO_GPDAT	0x8
#define	GPIO_GPIER	0xc
#define	GPIO_GPIMR	0x10
#define	GPIO_GPICR	0x14


struct qoriq_gpio_softc {
	device_t	dev;
	device_t	busdev;
	struct mtx	sc_mtx;
	struct resource *sc_mem;	/* Memory resource */
};

static device_t
qoriq_gpio_get_bus(device_t dev)
{
	struct qoriq_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
qoriq_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = MAXPIN;
	return (0);
}

/* Get a specific pin's capabilities. */
static int
qoriq_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{

	if (!VALID_PIN(pin))
		return (EINVAL);

	*caps = (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN);

	return (0);
}

/* Get a specific pin's name. */
static int
qoriq_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{

	if (!VALID_PIN(pin))
		return (EINVAL);

	snprintf(name, GPIOMAXNAME, "qoriq_gpio%d.%d",
	    device_get_unit(dev), pin);
	name[GPIOMAXNAME-1] = '\0';

	return (0);
}

/* Set flags for the pin. */
static int
qoriq_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);
	uint32_t reg;

	if (!VALID_PIN(pin))
		return (EINVAL);

	if ((flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) ==
	    (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT))
		return (EINVAL);

	GPIO_LOCK(sc);
	if (flags & GPIO_PIN_INPUT) {
		reg = bus_read_4(sc->sc_mem, GPIO_GPDIR);
		reg &= ~(1 << (31 - pin));
		bus_write_4(sc->sc_mem, GPIO_GPDIR, reg);
	}
	else if (flags & GPIO_PIN_OUTPUT) {
		reg = bus_read_4(sc->sc_mem, GPIO_GPDIR);
		reg |= (1 << (31 - pin));
		bus_write_4(sc->sc_mem, GPIO_GPDIR, reg);
		reg = bus_read_4(sc->sc_mem, GPIO_GPODR);
		if (flags & GPIO_PIN_OPENDRAIN)
			reg |= (1 << (31 - pin));
		else
			reg &= ~(1 << (31 - pin));
		bus_write_4(sc->sc_mem, GPIO_GPODR, reg);
	}
	GPIO_UNLOCK(sc);
	return (0);
}

/* Set a specific output pin's value. */
static int
qoriq_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);
	uint32_t outvals;
	uint8_t pinbit;

	if (!VALID_PIN(pin) || value > 1)
		return (EINVAL);

	GPIO_LOCK(sc);
	pinbit = 31 - pin;

	outvals = bus_read_4(sc->sc_mem, GPIO_GPDAT);
	outvals &= ~(1 << pinbit);
	outvals |= (value << pinbit);
	bus_write_4(sc->sc_mem, GPIO_GPDAT, outvals);

	GPIO_UNLOCK(sc);

	return (0);
}

/* Get a specific pin's input value. */
static int
qoriq_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *value)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);

	if (!VALID_PIN(pin))
		return (EINVAL);

	*value = (bus_read_4(sc->sc_mem, GPIO_GPDAT) >> (31 - pin)) & 1;

	return (0);
}

/* Toggle a pin's output value. */
static int
qoriq_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);
	uint32_t val;

	if (!VALID_PIN(pin))
		return (EINVAL);

	GPIO_LOCK(sc);

	val = bus_read_4(sc->sc_mem, GPIO_GPDAT);
	val ^= (1 << (31 - pin));
	bus_write_4(sc->sc_mem, GPIO_GPDAT, val);
	
	GPIO_UNLOCK(sc);

	return (0);
}

static struct ofw_compat_data gpio_matches[] = {
    {"fsl,qoriq-gpio", 1},
    {"fsl,pq3-gpio", 1},
    {"fsl,mpc8572-gpio", 1},
    {0, 0}
};

static int
qoriq_gpio_probe(device_t dev)
{

	if (ofw_bus_search_compatible(dev, gpio_matches)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Freescale QorIQ GPIO driver");

	return (0);
}

static int qoriq_gpio_detach(device_t dev);

static int
qoriq_gpio_attach(device_t dev)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);
	int rid;

	sc->dev = dev;

	GPIO_LOCK_INIT(sc);

	/* Allocate memory. */
	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev,
		     SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(dev, "Can't allocate memory for device output port");
		qoriq_gpio_detach(dev);
		return (ENOMEM);
	}

	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		qoriq_gpio_detach(dev);
		return (ENOMEM);
	}

	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), dev);

	return (0);
}

static int
qoriq_gpio_detach(device_t dev)
{
	struct qoriq_gpio_softc *sc = device_get_softc(dev);

	gpiobus_detach_bus(dev);

	if (sc->sc_mem != NULL) {
		/* Release output port resource. */
		bus_release_resource(dev, SYS_RES_MEMORY,
				     rman_get_rid(sc->sc_mem), sc->sc_mem);
	}

	GPIO_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t qoriq_gpio_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe, 	qoriq_gpio_probe),
	DEVMETHOD(device_attach, 	qoriq_gpio_attach),
	DEVMETHOD(device_detach, 	qoriq_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus, 	qoriq_gpio_get_bus),
	DEVMETHOD(gpio_pin_max, 	qoriq_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname, 	qoriq_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getcaps, 	qoriq_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_get, 	qoriq_gpio_pin_get),
	DEVMETHOD(gpio_pin_set, 	qoriq_gpio_pin_set),
	DEVMETHOD(gpio_pin_setflags, 	qoriq_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_toggle, 	qoriq_gpio_pin_toggle),

	DEVMETHOD_END
};

static driver_t qoriq_gpio_driver = {
	"gpio",
	qoriq_gpio_methods,
	sizeof(struct qoriq_gpio_softc),
};
static devclass_t qoriq_gpio_devclass;

EARLY_DRIVER_MODULE(qoriq_gpio, simplebus, qoriq_gpio_driver,
    qoriq_gpio_devclass, NULL, NULL,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);
