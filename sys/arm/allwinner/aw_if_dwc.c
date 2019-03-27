/*-
 * Copyright (c) 2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/dwc/if_dwc.h>
#include <dev/dwc/if_dwcvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/allwinner/aw_machdep.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>

#include "if_dwc_if.h"

static int
a20_if_dwc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "allwinner,sun7i-a20-gmac"))
		return (ENXIO);
	device_set_desc(dev, "A20 Gigabit Ethernet Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
a20_if_dwc_init(device_t dev)
{
	const char *tx_parent_name;
	char *phy_type;
	clk_t clk_tx, clk_tx_parent;
	regulator_t reg;
	phandle_t node;
	int error;

	node = ofw_bus_get_node(dev);

	/* Configure PHY for MII or RGMII mode */
	if (OF_getprop_alloc(node, "phy-mode", (void **)&phy_type)) {
		error = clk_get_by_ofw_name(dev, 0, "allwinner_gmac_tx", &clk_tx);
		if (error != 0) {
			device_printf(dev, "could not get tx clk\n");
			return (error);
		}

		if (strcmp(phy_type, "rgmii") == 0)
			tx_parent_name = "gmac_int_tx";
		else
			tx_parent_name = "mii_phy_tx";

		error = clk_get_by_name(dev, tx_parent_name, &clk_tx_parent);
		if (error != 0) {
			device_printf(dev, "could not get clock '%s'\n",
			    tx_parent_name);
			return (error);
		}

		error = clk_set_parent_by_clk(clk_tx, clk_tx_parent);
		if (error != 0) {
			device_printf(dev, "could not set tx clk parent\n");
			return (error);
		}
	}

	/* Enable PHY regulator if applicable */
	if (regulator_get_by_ofw_property(dev, 0, "phy-supply", &reg) == 0) {
		error = regulator_enable(reg);
		if (error != 0) {
			device_printf(dev, "could not enable PHY regulator\n");
			return (error);
		}
	}

	return (0);
}

static int
a20_if_dwc_mac_type(device_t dev)
{

	return (DWC_GMAC_ALT_DESC);
}

static int
a20_if_dwc_mii_clk(device_t dev)
{

	return (GMAC_MII_CLK_150_250M_DIV102);
}

static device_method_t a20_dwc_methods[] = {
	DEVMETHOD(device_probe,		a20_if_dwc_probe),

	DEVMETHOD(if_dwc_init,		a20_if_dwc_init),
	DEVMETHOD(if_dwc_mac_type,	a20_if_dwc_mac_type),
	DEVMETHOD(if_dwc_mii_clk,	a20_if_dwc_mii_clk),

	DEVMETHOD_END
};

static devclass_t a20_dwc_devclass;

extern driver_t dwc_driver;

DEFINE_CLASS_1(dwc, a20_dwc_driver, a20_dwc_methods, sizeof(struct dwc_softc),
    dwc_driver);
DRIVER_MODULE(a20_dwc, simplebus, a20_dwc_driver, a20_dwc_devclass, 0, 0);

MODULE_DEPEND(a20_dwc, dwc, 1, 1, 1);
