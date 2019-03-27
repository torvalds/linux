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
 * Allwinner LCD clocks
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

/* CH0 */
#define	CH0_SCLK_GATING			(1 << 31)
#define	CH0_LCD_RST			(1 << 30)
#define	CH0_CLK_SRC_SEL			(0x3 << 24)
#define	CH0_CLK_SRC_SEL_SHIFT		24
#define	CH0_CLK_SRC_SEL_PLL3_1X		0
#define	CH0_CLK_SRC_SEL_PLL7_1X		1
#define	CH0_CLK_SRC_SEL_PLL3_2X		2
#define	CH0_CLK_SRC_SEL_PLL6		3

/* CH1 */
#define	CH1_SCLK2_GATING		(1 << 31)
#define	CH1_SCLK2_SEL			(0x3 << 24)
#define	CH1_SCLK2_SEL_SHIFT		24
#define	CH1_SCLK2_SEL_PLL3_1X		0
#define	CH1_SCLK2_SEL_PLL7_1X		1
#define	CH1_SCLK2_SEL_PLL3_2X		2
#define	CH1_SCLK2_SEL_PLL7_2X		3
#define	CH1_SCLK1_GATING		(1 << 15)
#define	CH1_SCLK1_SEL			(0x1 << 11)
#define	CH1_SCLK1_SEL_SHIFT		11
#define	CH1_SCLK1_SEL_SCLK2		0
#define	CH1_SCLK1_SEL_SCLK2_DIV2	1
#define	CH1_CLK_DIV_RATIO_M		(0x1f << 0)
#define	CH1_CLK_DIV_RATIO_M_SHIFT	0

#define	TCON_PLLREF			3000000ULL
#define	TCON_PLLREF_FRAC1		297000000ULL
#define	TCON_PLLREF_FRAC2		270000000ULL
#define	TCON_PLL_M_MIN			1
#define	TCON_PLL_M_MAX			15
#define	TCON_PLL_N_MIN			9
#define	TCON_PLL_N_MAX			127

#define	CLK_IDX_CH1_SCLK1		0
#define	CLK_IDX_CH1_SCLK2		1

#define	CLK_IDX_

enum aw_lcdclk_type {
	AW_LCD_CH0 = 1,
	AW_LCD_CH1,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-lcd-ch0-clk",	AW_LCD_CH0 },
	{ "allwinner,sun4i-a10-lcd-ch1-clk",	AW_LCD_CH1 },
	{ NULL, 0 }
};

struct aw_lcdclk_softc {
	enum aw_lcdclk_type	type;
	device_t		clkdev;
	bus_addr_t		reg;
	int			id;
};

#define	LCDCLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	LCDCLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	LCDCLK_MODIFY(sc, clr, set)	\
	CLKDEV_MODIFY_4((sc)->clkdev, (sc)->reg, (clr), (set))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_lcdclk_hwreset_assert(device_t dev, intptr_t id, bool value)
{
	struct aw_lcdclk_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if (sc->type != AW_LCD_CH0)
		return (ENXIO);

	DEVICE_LOCK(sc);
	error = LCDCLK_MODIFY(sc, CH0_LCD_RST, value ? 0 : CH0_LCD_RST);
	DEVICE_UNLOCK(sc);

	return (error);
}

static int
aw_lcdclk_hwreset_is_asserted(device_t dev, intptr_t id, bool *value)
{
	struct aw_lcdclk_softc *sc;
	uint32_t val;
	int error;

	sc = device_get_softc(dev);

	if (sc->type != AW_LCD_CH0)
		return (ENXIO);

	DEVICE_LOCK(sc);
	error = LCDCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	if (error)
		return (error);

	*value = (val & CH0_LCD_RST) != 0 ? false : true;

	return (0);
}

