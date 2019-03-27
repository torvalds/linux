/*-
 * Copyright (c) 2015 Rubicon Communications, LLC (Netgate)
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

/*
 * GPIO driver for the ADI Engineering RCC-VE and RCC-DFF/DFFv2.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/gpio/gpiobusvar.h>
#include <isa/isavar.h>

#include "gpio_if.h"

#define	RCC_GPIO_BASE		0x500
#define	RCC_GPIO_USE_SEL	0x00
#define	RCC_GPIO_IO_SEL		0x04
#define	RCC_GPIO_GP_LVL		0x08

struct rcc_gpio_pin {
	uint32_t		pin;
	const char		*name;
	uint32_t		caps;
};

static struct rcc_gpio_pin rcc_pins[] = {
	{ .pin = (1 << 11), .name = "reset switch", .caps = GPIO_PIN_INPUT },
	{ .pin = (1 << 15), .name = "red LED", .caps = GPIO_PIN_OUTPUT },
	{ .pin = (1 << 17), .name = "green LED", .caps = GPIO_PIN_OUTPUT },
#if 0
	{ .pin = (1 << 16), .name = "HD1 LED", .caps = GPIO_PIN_OUTPUT },
	{ .pin = (1 << 18), .name = "HD2 LED", .caps = GPIO_PIN_OUTPUT },
#endif
};

struct rcc_gpio_softc {
	device_t		sc_dev;
	device_t		sc_busdev;
	struct mtx		sc_mtx;
	struct resource		*sc_io_res;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	uint32_t		sc_output;
	int			sc_io_rid;
	int			sc_gpio_npins;
};

#define	RCC_GPIO_LOCK(_sc)	mtx_lock(&(_sc)->sc_mtx)
#define	RCC_GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)
#define	RCC_WRITE(_sc, _off, _val)				\
	bus_space_write_4((_sc)->sc_bst, (_sc)->sc_bsh, _off, _val)
#define	RCC_READ(_sc, _off)					\
	bus_space_read_4((_sc)->sc_bst, (_sc)->sc_bsh, _off)

static void
rcc_gpio_modify_bits(struct rcc_gpio_softc *sc, uint32_t reg, uint32_t mask,
	uint32_t writebits)
{
	uint32_t value;

	RCC_GPIO_LOCK(sc);
	value = RCC_READ(sc, reg);
	value &= ~mask;
	value |= writebits;
	RCC_WRITE(sc, reg, value);
	RCC_GPIO_UNLOCK(sc);
}

static device_t
rcc_gpio_get_bus(device_t dev)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
rcc_gpio_pin_max(device_t dev, int *maxpin)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	*maxpin = sc->sc_gpio_npins - 1;

	return (0);
}

static int
rcc_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	*caps = rcc_pins[pin].caps;

	return (0);
}

static int
rcc_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	/* Flags cannot be changed. */
	*flags = rcc_pins[pin].caps;

	return (0);
}

static int
rcc_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	memcpy(name, rcc_pins[pin].name, GPIOMAXNAME);

	return (0);
}

static int
rcc_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	/* Flags cannot be changed - risk of short-circuit!!! */

	return (0);
}

static int
rcc_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	if ((rcc_pins[pin].caps & GPIO_PIN_OUTPUT) == 0)
		return (EINVAL);

	RCC_GPIO_LOCK(sc);
	if (value)
		sc->sc_output |= (1 << rcc_pins[pin].pin);
	else
		sc->sc_output &= ~(1 << rcc_pins[pin].pin);
	RCC_WRITE(sc, RCC_GPIO_GP_LVL, sc->sc_output);
	RCC_GPIO_UNLOCK(sc);

	return (0);
}

static int
rcc_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct rcc_gpio_softc *sc;
	uint32_t value;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	RCC_GPIO_LOCK(sc);
	if (rcc_pins[pin].caps & GPIO_PIN_INPUT)
		value = RCC_READ(sc, RCC_GPIO_GP_LVL);
	else
		value = sc->sc_output;
	RCC_GPIO_UNLOCK(sc);
	*val = (value & (1 << rcc_pins[pin].pin)) ? 1 : 0;

	return (0);
}

