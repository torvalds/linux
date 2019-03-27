/*-
 * Copyright (c) 2009 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/led/led.h>

#include "gpiobus_if.h"

struct gpioled
{
	struct gpioleds_softc	*parent_sc;
	gpio_pin_t		pin;
	struct cdev		*leddev;
};

struct gpioleds_softc
{
	device_t	sc_dev;
	device_t	sc_busdev;
	struct gpioled	*sc_leds;
	int		sc_total_leds;
};

static void gpioled_control(void *, int);
static int gpioled_probe(device_t);
static int gpioled_attach(device_t);
static int gpioled_detach(device_t);

static void
gpioled_control(void *priv, int onoff)
{
	struct gpioled *led;

	led = (struct gpioled *)priv;
	if (led->pin)
		gpio_pin_set_active(led->pin, onoff);
}

static void
gpioleds_attach_led(struct gpioleds_softc *sc, phandle_t node,
    struct gpioled *led)
{
	char *name;
	int state, err;
	char *default_state;

	led->parent_sc = sc;

	state = 0;
	if (OF_getprop_alloc(node, "default-state",
	    (void **)&default_state) != -1) {
		if (strcasecmp(default_state, "on") == 0)
			state = 1;
		else if (strcasecmp(default_state, "off") == 0)
			state = 0;
		else if (strcasecmp(default_state, "keep") == 0)
			state = -1;
		else {
			state = -1;
			device_printf(sc->sc_dev,
			    "unknown value for default-state in FDT\n");
		}
		OF_prop_free(default_state);
	}

	name = NULL;
	if (OF_getprop_alloc(node, "label", (void **)&name) == -1)
		OF_getprop_alloc(node, "name", (void **)&name);

	if (name == NULL) {
		device_printf(sc->sc_dev,
		    "no name provided for gpio LED, skipping\n");
		return;
	}

	err = gpio_pin_get_by_ofw_idx(sc->sc_dev, node, 0, &led->pin);
	if (err) {
		device_printf(sc->sc_dev, "<%s> failed to map pin\n", name);
		if (name)
			OF_prop_free(name);
		return;
	}
	gpio_pin_setflags(led->pin, GPIO_PIN_OUTPUT);

	led->leddev = led_create_state(gpioled_control, led, name,
	    state);

	if (name != NULL)
		OF_prop_free(name);
}

static void
gpioleds_detach_led(struct gpioled *led)
{

	if (led->leddev != NULL)
		led_destroy(led->leddev);

	if (led->pin)
		gpio_pin_release(led->pin);
}

static int
gpioled_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "gpio-leds"))
		return (ENXIO);

	device_set_desc(dev, "GPIO LEDs");

	return (BUS_PROBE_DEFAULT);
}

static int
gpioled_attach(device_t dev)
{
	struct gpioleds_softc *sc;
	phandle_t child, leds;
	int total_leds;

	if ((leds = ofw_bus_get_node(dev)) == -1)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_busdev = device_get_parent(dev);

	/* Traverse the 'gpio-leds' node and count leds */
	total_leds = 0;
	for (child = OF_child(leds); child != 0; child = OF_peer(child)) {
		if (!OF_hasprop(child, "gpios"))
			continue;
		total_leds++;
	}

	if (total_leds) {
		sc->sc_leds =  malloc(sizeof(struct gpioled) * total_leds,
		    M_DEVBUF, M_WAITOK | M_ZERO);

		sc->sc_total_leds = 0;
		/* Traverse the 'gpio-leds' node and count leds */
		for (child = OF_child(leds); child != 0; child = OF_peer(child)) {
			if (!OF_hasprop(child, "gpios"))
				continue;
			gpioleds_attach_led(sc, child, &sc->sc_leds[sc->sc_total_leds]);
			sc->sc_total_leds++;
		}
	}

	return (0);
}

static int
gpioled_detach(device_t dev)
{
	struct gpioleds_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->sc_total_leds; i++)
		gpioleds_detach_led(&sc->sc_leds[i]);

	if (sc->sc_leds)
		free(sc->sc_leds, M_DEVBUF);

	return (0);
}

static devclass_t gpioled_devclass;

static device_method_t gpioled_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gpioled_probe),
	DEVMETHOD(device_attach,	gpioled_attach),
	DEVMETHOD(device_detach,	gpioled_detach),

	DEVMETHOD_END
};

static driver_t gpioled_driver = {
	"gpioled",
	gpioled_methods,
	sizeof(struct gpioleds_softc),
};

DRIVER_MODULE(gpioled, ofwbus, gpioled_driver, gpioled_devclass, 0, 0);
DRIVER_MODULE(gpioled, simplebus, gpioled_driver, gpioled_devclass, 0, 0);
MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1);