static int
aw_lcdclk_init(struct clknode *clk, device_t dev)
{
	struct aw_lcdclk_softc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	LCDCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	switch (sc->type) {
	case AW_LCD_CH0:
		index = (val & CH0_CLK_SRC_SEL) >> CH0_CLK_SRC_SEL_SHIFT;
		break;
	case AW_LCD_CH1:
		switch (sc->id) {
		case CLK_IDX_CH1_SCLK1:
			index = 0;
			break;
		case CLK_IDX_CH1_SCLK2:
			index = (val & CH1_SCLK2_SEL_SHIFT) >>
			    CH1_SCLK2_SEL_SHIFT;
			break;
		default:
			return (ENXIO);
		}
		break;
	default:
		return (ENXIO);
	}

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_lcdclk_set_mux(struct clknode *clk, int index)
{
	struct aw_lcdclk_softc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_LCD_CH0:
		DEVICE_LOCK(sc);
		LCDCLK_READ(sc, &val);
		val &= ~CH0_CLK_SRC_SEL;
		val |= (index << CH0_CLK_SRC_SEL_SHIFT);
		LCDCLK_WRITE(sc, val);
		DEVICE_UNLOCK(sc);
		break;
	case AW_LCD_CH1:
		switch (sc->id) {
		case CLK_IDX_CH1_SCLK2:
			DEVICE_LOCK(sc);
			LCDCLK_READ(sc, &val);
			val &= ~CH1_SCLK2_SEL;
			val |= (index << CH1_SCLK2_SEL_SHIFT);
			LCDCLK_WRITE(sc, val);
			DEVICE_UNLOCK(sc);
			break;
		default:
			return (ENXIO);
		}
		break;
	default:
		return (ENXIO);
	}

	return (0);
}

static int
aw_lcdclk_set_gate(struct clknode *clk, bool enable)
{
	struct aw_lcdclk_softc *sc;
	uint32_t val, mask;

	sc = clknode_get_softc(clk);

	switch (sc->type) {
	case AW_LCD_CH0:
		mask = CH0_SCLK_GATING;
		break;
	case AW_LCD_CH1:
		mask = (sc->id == CLK_IDX_CH1_SCLK1) ? CH1_SCLK1_GATING :
		    CH1_SCLK2_GATING;
		break;
	default:
		return (ENXIO);
	}

	DEVICE_LOCK(sc);
	LCDCLK_READ(sc, &val);
	if (enable)
		val |= mask;
	else
		val &= ~mask;
	LCDCLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static int
aw_lcdclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_lcdclk_softc *sc;
	uint32_t val, m, src_sel;

	sc = clknode_get_softc(clk);

	if (sc->type != AW_LCD_CH1)
		return (0);

	DEVICE_LOCK(sc);
	LCDCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	m = ((val & CH1_CLK_DIV_RATIO_M) >> CH1_CLK_DIV_RATIO_M_SHIFT) + 1;
	*freq = *freq / m;

	if (sc->id == CLK_IDX_CH1_SCLK1) {
		src_sel = (val & CH1_SCLK1_SEL) >> CH1_SCLK1_SEL_SHIFT;
		if (src_sel == CH1_SCLK1_SEL_SCLK2_DIV2)
			*freq /= 2;
	}

	return (0);
}

static void
calc_tcon_pll_integer(uint64_t fin, uint64_t fout, uint32_t *pm, uint32_t *pn)
{
	int64_t diff, fcur, best;
	int m, n;

	best = fout;
	for (m = TCON_PLL_M_MIN; m <= TCON_PLL_M_MAX; m++) {
		for (n = TCON_PLL_N_MIN; n <= TCON_PLL_N_MAX; n++) {
			fcur = (n * fin) / m;
			diff = (int64_t)fout - fcur;
			if (diff > 0 && diff < best) {
				best = diff;
				*pm = m;
				*pn = n;
			}
		}
	}
}

static int
calc_tcon_pll_fractional(uint64_t fin, uint64_t fout, int *clk_div)
{
	int m;

	/* Test for 1X match */
	for (m = TCON_PLL_M_MIN; m <= TCON_PLL_M_MAX; m++) {
		if (fout == (fin / m)) {
			*clk_div = m;
			return (CH0_CLK_SRC_SEL_PLL3_1X);
		}
	}

	/* Test for 2X match */
	for (m = TCON_PLL_M_MIN; m <= TCON_PLL_M_MAX; m++) {
		if (fout == ((fin * 2) / m)) {
			*clk_div = m;
			return (CH0_CLK_SRC_SEL_PLL3_2X);
		}
	}

	return (-1);
}

