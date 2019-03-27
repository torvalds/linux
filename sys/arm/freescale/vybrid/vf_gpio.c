/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family General-Purpose Input/Output (GPIO)
 * Chapter 7, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <sys/mutex.h>
#include <sys/gpio.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include "gpio_if.h"

#include <arm/freescale/vybrid/vf_common.h>
#include <arm/freescale/vybrid/vf_port.h>

#define	GPIO_PDOR(n)	(0x00 + 0x40 * (n >> 5))
#define	GPIO_PSOR(n)	(0x04 + 0x40 * (n >> 5))
#define	GPIO_PCOR(n)	(0x08 + 0x40 * (n >> 5))
#define	GPIO_PTOR(n)	(0x0C + 0x40 * (n >> 5))
#define	GPIO_PDIR(n)	(0x10 + 0x40 * (n >> 5))

#define	GPIO_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)

#define	DEFAULT_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)

/*
 * GPIO interface
 */
static device_t vf_gpio_get_bus(device_t);
static int vf_gpio_pin_max(device_t, int *);
static int vf_gpio_pin_getcaps(device_t, uint32_t, uint32_t *);
static int vf_gpio_pin_getname(device_t, uint32_t, char *);
static int vf_gpio_pin_getflags(device_t, uint32_t, uint32_t *);
static int vf_gpio_pin_setflags(device_t, uint32_t, uint32_t);
static int vf_gpio_pin_set(device_t, uint32_t, unsigned int);
static int vf_gpio_pin_get(device_t, uint32_t, unsigned int *);
static int vf_gpio_pin_toggle(device_t, uint32_t pin);

struct vf_gpio_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;

	device_t		sc_busdev;
	struct mtx		sc_mtx;
	int			gpio_npins;
	struct gpio_pin		gpio_pins[NGPIO];
};

struct vf_gpio_softc *gpio_sc;

static struct resource_spec vf_gpio_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
vf_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-gpio"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family GPIO Unit");
	return (BUS_PROBE_DEFAULT);
}

static int
vf_gpio_attach(device_t dev)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	if (bus_alloc_resources(dev, vf_gpio_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	gpio_sc = sc;

	sc->gpio_npins = NGPIO;

	for (i = 0; i < sc->gpio_npins; i++) {
		sc->gpio_pins[i].gp_pin = i;
		sc->gpio_pins[i].gp_caps = DEFAULT_CAPS;
		sc->gpio_pins[i].gp_flags =
		    (READ4(sc, GPIO_PDOR(i)) & (1 << (i % 32))) ?
		    GPIO_PIN_OUTPUT: GPIO_PIN_INPUT;
		snprintf(sc->gpio_pins[i].gp_name, GPIOMAXNAME,
		    "vf_gpio%d.%d", device_get_unit(dev), i);
	}

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		bus_release_resources(dev, vf_gpio_spec, sc->res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	return (0);
}

static device_t
vf_gpio_get_bus(device_t dev)
{
	struct vf_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

static int
vf_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = NGPIO - 1;
	return (0);
}

static int
vf_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	memcpy(name, sc->gpio_pins[i].gp_name, GPIOMAXNAME);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
vf_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*caps = sc->gpio_pins[i].gp_caps;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
vf_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*flags = sc->gpio_pins[i].gp_flags;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
vf_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	*val = (READ4(sc, GPIO_PDIR(i)) & (1 << (i % 32))) ? 1 : 0;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
vf_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	WRITE4(sc, GPIO_PTOR(i), (1 << (i % 32)));
	GPIO_UNLOCK(sc);

	return (0);
}


static void
vf_gpio_pin_configure(struct vf_gpio_softc *sc, struct gpio_pin *pin,
    unsigned int flags)
{

	GPIO_LOCK(sc);

	/*
	 * Manage input/output
	 */
	if (flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) {
		pin->gp_flags &= ~(GPIO_PIN_INPUT|GPIO_PIN_OUTPUT);
		if (flags & GPIO_PIN_OUTPUT) {
			pin->gp_flags |= GPIO_PIN_OUTPUT;

		} else {
			pin->gp_flags |= GPIO_PIN_INPUT;
			WRITE4(sc, GPIO_PCOR(pin->gp_pin),
			    (1 << (pin->gp_pin % 32)));
		}
	}

	GPIO_UNLOCK(sc);
}


static int
vf_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	vf_gpio_pin_configure(sc, &sc->gpio_pins[i], flags);

	return (0);
}

static int
vf_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct vf_gpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->gpio_npins; i++) {
		if (sc->gpio_pins[i].gp_pin == pin)
			break;
	}

	if (i >= sc->gpio_npins)
		return (EINVAL);

	GPIO_LOCK(sc);
	if (value)
		WRITE4(sc, GPIO_PSOR(i), (1 << (i % 32)));
	else
		WRITE4(sc, GPIO_PCOR(i), (1 << (i % 32)));
	GPIO_UNLOCK(sc);

	return (0);
}

static device_method_t vf_gpio_methods[] = {
	DEVMETHOD(device_probe,		vf_gpio_probe),
	DEVMETHOD(device_attach,	vf_gpio_attach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus,		vf_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		vf_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	vf_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getcaps,	vf_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_getflags,	vf_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_get,		vf_gpio_pin_get),
	DEVMETHOD(gpio_pin_toggle,	vf_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_setflags,	vf_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_set,		vf_gpio_pin_set),
	{ 0, 0 }
};

static driver_t vf_gpio_driver = {
	"gpio",
	vf_gpio_methods,
	sizeof(struct vf_gpio_softc),
};

static devclass_t vf_gpio_devclass;

DRIVER_MODULE(vf_gpio, simplebus, vf_gpio_driver, vf_gpio_devclass, 0, 0);
