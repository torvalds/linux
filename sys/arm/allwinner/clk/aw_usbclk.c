/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * $FreeBSD$
 */

/*
 * Allwinner USB clocks
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_subr.h>

#include <dev/extres/clk/clk_gate.h>
#include <dev/extres/hwreset/hwreset.h>

#include "clkdev_if.h"
#include "hwreset_if.h"

#define	A10_SCLK_GATING_USBPHY	(1 << 8)
#define	A10_SCLK_GATING_OHCI1	(1 << 7)
#define	A10_SCLK_GATING_OHCI0	(1 << 6)

#define	USBPHY2_RST		(1 << 2)
#define	USBPHY1_RST		(1 << 1)
#define	USBPHY0_RST		(1 << 0)

enum aw_usbclk_type {
	AW_A10_USBCLK = 1,
	AW_A13_USBCLK,
	AW_A31_USBCLK,
	AW_A83T_USBCLK,
	AW_H3_USBCLK,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-usb-clk",	AW_A10_USBCLK },
	{ "allwinner,sun5i-a13-usb-clk",	AW_A13_USBCLK },
	{ "allwinner,sun6i-a31-usb-clk",	AW_A31_USBCLK },
	{ "allwinner,sun8i-a83t-usb-clk",	AW_A83T_USBCLK },
	{ "allwinner,sun8i-h3-usb-clk",		AW_H3_USBCLK },
	{ NULL, 0 }
};

/* Clock indices for A10, as there is no clock-indices property in the DT */
static uint32_t aw_usbclk_indices_a10[] = { 6, 7, 8 };
/* Clock indices for H3, as there is no clock-indices property in the DT */
static uint32_t aw_usbclk_indices_h3[] = { 8, 9, 10, 11, 16, 17, 18, 19 };

struct aw_usbclk_softc {
	bus_addr_t	reg;
};

static int
aw_usbclk_hwreset_assert(device_t dev, intptr_t id, bool value)
{
	struct aw_usbclk_softc *sc;
	uint32_t mask;
	device_t pdev;
	int error;

	sc = device_get_softc(dev);
	pdev = device_get_parent(dev);

	mask = USBPHY0_RST << id;

	CLKDEV_DEVICE_LOCK(pdev);
	error = CLKDEV_MODIFY_4(pdev, sc->reg, mask, value ? 0 : mask);
	CLKDEV_DEVICE_UNLOCK(pdev);

	return (error);
}

static int
aw_usbclk_hwreset_is_asserted(device_t dev, intptr_t id, bool *value)
{
	struct aw_usbclk_softc *sc;
	uint32_t mask, val;
	device_t pdev;
	int error;

	sc = device_get_softc(dev);
	pdev = device_get_parent(dev);

	mask = USBPHY0_RST << id;

	CLKDEV_DEVICE_LOCK(pdev);
	error = CLKDEV_READ_4(pdev, sc->reg, &val);
	CLKDEV_DEVICE_UNLOCK(pdev);

	if (error)
		return (error);

	*value = (val & mask) != 0 ? false : true;

	return (0);
}

static int
aw_usbclk_create(device_t dev, bus_addr_t paddr, struct clkdom *clkdom,
    const char *pclkname, const char *clkname, int index)
{
	const char *parent_names[1] = { pclkname };
	struct clk_gate_def def;

	memset(&def, 0, sizeof(def));
	def.clkdef.id = index;
	def.clkdef.name = clkname;
	def.clkdef.parent_names = parent_names;
	def.clkdef.parent_cnt = 1;
	def.offset = paddr;
	def.shift = index;
	def.mask = 1;
	def.on_value = 1;
	def.off_value = 0;

	return (clknode_gate_register(clkdom, &def));
}

static int
aw_usbclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner USB Clocks");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_usbclk_attach(device_t dev)
{
	struct aw_usbclk_softc *sc;
	struct clkdom *clkdom;
	const char **names;
	const char *pname;
	int index, nout, error;
	enum aw_usbclk_type type;
	uint32_t *indices;
	clk_t clk_parent, clk_parent_pll;
	bus_size_t psize;
	phandle_t node;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);
	indices = NULL;
	type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (ofw_reg_to_paddr(node, 0, &sc->reg, &psize, NULL) != 0) {
		device_printf(dev, "cannot parse 'reg' property\n");
		return (ENXIO);
	}

	clkdom = clkdom_create(dev);

	nout = clk_parse_ofw_out_names(dev, node, &names, &indices);
	if (nout == 0) {
		device_printf(dev, "no clock outputs found\n");
		error = ENOENT;
		goto fail;
	}

	if (indices == NULL && type == AW_A10_USBCLK)
		indices = aw_usbclk_indices_a10;
	else if (indices == NULL && type == AW_H3_USBCLK)
		indices = aw_usbclk_indices_h3;

	error = clk_get_by_ofw_index(dev, 0, 0, &clk_parent);
	if (error != 0) {
		device_printf(dev, "cannot parse clock parent\n");
		return (ENXIO);
	}
	if (type == AW_A83T_USBCLK) {
		error = clk_get_by_ofw_index(dev, 0, 1, &clk_parent_pll);
		if (error != 0) {
			device_printf(dev, "cannot parse pll clock parent\n");
			return (ENXIO);
		}
	}

	for (index = 0; index < nout; index++) {
		if (strcmp(names[index], "usb_hsic_pll") == 0)
			pname = clk_get_name(clk_parent_pll);
		else
			pname = clk_get_name(clk_parent);
		error = aw_usbclk_create(dev, sc->reg, clkdom, pname,
		    names[index], indices != NULL ? indices[index] : index);
		if (error)
			goto fail;
	}

	if (clkdom_finit(clkdom) != 0) {
		device_printf(dev, "cannot finalize clkdom initialization\n");
		error = ENXIO;
		goto fail;
	}

	if (bootverbose)
		clkdom_dump(clkdom);

	hwreset_register_ofw_provider(dev);

	return (0);

fail:
	return (error);
}

static device_method_t aw_usbclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_usbclk_probe),
	DEVMETHOD(device_attach,	aw_usbclk_attach),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	aw_usbclk_hwreset_assert),
	DEVMETHOD(hwreset_is_asserted,	aw_usbclk_hwreset_is_asserted),

	DEVMETHOD_END
};

static driver_t aw_usbclk_driver = {
	"aw_usbclk",
	aw_usbclk_methods,
	sizeof(struct aw_usbclk_softc)
};

static devclass_t aw_usbclk_devclass;

EARLY_DRIVER_MODULE(aw_usbclk, simplebus, aw_usbclk_driver,
    aw_usbclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
