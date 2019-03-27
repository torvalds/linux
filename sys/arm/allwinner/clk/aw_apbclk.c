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
 * Allwinner APB clock
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

#include <dev/extres/clk/clk.h>

#include "clkdev_if.h"

#define	A10_APB0_CLK_RATIO		(0x3 << 8)
#define	A10_APB0_CLK_RATIO_SHIFT	8
#define	A10_APB1_CLK_SRC_SEL		(0x3 << 24)
#define	A10_APB1_CLK_SRC_SEL_SHIFT	24
#define	A10_APB1_CLK_SRC_SEL_MAX	0x3
#define	A10_APB1_CLK_RAT_N		(0x3 << 16)
#define	A10_APB1_CLK_RAT_N_SHIFT	16
#define	A10_APB1_CLK_RAT_M		(0x1f << 0)
#define	A10_APB1_CLK_RAT_M_SHIFT	0
#define	A23_APB0_CLK_RATIO		(0x3 << 0)
#define	A23_APB0_CLK_RATIO_SHIFT	0
#define	A83T_APB1_CLK_RATIO		(0x3 << 8)
#define	A83T_APB1_CLK_RATIO_SHIFT	8

enum aw_apbclk_type {
	AW_A10_APB0 = 1,
	AW_A10_APB1,
	AW_A23_APB0,
	AW_A83T_APB1,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-apb0-clk",	AW_A10_APB0 },
	{ "allwinner,sun4i-a10-apb1-clk",	AW_A10_APB1 },
	{ "allwinner,sun8i-a23-apb0-clk",	AW_A23_APB0 },
	{ "allwinner,sun8i-a83t-apb1-clk",	AW_A83T_APB1 },
	{ NULL, 0 }
};

struct aw_apbclk_sc {
	device_t		clkdev;
	bus_addr_t		reg;
	enum aw_apbclk_type	type;
};

#define	APBCLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	APBCLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_apbclk_init(struct clknode *clk, device_t dev)
{
	struct aw_apbclk_sc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_A10_APB0:
	case AW_A23_APB0:
	case AW_A83T_APB1:
		index = 0;
		break;
	case AW_A10_APB1:
		DEVICE_LOCK(sc);
		APBCLK_READ(sc, &val);
		DEVICE_UNLOCK(sc);
		index = (val & A10_APB1_CLK_SRC_SEL) >>
		    A10_APB1_CLK_SRC_SEL_SHIFT;
		break;
	default:
		return (ENXIO);
	}

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_apbclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_apbclk_sc *sc;
	uint32_t val, div, m, n;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	APBCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	switch (sc->type) {
	case AW_A10_APB0:
		div = 1 << ((val & A10_APB0_CLK_RATIO) >>
		    A10_APB0_CLK_RATIO_SHIFT);
		if (div == 1)
			div = 2;
		*freq = *freq / div;
		break;
	case AW_A10_APB1:
		n = 1 << ((val & A10_APB1_CLK_RAT_N) >>
		    A10_APB1_CLK_RAT_N_SHIFT);
		m = ((val & A10_APB1_CLK_RAT_N) >>
		    A10_APB1_CLK_RAT_M_SHIFT) + 1;
		*freq = *freq / n / m;
		break;
	case AW_A23_APB0:
		div = 1 << ((val & A23_APB0_CLK_RATIO) >>
		    A23_APB0_CLK_RATIO_SHIFT);
		*freq = *freq / div;
		break;
	case AW_A83T_APB1:
		div = ((val & A83T_APB1_CLK_RATIO) >>
		    A83T_APB1_CLK_RATIO_SHIFT) + 1;
		*freq = *freq / div;
		break;
	default:
		return (ENXIO);
	}

	return (0);
}

static int
aw_apbclk_set_mux(struct clknode *clk, int index)
{
	struct aw_apbclk_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	if (sc->type != AW_A10_APB1)
		return (ENXIO);

	if (index < 0 || index > A10_APB1_CLK_SRC_SEL_MAX)
		return (ERANGE);

	DEVICE_LOCK(sc);
	APBCLK_READ(sc, &val);
	val &= ~A10_APB1_CLK_SRC_SEL;
	val |= (index << A10_APB1_CLK_SRC_SEL_SHIFT);
	APBCLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_apbclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_apbclk_init),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_apbclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_mux,		aw_apbclk_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_apbclk_clknode, aw_apbclk_clknode_class,
    aw_apbclk_clknode_methods, sizeof(struct aw_apbclk_sc), clknode_class);

static int
aw_apbclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner APB Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_apbclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_apbclk_sc *sc;
	struct clkdom *clkdom;
	struct clknode *clk;
	clk_t clk_parent;
	bus_addr_t paddr;
	bus_size_t psize;
	phandle_t node;
	int error, ncells, i;

	node = ofw_bus_get_node(dev);

	if (ofw_reg_to_paddr(node, 0, &paddr, &psize, NULL) != 0) {
		device_printf(dev, "cannot parse 'reg' property\n");
		return (ENXIO);
	}

	error = ofw_bus_parse_xref_list_get_length(node, "clocks",
	    "#clock-cells", &ncells);
	if (error != 0) {
		device_printf(dev, "cannot get clock count\n");
		return (error);
	}

	clkdom = clkdom_create(dev);

	memset(&def, 0, sizeof(def));
	error = clk_parse_ofw_clk_name(dev, node, &def.name);
	if (error != 0) {
		device_printf(dev, "cannot parse clock name\n");
		error = ENXIO;
		goto fail;
	}
	def.id = 1;
	def.parent_names = malloc(sizeof(char *) * ncells, M_OFWPROP, M_WAITOK);
	for (i = 0; i < ncells; i++) {
		error = clk_get_by_ofw_index(dev, 0, i, &clk_parent);
		if (error != 0) {
			device_printf(dev, "cannot get clock %d\n", i);
			goto fail;
		}
		def.parent_names[i] = clk_get_name(clk_parent);
		clk_release(clk_parent);
	}
	def.parent_cnt = ncells;

	clk = clknode_create(clkdom, &aw_apbclk_clknode_class, &def);
	if (clk == NULL) {
		device_printf(dev, "cannot create clknode\n");
		error = ENXIO;
		goto fail;
	}

	sc = clknode_get_softc(clk);
	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	sc->reg = paddr;
	sc->clkdev = device_get_parent(dev);

	clknode_register(clkdom, clk);

	if (clkdom_finit(clkdom) != 0) {
		device_printf(dev, "cannot finalize clkdom initialization\n");
		error = ENXIO;
		goto fail;
	}

	if (bootverbose)
		clkdom_dump(clkdom);

	return (0);

fail:
	return (error);
}

static device_method_t aw_apbclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_apbclk_probe),
	DEVMETHOD(device_attach,	aw_apbclk_attach),

	DEVMETHOD_END
};

static driver_t aw_apbclk_driver = {
	"aw_apbclk",
	aw_apbclk_methods,
	0
};

static devclass_t aw_apbclk_devclass;

EARLY_DRIVER_MODULE(aw_apbclk, simplebus, aw_apbclk_driver,
    aw_apbclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
