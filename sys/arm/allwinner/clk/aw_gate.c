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
 * Allwinner clock gates
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

#define	GATE_OFFSET(index)	((index / 32) * 4)
#define	GATE_SHIFT(index)	(index % 32)

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-dram-gates-clk",
	  (uintptr_t)"Allwinner DRAM Clock Gates" },
	{ "allwinner,sun4i-a10-ahb-gates-clk",
	  (uintptr_t)"Allwinner AHB Clock Gates" },
	{ "allwinner,sun4i-a10-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },
	{ "allwinner,sun4i-a10-apb1-gates-clk",
	  (uintptr_t)"Allwinner APB1 Clock Gates" },

	{ "allwinner,sun5i-a13-ahb-gates-clk",
	  (uintptr_t)"Allwinner AHB Clock Gates" },
	{ "allwinner,sun5i-a13-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },
	{ "allwinner,sun5i-a13-apb1-gates-clk",
	  (uintptr_t)"Allwinner APB1 Clock Gates" },

	{ "allwinner,sun7i-a20-ahb-gates-clk",
	  (uintptr_t)"Allwinner AHB Clock Gates" },
	{ "allwinner,sun7i-a20-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },
	{ "allwinner,sun7i-a20-apb1-gates-clk",
	  (uintptr_t)"Allwinner APB1 Clock Gates" },

	{ "allwinner,sun6i-a31-ahb1-gates-clk",
	  (uintptr_t)"Allwinner AHB1 Clock Gates" },
	{ "allwinner,sun6i-a31-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },
	{ "allwinner,sun6i-a31-apb1-gates-clk",
	  (uintptr_t)"Allwinner APB1 Clock Gates" },
	{ "allwinner,sun6i-a31-apb2-gates-clk",
	  (uintptr_t)"Allwinner APB2 Clock Gates" },

	{ "allwinner,sun8i-a23-apb1-gates-clk",
	  (uintptr_t)"Allwinner APB1 Clock Gates" },
	{ "allwinner,sun8i-a23-apb2-gates-clk",
	  (uintptr_t)"Allwinner APB2 Clock Gates" },

	{ "allwinner,sun8i-a83t-bus-gates-clk",
	  (uintptr_t)"Allwinner Bus Clock Gates" },
	{ "allwinner,sun8i-a83t-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },

	{ "allwinner,sun8i-h3-bus-gates-clk",
	  (uintptr_t)"Allwinner Bus Clock Gates" },
	{ "allwinner,sun8i-h3-apb0-gates-clk",
	  (uintptr_t)"Allwinner APB0 Clock Gates" },

	{ "allwinner,sun9i-a80-apbs-gates-clk",
	  (uintptr_t)"Allwinner APBS Clock Gates" },

	{ "allwinner,sunxi-multi-bus-gates-clk",
	  (uintptr_t)"Allwinner Multi Bus Clock Gates" },

	{ NULL, 0 }
};

static int
aw_gate_create(device_t dev, bus_addr_t paddr, struct clkdom *clkdom,
    const char *pclkname, const char *clkname, int index)
{
	const char *parent_names[1] = { pclkname };
	struct clk_gate_def def;

	memset(&def, 0, sizeof(def));
	def.clkdef.id = index;
	def.clkdef.name = clkname;
	def.clkdef.parent_names = parent_names;
	def.clkdef.parent_cnt = 1;
	def.offset = paddr + GATE_OFFSET(index);
	def.shift = GATE_SHIFT(index);
	def.mask = 1;
	def.on_value = 1;
	def.off_value = 0;

	return (clknode_gate_register(clkdom, &def));
}

static int
aw_gate_add(device_t dev, struct clkdom *clkdom, phandle_t node,
    bus_addr_t paddr)
{
	const char **names;
	uint32_t *indices;
	clk_t clk_parent;
	int index, nout, error;

	indices = NULL;

	nout = clk_parse_ofw_out_names(dev, node, &names, &indices);
	if (nout == 0) {
		device_printf(dev, "no clock outputs found\n");
		return (ENOENT);
	}
	if (indices == NULL) {
		device_printf(dev, "no clock-indices property\n");
		return (ENXIO);
	}

	error = clk_get_by_ofw_index(dev, node, 0, &clk_parent);
	if (error != 0) {
		device_printf(dev, "cannot parse clock parent\n");
		return (ENXIO);
	}

	for (index = 0; index < nout; index++) {
		error = aw_gate_create(dev, paddr, clkdom,
		    clk_get_name(clk_parent), names[index], indices[index]);
		if (error)
			return (error);
	}

	return (0);
}

static int
aw_gate_probe(device_t dev)
{
	const char *d;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	d = (const char *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (d == NULL)
		return (ENXIO);

	device_set_desc(dev, d);
	return (BUS_PROBE_DEFAULT);
}

static int
aw_gate_attach(device_t dev)
{
	struct clkdom *clkdom;
	bus_addr_t paddr;
	bus_size_t psize;
	phandle_t node, child;

	node = ofw_bus_get_node(dev);

	if (ofw_reg_to_paddr(node, 0, &paddr, &psize, NULL) != 0) {
		device_printf(dev, "cannot parse 'reg' property\n");
		return (ENXIO);
	}

	clkdom = clkdom_create(dev);

	if (ofw_bus_is_compatible(dev, "allwinner,sunxi-multi-bus-gates-clk")) {
		for (child = OF_child(node); child > 0; child = OF_peer(child))
			aw_gate_add(dev, clkdom, child, paddr);
	} else
		aw_gate_add(dev, clkdom, node, paddr);

	if (clkdom_finit(clkdom) != 0) {
		device_printf(dev, "cannot finalize clkdom initialization\n");
		return (ENXIO);
	}

	if (bootverbose)
		clkdom_dump(clkdom);

	return (0);
}

static device_method_t aw_gate_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_gate_probe),
	DEVMETHOD(device_attach,	aw_gate_attach),

	DEVMETHOD_END
};

static driver_t aw_gate_driver = {
	"aw_gate",
	aw_gate_methods,
	0
};

static devclass_t aw_gate_devclass;

EARLY_DRIVER_MODULE(aw_gate, simplebus, aw_gate_driver,
    aw_gate_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
