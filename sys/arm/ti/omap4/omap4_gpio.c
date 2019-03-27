/*-
 * Copyright (c) 2011 Ben Gray <ben.r.gray@gmail.com>.
 * Copyright (c) 2014 Andrew Turner <andrew@FreeBSD.org>
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_cpuid.h>
#include <arm/ti/ti_gpio.h>
#include <arm/ti/ti_pinmux.h>

#include <arm/ti/omap4/omap4_scm_padconf.h>

#include "ti_gpio_if.h"

static struct ofw_compat_data compat_data[] = {
	{"ti,omap4-gpio",	1},
	{"ti,gpio",		1},
	{NULL,			0},
};

static int
omap4_gpio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);
	if (ti_chip() != CHIP_OMAP_4)
		return (ENXIO);

	device_set_desc(dev, "TI OMAP4 General Purpose I/O (GPIO)");

	return (0);
}

static int
omap4_gpio_set_flags(device_t dev, uint32_t gpio, uint32_t flags)
{
	unsigned int state = 0;
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);
	/* First the SCM driver needs to be told to put the pad into GPIO mode */
	if (flags & GPIO_PIN_OUTPUT)
		state = PADCONF_PIN_OUTPUT;
	else if (flags & GPIO_PIN_INPUT) {
		if (flags & GPIO_PIN_PULLUP)
			state = PADCONF_PIN_INPUT_PULLUP;
		else if (flags & GPIO_PIN_PULLDOWN)
			state = PADCONF_PIN_INPUT_PULLDOWN;
		else
			state = PADCONF_PIN_INPUT;
	}
	return ti_pinmux_padconf_set_gpiomode((sc->sc_bank-1)*32 + gpio, state);
}

static int
omap4_gpio_get_flags(device_t dev, uint32_t gpio, uint32_t *flags)
{
	unsigned int state;
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);

	/* Get the current pin state */
	if (ti_pinmux_padconf_get_gpiomode((sc->sc_bank-1)*32 + gpio, &state) != 0) {
		*flags = 0;
		return (EINVAL);
	} else {
		switch (state) {
			case PADCONF_PIN_OUTPUT:
				*flags = GPIO_PIN_OUTPUT;
				break;
			case PADCONF_PIN_INPUT:
				*flags = GPIO_PIN_INPUT;
				break;
			case PADCONF_PIN_INPUT_PULLUP:
				*flags = GPIO_PIN_INPUT | GPIO_PIN_PULLUP;
				break;
			case PADCONF_PIN_INPUT_PULLDOWN:
				*flags = GPIO_PIN_INPUT | GPIO_PIN_PULLDOWN;
				break;
			default:
				*flags = 0;
				break;
		}
	}

	return (0);
}

static device_method_t omap4_gpio_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, omap4_gpio_probe),

	/* ti_gpio interface */
	DEVMETHOD(ti_gpio_set_flags, omap4_gpio_set_flags),
	DEVMETHOD(ti_gpio_get_flags, omap4_gpio_get_flags),

	DEVMETHOD_END
};

extern driver_t ti_gpio_driver;
static devclass_t omap4_gpio_devclass;

DEFINE_CLASS_1(gpio, omap4_gpio_driver, omap4_gpio_methods,
    sizeof(struct ti_gpio_softc), ti_gpio_driver);
DRIVER_MODULE(omap4_gpio, simplebus, omap4_gpio_driver, omap4_gpio_devclass,
    0, 0);
