/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726 GPIO driver.
 *
 * Note: The OEN register is active * low *.  Setting a bit to zero
 * enables the output driver, setting a bit to one disables the driver.
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

#include <sys/gpio.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>

#include "gpio_if.h"


struct aml8726_gpio_softc {
	device_t	dev;
	struct resource *res[3];
	struct mtx	mtx;
	uint32_t	npins;
	device_t	busdev;
};

static struct resource_spec aml8726_gpio_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE | RF_SHAREABLE }, /* oen */
	{ SYS_RES_MEMORY, 1, RF_ACTIVE | RF_SHAREABLE }, /* output */
	{ SYS_RES_MEMORY, 2, RF_ACTIVE }, /* input */
	{ -1, 0 }
};

#define	AML_GPIO_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_GPIO_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_GPIO_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "gpio", MTX_DEF)
#define	AML_GPIO_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	AML_GPIO_OE_N_REG		0
#define	AML_GPIO_OUT_REG		1
#define	AML_GPIO_IN_REG			2

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[reg], 0, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[reg], 0)

static int
aml8726_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-gpio"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 GPIO");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_gpio_attach(device_t dev)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	phandle_t node;
	pcell_t prop;

	sc->dev = dev;

	node = ofw_bus_get_node(dev);

	if (OF_getencprop(node, "pin-count",
	    &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing pin-count attribute in FDT\n");
		return (ENXIO);
	}
	sc->npins = prop;

	if (sc->npins > 32)
		return (ENXIO);

	if (bus_alloc_resources(dev, aml8726_gpio_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	/*
	 * The GPIOAO OUT bits occupy the upper word of the OEN register.
	 */
	if (rman_get_start(sc->res[1]) == rman_get_start(sc->res[0]))
	  if (sc->npins > 16) {
		device_printf(dev,
		    "too many pins for overlapping OEN and OUT\n");
		bus_release_resources(dev, aml8726_gpio_spec, sc->res);
		return (ENXIO);
		}

	AML_GPIO_LOCK_INIT(sc);

	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		AML_GPIO_LOCK_DESTROY(sc);
		bus_release_resources(dev, aml8726_gpio_spec, sc->res);
		return (ENXIO);
	}

	return (0);
}

static int
aml8726_gpio_detach(device_t dev)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);

	gpiobus_detach_bus(dev);

	AML_GPIO_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_gpio_spec, sc->res);

	return (0);
}

static device_t
aml8726_gpio_get_bus(device_t dev)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
aml8726_gpio_pin_max(device_t dev, int *maxpin)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);

	*maxpin = (int)sc->npins;

	return (0);
}

/* Get a specific pin's capabilities. */
static int
aml8726_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	*caps = (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT);

	return (0);
}

/* Get a specific pin's name. */
static int
aml8726_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	snprintf(name, GPIOMAXNAME, "%s.%u", ofw_bus_get_name(dev), pin);

	return (0);
}

/* Get a specific pin's current in/out state. */
static int
aml8726_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	uint32_t mask = 1U << pin;

	if (pin >= sc->npins)
		return (EINVAL);

	if ((CSR_READ_4(sc, AML_GPIO_OE_N_REG) & mask) == 0) {
		/* output */
		*flags = GPIO_PIN_OUTPUT;
	} else
		/* input */
		*flags = GPIO_PIN_INPUT;

	return (0);
}

/* Set a specific pin's in/out state. */
static int
aml8726_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	uint32_t mask = 1U << pin;

	if (pin >= sc->npins)
		return (EINVAL);

	AML_GPIO_LOCK(sc);

	if ((flags & GPIO_PIN_OUTPUT) != 0) {
		/* Output.  Turn on driver.  */
		CSR_WRITE_4(sc, AML_GPIO_OE_N_REG,
		    (CSR_READ_4(sc, AML_GPIO_OE_N_REG) & ~mask));
	} else {
		/* Input.  Turn off driver. */
		CSR_WRITE_4(sc, AML_GPIO_OE_N_REG,
		    (CSR_READ_4(sc, AML_GPIO_OE_N_REG) | mask));
	}
		
	AML_GPIO_UNLOCK(sc);

	return (0);
}

/* Set a specific output pin's value. */
static int
aml8726_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	uint32_t mask;

	if (pin >= sc->npins || value > 1)
		return (EINVAL);

	/*
	 * The GPIOAO OUT bits occupy the upper word of the OEN register.
	 */
	if (rman_get_start(sc->res[1]) == rman_get_start(sc->res[0]))
		pin += 16;

	mask = 1U << pin;

	AML_GPIO_LOCK(sc);

	CSR_WRITE_4(sc, AML_GPIO_OUT_REG,
	    ((CSR_READ_4(sc, AML_GPIO_OUT_REG) & ~mask) | (value << pin)));

	AML_GPIO_UNLOCK(sc);

	return (0);
}

/* Get a specific pin's input value. */
static int
aml8726_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *value)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	uint32_t mask = 1U << pin;

	if (pin >= sc->npins)
		return (EINVAL);

	*value = (CSR_READ_4(sc, AML_GPIO_IN_REG) & mask) ? 1 : 0;

	return (0);
}

/* Toggle a pin's output value. */
static int
aml8726_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct aml8726_gpio_softc *sc = device_get_softc(dev);
	uint32_t mask;

	if (pin >= sc->npins)
		return (EINVAL);

	/*
	 * The GPIOAO OUT bits occupy the upper word of the OEN register.
	 */
	if (rman_get_start(sc->res[1]) == rman_get_start(sc->res[0]))
		pin += 16;

	mask = 1U << pin;

	AML_GPIO_LOCK(sc);

	CSR_WRITE_4(sc, AML_GPIO_OUT_REG,
	    CSR_READ_4(sc, AML_GPIO_OUT_REG) ^ mask);

	AML_GPIO_UNLOCK(sc);

	return (0);
}

static phandle_t
aml8726_gpio_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t aml8726_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_gpio_probe),
	DEVMETHOD(device_attach,	aml8726_gpio_attach),
	DEVMETHOD(device_detach,	aml8726_gpio_detach),

	/* GPIO interface */
	DEVMETHOD(gpio_get_bus,		aml8726_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		aml8726_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	aml8726_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	aml8726_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	aml8726_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	aml8726_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		aml8726_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		aml8726_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	aml8726_gpio_pin_toggle),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	aml8726_gpio_get_node),

	DEVMETHOD_END
};

static driver_t aml8726_gpio_driver = {
	"gpio",
	aml8726_gpio_methods,
	sizeof(struct aml8726_gpio_softc),
};

static devclass_t aml8726_gpio_devclass;

DRIVER_MODULE(aml8726_gpio, simplebus, aml8726_gpio_driver,
    aml8726_gpio_devclass, 0, 0);
