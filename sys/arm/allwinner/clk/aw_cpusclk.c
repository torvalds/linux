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
 * Allwinner CPUS clock
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

#define	A80_CPUS_CLK_SRC_SEL			(0x3 << 16)
#define	A80_CPUS_CLK_SRC_SEL_SHIFT		16
#define	A80_CPUS_CLK_SRC_SEL_X32KI		0
#define	A80_CPUS_CLK_SRC_SEL_OSC24M		1
#define	A80_CPUS_CLK_SRC_SEL_PLL_PERIPH		2
#define	A80_CPUS_CLK_SRC_SEL_PLL_AUDIO		3
#define	A80_CPUS_POST_DIV			(0x1f << 8)
#define	A80_CPUS_POST_DIV_SHIFT			8
#define	A80_CPUS_CLK_RATIO			(0x3 << 4)
#define	A80_CPUS_CLK_RATIO_SHIFT		4

#define	A83T_CPUS_CLK_SRC_SEL			(0x3 << 16)
#define	A83T_CPUS_CLK_SRC_SEL_SHIFT		16
#define	A83T_CPUS_CLK_SRC_SEL_X32KI		0
#define	A83T_CPUS_CLK_SRC_SEL_OSC24M		1
#define	A83T_CPUS_CLK_SRC_SEL_PLL_PERIPH	2
#define	A83T_CPUS_CLK_SRC_SEL_INTERNAL_OSC	3
#define	A83T_CPUS_POST_DIV			(0x1f << 8)
#define	A83T_CPUS_POST_DIV_SHIFT		8
#define	A83T_CPUS_CLK_RATIO			(0x3 << 4)
#define	A83T_CPUS_CLK_RATIO_SHIFT		4

enum aw_cpusclk_type {
	AW_A80_CPUS = 1,
	AW_A83T_CPUS,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun9i-a80-cpus-clk",	AW_A80_CPUS },
	{ "allwinner,sun8i-a83t-cpus-clk",	AW_A83T_CPUS },
	{ NULL, 0 }
};

struct aw_cpusclk_sc {
	device_t		clkdev;
	bus_addr_t		reg;
	enum aw_cpusclk_type	type;
};

#define	CPUSCLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	CPUSCLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_cpusclk_init(struct clknode *clk, device_t dev)
{
	struct aw_cpusclk_sc *sc;
	uint32_t val, mask, shift, index;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_A80_CPUS:
		mask = A80_CPUS_CLK_SRC_SEL;
		shift = A80_CPUS_CLK_SRC_SEL_SHIFT;
		break;
	case AW_A83T_CPUS:
		mask = A83T_CPUS_CLK_SRC_SEL;
		shift = A83T_CPUS_CLK_SRC_SEL_SHIFT;
		break;
	default:
		return (ENXIO);
	}

	DEVICE_LOCK(sc);
	CPUSCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);
	index = (val & mask) >> shift;

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_cpusclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_cpusclk_sc *sc;
	uint32_t val, src_sel, post_div, clk_ratio;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	CPUSCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	switch (sc->type) {
	case AW_A80_CPUS:
		src_sel = (val & A80_CPUS_CLK_SRC_SEL) >>
		    A80_CPUS_CLK_SRC_SEL_SHIFT;
		post_div = ((val & A80_CPUS_POST_DIV) >>
		    A80_CPUS_POST_DIV_SHIFT) + 1;
		clk_ratio = ((val & A80_CPUS_CLK_RATIO) >>
		    A80_CPUS_CLK_RATIO_SHIFT) + 1;
		if (src_sel == A80_CPUS_CLK_SRC_SEL_PLL_PERIPH)
			*freq = *freq / post_div / clk_ratio;
		else
			*freq = *freq / clk_ratio;
		break;
	case AW_A83T_CPUS:
		src_sel = (val & A83T_CPUS_CLK_SRC_SEL) >>
		    A83T_CPUS_CLK_SRC_SEL_SHIFT;
		post_div = ((val & A83T_CPUS_POST_DIV) >>
		    A83T_CPUS_POST_DIV_SHIFT) + 1;
		clk_ratio = 1 << ((val & A83T_CPUS_CLK_RATIO) >>
		    A83T_CPUS_CLK_RATIO_SHIFT);
		if (src_sel == A83T_CPUS_CLK_SRC_SEL_PLL_PERIPH)
			*freq = *freq / post_div / clk_ratio;
		else
			*freq = *freq / clk_ratio;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
aw_cpusclk_set_mux(struct clknode *clk, int index)
{
	struct aw_cpusclk_sc *sc;
	uint32_t mask, shift, val;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_A80_CPUS:
		mask = A80_CPUS_CLK_SRC_SEL;
		shift = A80_CPUS_CLK_SRC_SEL_SHIFT;
		break;
	case AW_A83T_CPUS:
		mask = A83T_CPUS_CLK_SRC_SEL;
		shift = A83T_CPUS_CLK_SRC_SEL_SHIFT;
		break;
	default:
		return (ENXIO);
	}

	DEVICE_LOCK(sc);
	CPUSCLK_READ(sc, &val);
	val &= ~mask;
	val |= (index << shift);
	CPUSCLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_cpusclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_cpusclk_init),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_cpusclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_mux,		aw_cpusclk_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_cpusclk_clknode, aw_cpusclk_clknode_class,
    aw_cpusclk_clknode_methods, sizeof(struct aw_cpusclk_sc), clknode_class);

static int
aw_cpusclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner CPUS Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_cpusclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_cpusclk_sc *sc;
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
	def.id = 1;
	def.parent_names = malloc(sizeof(char *) * ncells, M_OFWPROP,
	    M_WAITOK);
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

	error = clk_parse_ofw_clk_name(dev, node, &def.name);
	if (error != 0) {
		device_printf(dev, "cannot parse clock name\n");
		error = ENXIO;
		goto fail;
	}

	clk = clknode_create(clkdom, &aw_cpusclk_clknode_class, &def);
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

static device_method_t aw_cpusclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_cpusclk_probe),
	DEVMETHOD(device_attach,	aw_cpusclk_attach),

	DEVMETHOD_END
};

static driver_t aw_cpusclk_driver = {
	"aw_cpusclk",
	aw_cpusclk_methods,
	0
};

static devclass_t aw_cpusclk_devclass;

EARLY_DRIVER_MODULE(aw_cpusclk, simplebus, aw_cpusclk_driver,
    aw_cpusclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
