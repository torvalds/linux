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
 * Allwinner AHB clock
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

#define	A10_AHB_CLK_DIV_RATIO		(0x3 << 4)
#define	A10_AHB_CLK_DIV_RATIO_SHIFT	4

#define	A13_AHB_CLK_SRC_SEL		(0x3 << 6)
#define	A13_AHB_CLK_SRC_SEL_MAX		3
#define	A13_AHB_CLK_SRC_SEL_SHIFT	6

#define	A31_AHB1_PRE_DIV		(0x3 << 6)
#define	A31_AHB1_PRE_DIV_SHIFT		6
#define	A31_AHB1_CLK_SRC_SEL		(0x3 << 12)
#define	A31_AHB1_CLK_SRC_SEL_PLL6	3
#define	A31_AHB1_CLK_SRC_SEL_MAX	3
#define	A31_AHB1_CLK_SRC_SEL_SHIFT	12

#define	A83T_AHB1_CLK_SRC_SEL		(0x3 << 12)
#define	A83T_AHB1_CLK_SRC_SEL_ISPLL(x)	((x) & 0x2)
#define	A83T_AHB1_CLK_SRC_SEL_MAX	3
#define	A83T_AHB1_CLK_SRC_SEL_SHIFT	12
#define	A83T_AHB1_PRE_DIV		(0x3 << 6)
#define	A83T_AHB1_PRE_DIV_SHIFT		6
#define	A83T_AHB1_CLK_DIV_RATIO		(0x3 << 4)
#define	A83T_AHB1_CLK_DIV_RATIO_SHIFT	4

#define	H3_AHB2_CLK_CFG			(0x3 << 0)
#define	H3_AHB2_CLK_CFG_SHIFT		0
#define	H3_AHB2_CLK_CFG_AHB1		0
#define	H3_AHB2_CLK_CFG_PLL_PERIPH_DIV2	1
#define	H3_AHB2_CLK_CFG_MAX		1

enum aw_ahbclk_type {
	AW_A10_AHB = 1,
	AW_A13_AHB,
	AW_A31_AHB1,
	AW_A83T_AHB1,
	AW_H3_AHB2,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-ahb-clk",	AW_A10_AHB },
	{ "allwinner,sun5i-a13-ahb-clk",	AW_A13_AHB },
	{ "allwinner,sun6i-a31-ahb1-clk",	AW_A31_AHB1 },
	{ "allwinner,sun8i-a83t-ahb1-clk",	AW_A83T_AHB1 },
	{ "allwinner,sun8i-h3-ahb2-clk",	AW_H3_AHB2 },
	{ NULL, 0 }
};

struct aw_ahbclk_sc {
	device_t		clkdev;
	bus_addr_t		reg;
	enum aw_ahbclk_type	type;
};

#define	AHBCLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	AHBCLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_ahbclk_init(struct clknode *clk, device_t dev)
{
	struct aw_ahbclk_sc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_A10_AHB:
		index = 0;
		break;
	case AW_A13_AHB:
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		DEVICE_UNLOCK(sc);
		index = (val & A13_AHB_CLK_SRC_SEL) >>
		    A13_AHB_CLK_SRC_SEL_SHIFT;
		break;
	case AW_A31_AHB1:
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		DEVICE_UNLOCK(sc);
		index = (val & A31_AHB1_CLK_SRC_SEL) >>
		    A31_AHB1_CLK_SRC_SEL_SHIFT;
		break;
	case AW_A83T_AHB1:
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		DEVICE_UNLOCK(sc);
		index = (val & A83T_AHB1_CLK_SRC_SEL) >>
		    A83T_AHB1_CLK_SRC_SEL_SHIFT;
		break;
	case AW_H3_AHB2:
		/* Set source to PLL_PERIPH/2 */
		index = H3_AHB2_CLK_CFG_PLL_PERIPH_DIV2;
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		val &= ~H3_AHB2_CLK_CFG;
		val |= (index << H3_AHB2_CLK_CFG_SHIFT);
		AHBCLK_WRITE(sc, val);
		DEVICE_UNLOCK(sc);
		break;
	default:
		return (ENXIO);
	}

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_ahbclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_ahbclk_sc *sc;
	uint32_t val, src_sel, div, pre_div;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	AHBCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	switch (sc->type) {
	case AW_A31_AHB1:
		div = 1 << ((val & A10_AHB_CLK_DIV_RATIO) >>
		    A10_AHB_CLK_DIV_RATIO_SHIFT);
		src_sel = (val & A31_AHB1_CLK_SRC_SEL) >>
		    A31_AHB1_CLK_SRC_SEL_SHIFT;
		if (src_sel == A31_AHB1_CLK_SRC_SEL_PLL6)
			pre_div = ((val & A31_AHB1_PRE_DIV) >>
			    A31_AHB1_PRE_DIV_SHIFT) + 1;
		else
			pre_div = 1;
		break;
	case AW_A83T_AHB1:
		div = 1 << ((val & A83T_AHB1_CLK_DIV_RATIO) >>
		    A83T_AHB1_CLK_DIV_RATIO_SHIFT);
		src_sel = (val & A83T_AHB1_CLK_SRC_SEL) >>
		    A83T_AHB1_CLK_SRC_SEL_SHIFT;
		if (A83T_AHB1_CLK_SRC_SEL_ISPLL(src_sel))
			pre_div = ((val & A83T_AHB1_PRE_DIV) >>
			    A83T_AHB1_PRE_DIV_SHIFT) + 1;
		else
			pre_div = 1;
		break;
	case AW_H3_AHB2:
		div = pre_div = 1;
		break;
	default:
		div = 1 << ((val & A10_AHB_CLK_DIV_RATIO) >>
		    A10_AHB_CLK_DIV_RATIO_SHIFT);
		pre_div = 1;
		break;
	}

	*freq = *freq / pre_div / div;

	return (0);
}