static int
rcc_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (pin >= sc->sc_gpio_npins)
		return (EINVAL);

	if ((rcc_pins[pin].caps & GPIO_PIN_OUTPUT) == 0)
		return (EINVAL);

	RCC_GPIO_LOCK(sc);
	if ((sc->sc_output & (1 << rcc_pins[pin].pin)) == 0)
		sc->sc_output |= (1 << rcc_pins[pin].pin);
	else
		sc->sc_output &= ~(1 << rcc_pins[pin].pin);
	RCC_WRITE(sc, RCC_GPIO_GP_LVL, sc->sc_output);
	RCC_GPIO_UNLOCK(sc);

	return (0);
}

static int
rcc_gpio_probe(device_t dev)
{
	char *prod;
	int port;

	/*
	 * We don't know of any PnP ID's for this GPIO controller.
	 */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	/*
	 * We have to have an IO port hint that is valid.
	 */
	port = isa_get_port(dev);
	if (port != RCC_GPIO_BASE)
		return (ENXIO);

	prod = kern_getenv("smbios.system.product");
	if (prod == NULL ||
	    (strcmp(prod, "RCC-VE") != 0 && strcmp(prod, "RCC-DFF") != 0))
		return (ENXIO);

	device_set_desc(dev, "RCC-VE/DFF GPIO controller");

	return (BUS_PROBE_DEFAULT);
}

static int
rcc_gpio_attach(device_t dev)
{
	int i;
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
 	sc->sc_dev = dev;

	/* Allocate IO resources. */
	sc->sc_io_rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->sc_io_rid, RF_ACTIVE);
	if (sc->sc_io_res == NULL) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}
	sc->sc_bst = rman_get_bustag(sc->sc_io_res);
	sc->sc_bsh = rman_get_bushandle(sc->sc_io_res);
	mtx_init(&sc->sc_mtx, "rcc-gpio", "gpio", MTX_DEF);

	/* Initialize the pins. */
	sc->sc_gpio_npins = nitems(rcc_pins);
	for (i = 0; i < sc->sc_gpio_npins; i++) {
		/* Enable it for GPIO. */
		rcc_gpio_modify_bits(sc, RCC_GPIO_USE_SEL, 0, rcc_pins[i].pin);
		/* Set the pin as input or output. */
		if (rcc_pins[i].caps & GPIO_PIN_OUTPUT)
			rcc_gpio_modify_bits(sc, RCC_GPIO_IO_SEL,
			    rcc_pins[i].pin, 0);
		else
			rcc_gpio_modify_bits(sc, RCC_GPIO_IO_SEL,
			    0, rcc_pins[i].pin);
	}
	RCC_WRITE(sc, RCC_GPIO_GP_LVL, sc->sc_output);

	/* Attach the gpiobus. */
	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_io_rid,
		    sc->sc_io_res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	return (0);
}

static int
rcc_gpio_detach(device_t dev)
{
	int i;
	struct rcc_gpio_softc *sc;

	sc = device_get_softc(dev);
	gpiobus_detach_bus(dev);

	/* Disable the GPIO function. */
	for (i = 0; i < sc->sc_gpio_npins; i++)
		rcc_gpio_modify_bits(sc, RCC_GPIO_USE_SEL, rcc_pins[i].pin, 0);

	if (sc->sc_io_res != NULL)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->sc_io_rid,
		    sc->sc_io_res);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static device_method_t rcc_gpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rcc_gpio_probe),
	DEVMETHOD(device_attach,	rcc_gpio_attach),
	DEVMETHOD(device_detach,	rcc_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		rcc_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		rcc_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	rcc_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags,	rcc_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps,	rcc_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags,	rcc_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		rcc_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		rcc_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	rcc_gpio_pin_toggle),

	DEVMETHOD_END
};

static devclass_t rcc_gpio_devclass;

static driver_t rcc_gpio_driver = {
	"gpio",
	rcc_gpio_methods,
	sizeof(struct rcc_gpio_softc),
};

DRIVER_MODULE(rcc_gpio, isa, rcc_gpio_driver, rcc_gpio_devclass, 0, 0);
MODULE_DEPEND(rcc_gpio, gpiobus, 1, 1, 1);