static int
calc_tcon_pll(uint64_t fin, uint64_t fout, uint64_t *pll_freq, int *tcon_pll_div)
{
	uint32_t m, m2, n, n2;
	uint64_t fsingle, fdouble;
	int src_sel;
	bool dbl;

	/* Test fractional freq first */
	src_sel = calc_tcon_pll_fractional(TCON_PLLREF_FRAC1, fout,
	    tcon_pll_div);
	if (src_sel != -1) {
		*pll_freq = TCON_PLLREF_FRAC1;
		return src_sel;
	}
	src_sel = calc_tcon_pll_fractional(TCON_PLLREF_FRAC2, fout,
	    tcon_pll_div);
	if (src_sel != -1) {
		*pll_freq = TCON_PLLREF_FRAC2;
		return src_sel;
	}

	m = n = m2 = n2 = 0;
	dbl = false;

	/* Find the frequency closes to the target dot clock, using
	 * both 1X and 2X PLL inputs as possible candidates.
	 */
	calc_tcon_pll_integer(TCON_PLLREF, fout, &m, &n);
	calc_tcon_pll_integer(TCON_PLLREF * 2, fout, &m2, &n2);

	fsingle = m ? (n * TCON_PLLREF) / m : 0;
	fdouble = m2 ? (n2 * TCON_PLLREF * 2) / m2 : 0;

	if (fdouble > fsingle) {
		dbl = true;
		m = m2;
		n = n2;
	}

	/* Set desired parent frequency */
	*pll_freq = n * TCON_PLLREF;
	*tcon_pll_div = m;

	/* Return the desired source clock */
	return (dbl ? CH0_CLK_SRC_SEL_PLL3_2X :
	    CH0_CLK_SRC_SEL_PLL3_1X);
}

static int
aw_lcdclk_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
    int flags, int *stop)
{
	struct aw_lcdclk_softc *sc;
	struct clknode *parent_clk;
	const char **parent_names;
	uint64_t pll_freq;
	uint32_t val, src_sel;
	int error, tcon_pll_div;

	sc = clknode_get_softc(clk);

	if (sc->type == AW_LCD_CH0) {
		*stop = 0;
		return (0);
	}

	if (sc->id != CLK_IDX_CH1_SCLK2)
		return (ENXIO);

	src_sel = calc_tcon_pll(fin, *fout, &pll_freq, &tcon_pll_div);

	parent_names = clknode_get_parent_names(clk);
	parent_clk = clknode_find_by_name(parent_names[src_sel]);

	if (parent_clk == NULL)
		return (ERANGE);

	/* Fetch input frequency */
	error = clknode_get_freq(parent_clk, &pll_freq);
	if (error != 0)
		return (error);

	*fout = pll_freq / tcon_pll_div;
	*stop = 1;

	if ((flags & CLK_SET_DRYRUN) != 0)
		return (0);

	/* Switch parent clock if necessary */
	error = clknode_set_parent_by_idx(clk, src_sel);
	if (error != 0)
		return (error);

	error = clknode_set_freq(parent_clk, pll_freq,
	    0, 0);
	if (error != 0)
		return (error);

	/* Fetch new input frequency */
	error = clknode_get_freq(parent_clk, &pll_freq);
	if (error != 0)
		return (error);

	*fout = pll_freq / tcon_pll_div;

	error = clknode_enable(parent_clk);
	if (error != 0)
		return (error);

	/* Set LCD divisor */
	DEVICE_LOCK(sc);
	LCDCLK_READ(sc, &val);
	val &= ~CH1_CLK_DIV_RATIO_M;
	val |= ((tcon_pll_div - 1) << CH1_CLK_DIV_RATIO_M_SHIFT);
	LCDCLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_lcdclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_lcdclk_init),
	CLKNODEMETHOD(clknode_set_gate,		aw_lcdclk_set_gate),
	CLKNODEMETHOD(clknode_set_mux,		aw_lcdclk_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_lcdclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		aw_lcdclk_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_lcdclk_clknode, aw_lcdclk_clknode_class,
    aw_lcdclk_clknode_methods, sizeof(struct aw_lcdclk_softc), clknode_class);

