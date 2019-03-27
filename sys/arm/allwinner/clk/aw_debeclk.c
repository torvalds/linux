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
 * Allwinner display backend clocks
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
#include <dev/extres/hwreset/hwreset.h>

#include "clkdev_if.h"
#include "hwreset_if.h"

#define	SCLK_GATING		(1 << 31)
#define	BE_RST			(1 << 30)
#define	CLK_SRC_SEL		(0x3 << 24)
#define	CLK_SRC_SEL_SHIFT	24
#define	CLK_SRC_SEL_MAX		2
#define	CLK_SRC_SEL_PLL3	0
#define	CLK_SRC_SEL_PLL7	1
#define	CLK_SRC_SEL_PLL5	2
#define	CLK_RATIO_M		(0xf << 0)
#define	CLK_RATIO_M_SHIFT	0
#define	CLK_RATIO_M_MAX		0xf

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-de-be-clk",	1 },
	{ NULL, 0 }
};

struct aw_debeclk_softc {
	device_t	clkdev;
	bus_addr_t	reg;
};

#define	DEBECLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	DEBECLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEBECLK_MODIFY(sc, clr, set)	\
	CLKDEV_MODIFY_4((sc)->clkdev, (sc)->reg, (clr), (set))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_debeclk_hwreset_assert(device_t dev, intptr_t id, bool value)
{
	struct aw_debeclk_softc *sc;
	int error;

	sc = device_get_softc(dev);

	DEVICE_LOCK(sc);
	error = DEBECLK_MODIFY(sc, BE_RST, value ? 0 : BE_RST);
	DEVICE_UNLOCK(sc);

	return (error);
}

static int
aw_debeclk_hwreset_is_asserted(device_t dev, intptr_t id, bool *value)
{
	struct aw_debeclk_softc *sc;
	uint32_t val;
	int error;

	sc = device_get_softc(dev);

	DEVICE_LOCK(sc);
	error = DEBECLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	if (error)
		return (error);

	*value = (val & BE_RST) != 0 ? false : true;

	return (0);
}

static int
aw_debeclk_init(struct clknode *clk, device_t dev)
{
	struct aw_debeclk_softc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	/* Set BE source to PLL5 (DDR external peripheral clock) */
	index = CLK_SRC_SEL_PLL5;

	DEVICE_LOCK(sc);
	DEBECLK_READ(sc, &val);
	val &= ~CLK_SRC_SEL;
	val |= (index << CLK_SRC_SEL_SHIFT);
	DEBECLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_debeclk_set_mux(struct clknode *clk, int index)
{
	struct aw_debeclk_softc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	if (index < 0 || index > CLK_SRC_SEL_MAX)
		return (ERANGE);

	DEVICE_LOCK(sc);
	DEBECLK_READ(sc, &val);
	val &= ~CLK_SRC_SEL;
	val |= (index << CLK_SRC_SEL_SHIFT);
	DEBECLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static int
aw_debeclk_set_gate(struct clknode *clk, bool enable)
{
	struct aw_debeclk_softc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	DEBECLK_READ(sc, &val);
	if (enable)
		val |= SCLK_GATING;
	else
		val &= ~SCLK_GATING;
	DEBECLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static int
aw_debeclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_debeclk_softc *sc;
	uint32_t val, m;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	DEBECLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	m = ((val & CLK_RATIO_M) >> CLK_RATIO_M_SHIFT) + 1;

	*freq = *freq / m;

	return (0);
}

static int
aw_debeclk_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
    int flags, int *stop)
{
	struct aw_debeclk_softc *sc;
	uint32_t val, m;

	sc = clknode_get_softc(clk);

	m = howmany(fin, *fout) - 1;

	*fout = fin / (m + 1);
	*stop = 1;

	if ((flags & CLK_SET_DRYRUN) != 0)
		return (0);

	DEVICE_LOCK(sc);
	DEBECLK_READ(sc, &val);
	val &= ~CLK_RATIO_M;
	val |= (m << CLK_RATIO_M_SHIFT);
	DEBECLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_debeclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_debeclk_init),
	CLKNODEMETHOD(clknode_set_gate,		aw_debeclk_set_gate),
	CLKNODEMETHOD(clknode_set_mux,		aw_debeclk_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_debeclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		aw_debeclk_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_debeclk_clknode, aw_debeclk_clknode_class,
    aw_debeclk_clknode_methods, sizeof(struct aw_debeclk_softc), clknode_class);

static int
aw_debeclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Display Engine Backend Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_debeclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_debeclk_softc *sc, *clk_sc;
	struct clkdom *clkdom;
	struct clknode *clk;
	clk_t clk_parent;
	bus_size_t psize;
	phandle_t node;
	int error, ncells, i;

	sc = device_get_softc(dev);
	sc->clkdev = device_get_parent(dev);
	node = ofw_bus_get_node(dev);

	if (ofw_reg_to_paddr(node, 0, &sc->reg, &psize, NULL) != 0) {
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

	clk = clknode_create(clkdom, &aw_debeclk_clknode_class, &def);
	if (clk == NULL) {
		device_printf(dev, "cannot create clknode\n");
		error = ENXIO;
		goto fail;
	}

	clk_sc = clknode_get_softc(clk);
	clk_sc->reg = sc->reg;
	clk_sc->clkdev = device_get_parent(dev);

	clknode_register(clkdom, clk);

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

static device_method_t aw_debeclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_debeclk_probe),
	DEVMETHOD(device_attach,	aw_debeclk_attach),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	aw_debeclk_hwreset_assert),
	DEVMETHOD(hwreset_is_asserted,	aw_debeclk_hwreset_is_asserted),

	DEVMETHOD_END
};

static driver_t aw_debeclk_driver = {
	"aw_debeclk",
	aw_debeclk_methods,
	sizeof(struct aw_debeclk_softc)
};

static devclass_t aw_debeclk_devclass;

EARLY_DRIVER_MODULE(aw_debeclk, simplebus, aw_debeclk_driver,
    aw_debeclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
