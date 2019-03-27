/*-
 * Copyright (c) 2015-2016 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/sysctl.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>

#include <dev/gpio/gpiobusvar.h>

struct gpiobacklight_softc 
{
	gpio_pin_t		sc_pin;
	struct sysctl_oid	*sc_oid;
	bool			sc_brightness;
};

static int gpiobacklight_sysctl(SYSCTL_HANDLER_ARGS);
static void gpiobacklight_update_brightness(struct gpiobacklight_softc *);
static int gpiobacklight_probe(device_t);
static int gpiobacklight_attach(device_t);
static int gpiobacklight_detach(device_t);

static void 
gpiobacklight_update_brightness(struct gpiobacklight_softc *sc)
{

	if (sc->sc_pin)
		gpio_pin_set_active(sc->sc_pin, sc->sc_brightness);
}

static int
gpiobacklight_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct gpiobacklight_softc *sc;
	int error;
	int brightness;

	sc = (struct gpiobacklight_softc*)arg1;

	brightness = sc->sc_brightness;
	error = sysctl_handle_int(oidp, &brightness, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	sc->sc_brightness = (brightness > 0);
	gpiobacklight_update_brightness(sc);

	return (0);
}

static int
gpiobacklight_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "gpio-backlight"))
		return (ENXIO);

	device_set_desc(dev, "GPIO backlight");

	return (0);
}

static int
gpiobacklight_attach(device_t dev)
{
	struct gpiobacklight_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	phandle_t node;

	sc = device_get_softc(dev);

	if ((node = ofw_bus_get_node(dev)) == -1)
		return (ENXIO);

	if (OF_hasprop(node, "default-on"))
		sc->sc_brightness = true;
	else
		sc->sc_brightness = false;

	gpio_pin_get_by_ofw_idx(dev, node, 0, &sc->sc_pin);
	if (sc->sc_pin == NULL) {
		device_printf(dev, "failed to map GPIO pin\n");
		return (ENXIO);
	}

	gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);

	gpiobacklight_update_brightness(sc);

	/* Init backlight interface */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	sc->sc_oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "brightness", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    gpiobacklight_sysctl, "I", "backlight brightness");

	return (0);
}

static int
gpiobacklight_detach(device_t dev)
{
	struct gpiobacklight_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_pin)
		gpio_pin_release(sc->sc_pin);

	return (0);
}

static devclass_t gpiobacklight_devclass;

static device_method_t gpiobacklight_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gpiobacklight_probe),
	DEVMETHOD(device_attach,	gpiobacklight_attach),
	DEVMETHOD(device_detach,	gpiobacklight_detach),

	DEVMETHOD_END
};

static driver_t gpiobacklight_driver = {
	"gpiobacklight",
	gpiobacklight_methods,
	sizeof(struct gpiobacklight_softc),
};

DRIVER_MODULE(gpiobacklight, simplebus, gpiobacklight_driver,
    gpiobacklight_devclass, 0, 0);
MODULE_DEPEND(gpiobacklight, gpiobus, 1, 1, 1);
