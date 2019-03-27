/*-
 * Copyright (c) 2017 Justin Hibbits
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <dev/led/led.h>

#include "gpio_if.h"

struct rbled_softc {
	struct cdev	*sc_led;
	device_t	 sc_gpio;
	uint32_t	 sc_ledpin;
};

static int	rbled_probe(device_t);
static int	rbled_attach(device_t);
static int	rbled_detach(device_t);
static void	rbled_toggle(void *, int);

static device_method_t  rbled_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rbled_probe),
	DEVMETHOD(device_attach,	rbled_attach),
        DEVMETHOD(device_detach,        rbled_detach),

	DEVMETHOD_END
};

static driver_t rbled_driver = {
	"rbled",
	rbled_methods,
	sizeof(struct rbled_softc),
};

static devclass_t rbled_devclass;

DRIVER_MODULE(rbled, simplebus, rbled_driver, rbled_devclass, 0, 0);

static int
rbled_probe(device_t dev)
{
	phandle_t node;
	const char *name;
	cell_t gp[2];
	char model[6];

	node = ofw_bus_get_node(dev);

	name = ofw_bus_get_name(dev);
	if (name == NULL)
		return (ENXIO);
	if (strcmp(name, "led") != 0)
		return (ENXIO);

	if (OF_getprop(node, "user_led", gp, sizeof(gp)) <= 0)
		return (ENXIO);

	/* Check root model. */
	node = OF_peer(0);
	if (OF_getprop(node, "model", model, sizeof(model)) <= 0)
		return (ENXIO);
	if (strcmp(model, "RB800") != 0)
		return (ENXIO);

	device_set_desc(dev, "RouterBoard LED");
	return (0);
}

static int
rbled_attach(device_t dev)
{
	struct rbled_softc *sc;
	phandle_t node;
	cell_t gp[2];

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	if (OF_getprop(node, "user_led", gp, sizeof(gp)) <= 0)
		return (ENXIO);
	
	sc->sc_gpio = OF_device_from_xref(gp[0]);
	if (sc->sc_gpio == NULL) {
		device_printf(dev, "No GPIO resource found!\n");
		return (ENXIO);
	}
	sc->sc_ledpin = gp[1];

	sc->sc_led = led_create(rbled_toggle, sc, "user_led");

	if (sc->sc_led == NULL)
		return (ENXIO);

	return (0);
}

static int
rbled_detach(device_t dev)
{
	struct rbled_softc *sc;

	sc = device_get_softc(dev);
	led_destroy(sc->sc_led);

	return (0);
}

static void
rbled_toggle(void *priv, int onoff)
{
	struct rbled_softc *sc = priv;

	GPIO_PIN_SET(sc->sc_gpio, sc->sc_ledpin, onoff);
}