static int
aw_lcdclk_create(device_t dev, struct clkdom *clkdom,
    const char **parent_names, int parent_cnt, const char *name, int index)
{
	struct aw_lcdclk_softc *sc, *clk_sc;
	struct clknode_init_def def;
	struct clknode *clk;
	phandle_t node;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	memset(&def, 0, sizeof(def));
	def.id = index;
	def.name = name;
	def.parent_names = parent_names;
	def.parent_cnt = parent_cnt;

	clk = clknode_create(clkdom, &aw_lcdclk_clknode_class, &def);
	if (clk == NULL) {
		device_printf(dev, "cannot create clknode\n");
		return (ENXIO);
	}

	clk_sc = clknode_get_softc(clk);
	clk_sc->type = sc->type;
	clk_sc->reg = sc->reg;
	clk_sc->clkdev = sc->clkdev;
	clk_sc->id = index;

	clknode_register(clkdom, clk);

	return (0);
}

static int
aw_lcdclk_probe(device_t dev)
{
	enum aw_lcdclk_type type;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	switch (type) {
	case AW_LCD_CH0:
		device_set_desc(dev, "Allwinner LCD CH0 Clock");
		break;
	case AW_LCD_CH1:
		device_set_desc(dev, "Allwinner LCD CH1 Clock");
		break;
	default:
		return (ENXIO);
	}

	return (BUS_PROBE_DEFAULT);
}

static int
aw_lcdclk_attach(device_t dev)
{
	struct aw_lcdclk_softc *sc;
	struct clkdom *clkdom;
	clk_t clk_parent;
	bus_size_t psize;
	phandle_t node;
	uint32_t *indices;
	const char **parent_names;
	const char **names;
	int error, ncells, nout, i;

	sc = device_get_softc(dev);
	sc->clkdev = device_get_parent(dev);
	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

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

	parent_names = malloc(sizeof(char *) * ncells, M_OFWPROP, M_WAITOK);
	for (i = 0; i < ncells; i++) {
		error = clk_get_by_ofw_index(dev, 0, i, &clk_parent);
		if (error != 0) {
			device_printf(dev, "cannot get clock %d\n", i);
			goto fail;
		}
		parent_names[i] = clk_get_name(clk_parent);
		clk_release(clk_parent);
	}

	nout = clk_parse_ofw_out_names(dev, node, &names, &indices);
	if (nout == 0) {
		device_printf(dev, "no clock outputs found\n");
		return (error);
	}

	clkdom = clkdom_create(dev);

	for (i = 0; i < nout; i++) {
		error = aw_lcdclk_create(dev, clkdom, parent_names, ncells,
		    names[i], nout == 1 ? 1 : i);
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

	if (sc->type == AW_LCD_CH0)
		hwreset_register_ofw_provider(dev);

	OF_prop_free(parent_names);
	return (0);

fail:
	OF_prop_free(parent_names);
	return (error);
}

static device_method_t aw_lcdclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_lcdclk_probe),
	DEVMETHOD(device_attach,	aw_lcdclk_attach),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	aw_lcdclk_hwreset_assert),
	DEVMETHOD(hwreset_is_asserted,	aw_lcdclk_hwreset_is_asserted),

	DEVMETHOD_END
};

static driver_t aw_lcdclk_driver = {
	"aw_lcdclk",
	aw_lcdclk_methods,
	sizeof(struct aw_lcdclk_softc)
};

static devclass_t aw_lcdclk_devclass;

EARLY_DRIVER_MODULE(aw_lcdclk, simplebus, aw_lcdclk_driver,
    aw_lcdclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
