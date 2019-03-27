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
 * Allwinner HDMI clock
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

#include <dev/extres/clk/clk_mux.h>
#include <dev/extres/clk/clk_gate.h>

#include "clkdev_if.h"

#define	SCLK_GATING		(1 << 31)
#define	CLK_SRC_SEL		(0x3 << 24)
#define	CLK_SRC_SEL_SHIFT	24
#define	CLK_SRC_SEL_MAX		0x3
#define	CLK_RATIO_N		(0x3 << 16)
#define	CLK_RATIO_N_SHIFT	16
#define	CLK_RATIO_N_MAX		0x3
#define	CLK_RATIO_M		(0x1f << 0)
#define	CLK_RATIO_M_SHIFT	0
#define	CLK_RATIO_M_MAX		0x1f

#define	CLK_IDX_PLL3_1X		0

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-hdmi-clk",	1 },
	{ NULL, 0 }
};

struct aw_hdmiclk_sc {
	device_t	clkdev;
	bus_addr_t	reg;
};

#define	HDMICLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	HDMICLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_hdmiclk_init(struct clknode *clk, device_t dev)
{
	struct aw_hdmiclk_sc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	/* Select PLL3(1X) clock source */
	index = CLK_IDX_PLL3_1X;

	DEVICE_LOCK(sc);
	HDMICLK_READ(sc, &val);
	val &= ~CLK_SRC_SEL;
	val |= (index << CLK_SRC_SEL_SHIFT);
	HDMICLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_hdmiclk_set_mux(struct clknode *clk, int index)
{
	struct aw_hdmiclk_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	if (index < 0 || index > CLK_SRC_SEL_MAX)
		return (ERANGE);

	DEVICE_LOCK(sc);
	HDMICLK_READ(sc, &val);
	val &= ~CLK_SRC_SEL;
	val |= (index << CLK_SRC_SEL_SHIFT);
	HDMICLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static int
aw_hdmiclk_set_gate(struct clknode *clk, bool enable)
{
	struct aw_hdmiclk_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	HDMICLK_READ(sc, &val);
	if (enable)
		val |= SCLK_GATING;
	else
		val &= ~SCLK_GATING;
	HDMICLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static int
aw_hdmiclk_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct aw_hdmiclk_sc *sc;
	uint32_t val, m, n;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	HDMICLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	n = 1 << ((val & CLK_RATIO_N) >> CLK_RATIO_N_SHIFT);
	m = ((val & CLK_RATIO_M) >> CLK_RATIO_M_SHIFT) + 1;

	*freq = *freq / n / m;

	return (0);
}

static int
aw_hdmiclk_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
    int flags, int *stop)
{
	struct aw_hdmiclk_sc *sc;
	uint32_t val, m, n, best_m, best_n;
	uint64_t cur_freq;
	int64_t best_diff, cur_diff;

	sc = clknode_get_softc(clk);
	best_n = best_m = 0;
	best_diff = (int64_t)*fout; 

	for (n = 0; n <= CLK_RATIO_N_MAX; n++)
		for (m = 0; m <= CLK_RATIO_M_MAX; m++) {
			cur_freq = fin / (1 << n) / (m + 1);
			cur_diff = (int64_t)*fout - cur_freq;
			if (cur_diff >= 0 && cur_diff < best_diff) {
				best_diff = cur_diff;
				best_m = m;
				best_n = n;
			}
		}

	if (best_diff == (int64_t)*fout)
		return (ERANGE);

	*fout = fin / (1 << best_n) / (best_m + 1);
	*stop = 1;

	if ((flags & CLK_SET_DRYRUN) != 0)
		return (0);

	DEVICE_LOCK(sc);
	HDMICLK_READ(sc, &val);
	val &= ~(CLK_RATIO_N | CLK_RATIO_M);
	val |= (best_n << CLK_RATIO_N_SHIFT);
	val |= (best_m << CLK_RATIO_M_SHIFT);
	HDMICLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_hdmiclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_hdmiclk_init),
	CLKNODEMETHOD(clknode_set_gate,		aw_hdmiclk_set_gate),
	CLKNODEMETHOD(clknode_set_mux,		aw_hdmiclk_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_hdmiclk_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		aw_hdmiclk_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_hdmiclk_clknode, aw_hdmiclk_clknode_class,
    aw_hdmiclk_clknode_methods, sizeof(struct aw_hdmiclk_sc), clknode_class);

static int
aw_hdmiclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner HDMI Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_hdmiclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_hdmiclk_sc *sc;
	struct clkdom *clkdom;
	struct clknode *clk;
	clk_t clk_parent;
	bus_addr_t paddr;
	bus_size_t psize;
	phandle_t node;
	int error;

	node = ofw_bus_get_node(dev);

	if (ofw_reg_to_paddr(node, 0, &paddr, &psize, NULL) != 0) {
		device_printf(dev, "cannot parse 'reg' property\n");
		return (ENXIO);
	}

	clkdom = clkdom_create(dev);

	error = clk_get_by_ofw_index(dev, 0, 0, &clk_parent);
	if (error != 0) {
		device_printf(dev, "cannot parse clock parent\n");
		return (ENXIO);
	}

	memset(&def, 0, sizeof(def));
	error = clk_parse_ofw_clk_name(dev, node, &def.name);
	if (error != 0) {
		device_printf(dev, "cannot parse clock name\n");
		error = ENXIO;
		goto fail;
	}
	def.id = 1;
	def.parent_names = malloc(sizeof(char *), M_OFWPROP, M_WAITOK);
	def.parent_names[0] = clk_get_name(clk_parent);
	def.parent_cnt = 1;

	clk = clknode_create(clkdom, &aw_hdmiclk_clknode_class, &def);
	if (clk == NULL) {
		device_printf(dev, "cannot create clknode\n");
		error = ENXIO;
		goto fail;
	}

	sc = clknode_get_softc(clk);
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

static device_method_t aw_hdmiclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_hdmiclk_probe),
	DEVMETHOD(device_attach,	aw_hdmiclk_attach),

	DEVMETHOD_END
};

static driver_t aw_hdmiclk_driver = {
	"aw_hdmiclk",
	aw_hdmiclk_methods,
	0
};

static devclass_t aw_hdmiclk_devclass;

EARLY_DRIVER_MODULE(aw_hdmiclk, simplebus, aw_hdmiclk_driver,
    aw_hdmiclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
