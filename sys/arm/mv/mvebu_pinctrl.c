/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Rubicon Communications, LLC (Netgate)
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
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/fdt_pinctrl.h>

#include "opt_soc.h"

#define	PINS_PER_REG	8
#define	BITS_PER_PIN	4
#define	PINS_MASK	0xf
#define	MAX_PIN_FUNC	5

struct mv_pins {
	const char	*name;
	const char	*functions[MAX_PIN_FUNC];
};

struct mv_padconf {
	const struct mv_pins	*pins;
	size_t		npins;
};

#ifdef SOC_MARVELL_8K
const static struct mv_pins ap806_pins[] = {
	{"mpp0", {"gpio", "sdio", NULL, "spi0"}},
	{"mpp1", {"gpio", "sdio", NULL, "spi0"}},
	{"mpp2", {"gpio", "sdio", NULL, "spi0"}},
	{"mpp3", {"gpio", "sdio", NULL, "spi0"}},
	{"mpp4", {"gpio", "sdio", NULL, "i2c0"}},
	{"mpp5", {"gpio", "sdio", NULL, "i2c0"}},
	{"mpp6", {"gpio", "sdio", NULL, NULL}},
	{"mpp7", {"gpio", "sdio", NULL, "uart1"}},
	{"mpp8", {"gpio", "sdio", NULL, "uart1"}},
	{"mpp9", {"gpio", "sdio", NULL, "spi0"}},
	{"mpp10", {"gpio", "sdio", NULL, NULL}},
	{"mpp11", {"gpio", NULL, NULL, "uart0"}},
	{"mpp12", {"gpio", "sdio", "sdio", NULL}},
	{"mpp13", {"gpio", NULL, NULL}},
	{"mpp14", {"gpio", NULL, NULL}},
	{"mpp15", {"gpio", NULL, NULL}},
	{"mpp16", {"gpio", NULL, NULL}},
	{"mpp17", {"gpio", NULL, NULL}},
	{"mpp18", {"gpio", NULL, NULL}},
	{"mpp19", {"gpio", NULL, NULL, "uart0", "sdio"}},
};

const struct mv_padconf ap806_padconf = {
	.npins = nitems(ap806_pins),
	.pins = ap806_pins,
};
#endif

struct mv_pinctrl_softc {
	device_t		dev;
	struct resource		*res;

	struct mv_padconf	*padconf;
};

static struct resource_spec mv_pinctrl_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
#ifdef SOC_MARVELL_8K
	{"marvell,ap806-pinctrl", (uintptr_t)&ap806_padconf},
#endif
	{NULL,             0}
};

#define	RD4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	WR4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static void
mv_pinctrl_configure_pin(struct mv_pinctrl_softc *sc, uint32_t pin,
    uint32_t function)
{
	uint32_t offset, shift, reg;

	offset = (pin / PINS_PER_REG) * BITS_PER_PIN;
	shift = (pin % PINS_PER_REG) * BITS_PER_PIN;
	reg = RD4(sc, offset);
	reg &= ~(PINS_MASK << shift);
	reg |= function << shift;
	WR4(sc, offset, reg);
}

static int
mv_pinctrl_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct mv_pinctrl_softc *sc;
	phandle_t node;
	char *function;
	const char **pins;
	int i, pin_num, pin_func, npins;

	sc = device_get_softc(dev);
	node = OF_node_from_xref(cfgxref);

	if (OF_getprop_alloc(node, "marvell,function",
	    (void **)&function) == -1)
		return (ENOMEM);

	npins = ofw_bus_string_list_to_array(node, "marvell,pins", &pins);
	if (npins == -1)
		return (ENOMEM);

	for (i = 0; i < npins; i++) {
		for (pin_num = 0; pin_num < sc->padconf->npins; pin_num++) {
			if (strcmp(pins[i], sc->padconf->pins[pin_num].name) == 0)
				break;
		}
		if (pin_num == sc->padconf->npins)
			continue;

		for (pin_func = 0; pin_func < MAX_PIN_FUNC; pin_func++)
			if (sc->padconf->pins[pin_num].functions[pin_func] &&
			    strcmp(function, sc->padconf->pins[pin_num].functions[pin_func]) == 0)
				break;

		if (pin_func == MAX_PIN_FUNC)
			continue;

		mv_pinctrl_configure_pin(sc, pin_num, pin_func);
	}

	OF_prop_free(pins);

	return (0);
}

static int
mv_pinctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell Pinctrl controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_pinctrl_attach(device_t dev)
{
	struct mv_pinctrl_softc *sc;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->padconf = (struct mv_padconf *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (bus_alloc_resources(dev, mv_pinctrl_res_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);

	fdt_pinctrl_register(dev, "marvell,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);
}

static int
mv_pinctrl_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t mv_pinctrl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_pinctrl_probe),
	DEVMETHOD(device_attach,	mv_pinctrl_attach),
	DEVMETHOD(device_detach,	mv_pinctrl_detach),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,mv_pinctrl_configure_pins),

	DEVMETHOD_END
};

static devclass_t mv_pinctrl_devclass;

static driver_t mv_pinctrl_driver = {
	"mv_pinctrl",
	mv_pinctrl_methods,
	sizeof(struct mv_pinctrl_softc),
};

EARLY_DRIVER_MODULE(mv_pinctrl, simplebus, mv_pinctrl_driver,
    mv_pinctrl_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
