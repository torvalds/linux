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
 * Allwinner oscillator clock
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk_fixed.h>

static int
aw_oscclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun4i-a10-osc-clk"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner Oscillator Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_oscclk_attach(device_t dev)
{
	struct clk_fixed_def def;
	struct clkdom *clkdom;
	phandle_t node;
	uint32_t freq;
	int error;

	node = ofw_bus_get_node(dev);

	if (OF_getencprop(node, "clock-frequency", &freq,  sizeof(freq)) <= 0) {
		device_printf(dev, "missing clock-frequency property\n");
		error = ENXIO;
		goto fail;
	}

	clkdom = clkdom_create(dev);

	memset(&def, 0, sizeof(def));
	def.clkdef.id = 1;
	def.freq = freq;
	error = clk_parse_ofw_clk_name(dev, node, &def.clkdef.name);
	if (error != 0) {
		device_printf(dev, "cannot parse clock name\n");
		error = ENXIO;
		goto fail;
	}

	error = clknode_fixed_register(clkdom, &def);
	if (error != 0) {
		device_printf(dev, "cannot register fixed clock\n");
		error = ENXIO;
		goto fail;
	}

	if (clkdom_finit(clkdom) != 0) {
		device_printf(dev, "cannot finalize clkdom initialization\n");
		error = ENXIO;
		goto fail;
	}

	if (bootverbose)
		clkdom_dump(clkdom);

	OF_prop_free(__DECONST(char *, def.clkdef.name));

	return (0);

fail:
	OF_prop_free(__DECONST(char *, def.clkdef.name));
	return (error);
}

static device_method_t aw_oscclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_oscclk_probe),
	DEVMETHOD(device_attach,	aw_oscclk_attach),

	DEVMETHOD_END
};

static driver_t aw_oscclk_driver = {
	"aw_oscclk",
	aw_oscclk_methods,
	0,
};

static devclass_t aw_oscclk_devclass;

EARLY_DRIVER_MODULE(aw_oscclk, simplebus, aw_oscclk_driver,
    aw_oscclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
