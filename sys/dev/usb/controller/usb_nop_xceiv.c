/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Rubicon Communications, LLC (Netgate)
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/extres/phy/phy_usb.h>

#include "phynode_if.h"

struct usb_nop_xceiv_softc {
	device_t		dev;
	regulator_t		vcc_supply;
	clk_t			clk;
	uint32_t		clk_freq;
};

static struct ofw_compat_data compat_data[] = {
	{"usb-nop-xceiv", 1},
	{NULL,            0}
};

/* Phy class and methods. */
static int usb_nop_xceiv_phy_enable(struct phynode *phy, bool enable);
static phynode_usb_method_t usb_nop_xceiv_phynode_methods[] = {
	PHYNODEMETHOD(phynode_enable, usb_nop_xceiv_phy_enable),

	PHYNODEMETHOD_END
};
DEFINE_CLASS_1(usb_nop_xceiv_phynode, usb_nop_xceiv_phynode_class,
    usb_nop_xceiv_phynode_methods,
    sizeof(struct phynode_usb_sc), phynode_usb_class);

static int
usb_nop_xceiv_phy_enable(struct phynode *phynode, bool enable)
{
	struct usb_nop_xceiv_softc *sc;
	device_t dev;
	intptr_t phy;
	int error;

	dev = phynode_get_device(phynode);
	phy = phynode_get_id(phynode);
	sc = device_get_softc(dev);

	if (phy != 0)
		return (ERANGE);

	/* Enable the phy clock */
	if (sc->clk_freq != 0) {
		if (enable) {
			error = clk_set_freq(sc->clk, sc->clk_freq,
			  CLK_SET_ROUND_ANY);
			if (error != 0) {
				device_printf(dev, "Cannot set clock to %dMhz\n",
				  sc->clk_freq);
				goto fail;
			}

			error = clk_enable(sc->clk);
		} else
			error = clk_disable(sc->clk);

		if (error != 0) {
			device_printf(dev, "Cannot %sable the clock\n",
			    enable ? "En" : "Dis");
			goto fail;
		}
	}
	if (sc->vcc_supply) {
		if (enable)
			error = regulator_enable(sc->vcc_supply);
		else
			error = regulator_disable(sc->vcc_supply);
		if (error != 0) {
			device_printf(dev, "Cannot %sable the regulator\n",
			    enable ? "En" : "Dis");
			goto fail;
		}
	}

	return (0);

fail:
	return (ENXIO);
}

static int
usb_nop_xceiv_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "USB NOP PHY");
	return (BUS_PROBE_DEFAULT);
}

static int
usb_nop_xceiv_attach(device_t dev)
{
	struct usb_nop_xceiv_softc *sc;
	struct phynode *phynode;
	struct phynode_init_def phy_init;
	phandle_t node;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	/* Parse the optional properties */
	OF_getencprop(node, "clock-frequency", &sc->clk_freq, sizeof(uint32_t));

	error = clk_get_by_ofw_name(dev, node, "main_clk", &sc->clk);
	if (error != 0 && sc->clk_freq != 0) {
		device_printf(dev, "clock property is mandatory if clock-frequency is present\n");
		return (ENXIO);
	}

	regulator_get_by_ofw_property(dev, node, "vcc-supply", &sc->vcc_supply);

	phy_init.id = 0;
	phy_init.ofw_node = node;
	phynode = phynode_create(dev, &usb_nop_xceiv_phynode_class,
	    &phy_init);
	if (phynode == NULL) {
		device_printf(dev, "failed to create USB NOP PHY\n");
		return (ENXIO);
	}
	if (phynode_register(phynode) == NULL) {
		device_printf(dev, "failed to create USB NOP PHY\n");
		return (ENXIO);
	}

	OF_device_register_xref(OF_xref_from_node(node), dev);

	return (0);
}

static int
usb_nop_xceiv_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t usb_nop_xceiv_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, usb_nop_xceiv_probe),
	DEVMETHOD(device_attach, usb_nop_xceiv_attach),
	DEVMETHOD(device_detach, usb_nop_xceiv_detach),

	DEVMETHOD_END
};

static devclass_t usb_nop_xceiv_devclass;

static driver_t usb_nop_xceiv_driver = {
	"usb_nop_xceiv",
	usb_nop_xceiv_methods,
	sizeof(struct usb_nop_xceiv_softc),
};

EARLY_DRIVER_MODULE(usb_nop_xceiv, simplebus, usb_nop_xceiv_driver,
    usb_nop_xceiv_devclass, 0, 0, BUS_PASS_SUPPORTDEV + BUS_PASS_ORDER_MIDDLE);