static int
aw_ahbclk_set_mux(struct clknode *clk, int index)
{
	struct aw_ahbclk_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_A10_AHB:
		if (index != 0)
			return (ERANGE);
		break;
	case AW_A13_AHB:
		if (index < 0 || index > A13_AHB_CLK_SRC_SEL_MAX)
			return (ERANGE);
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		val &= ~A13_AHB_CLK_SRC_SEL;
		val |= (index << A13_AHB_CLK_SRC_SEL_SHIFT);
		AHBCLK_WRITE(sc, val);
		DEVICE_UNLOCK(sc);
		break;
	case AW_A83T_AHB1:
		if (index < 0 || index > A83T_AHB1_CLK_SRC_SEL_MAX)
			return (ERANGE);
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		val &= ~A83T_AHB1_CLK_SRC_SEL;
		val |= (index << A83T_AHB1_CLK_SRC_SEL_SHIFT);
		AHBCLK_WRITE(sc, val);
		DEVICE_UNLOCK(sc);
		break;
	case AW_H3_AHB2:
		if (index < 0 || index > H3_AHB2_CLK_CFG)
			return (ERANGE);
		DEVICE_LOCK(sc);
		AHBCLK_READ(sc, &val);
		val &= ~H3_AHB2_CLK_CFG;
		val |= (index << H3_AHB2_CLK_CFG_SHIFT);
		AHBCLK_WRITE(sc, val);
		DEVICE_UNLOCK(sc);
		break;
	default:
		return (ENXIO);
	}

	return (0);
}

static clknode_method_t aw_ahbclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_ahbclk_init),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_ahbclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_mux,		aw_ahbclk_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_ahbclk_clknode, aw_ahbclk_clknode_class,
    aw_ahbclk_clknode_methods, sizeof(struct aw_ahbclk_sc), clknode_class);

static int
aw_ahbclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner AHB Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_ahbclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_ahbclk_sc *sc;
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

	clk = clknode_create(clkdom, &aw_ahbclk_clknode_class, &def);
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

	error = clk_set_assigned(dev, node);
	if (error != 0 && error != ENOENT) {
		device_printf(dev, "cannot set assigned parents: %d\n", error);
		goto fail;
	}

	if (bootverbose)
		clkdom_dump(clkdom);

	return (0);

fail:
	return (error);
}

static device_method_t aw_ahbclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_ahbclk_probe),
	DEVMETHOD(device_attach,	aw_ahbclk_attach),

	DEVMETHOD_END
};

static driver_t aw_ahbclk_driver = {
	"aw_ahbclk",
	aw_ahbclk_methods,
	0
};

static devclass_t aw_ahbclk_devclass;

EARLY_DRIVER_MODULE(aw_ahbclk, simplebus, aw_ahbclk_driver,
    aw_ahbclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
