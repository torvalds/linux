/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <sys/kobj.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_gate.h>
#include <dev/extres/clk/clk_mux.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <gnu/dts/include/dt-bindings/clock/tegra124-car.h>

#include "clkdev_if.h"
#include "hwreset_if.h"
#include "tegra124_car.h"

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-car",	1},
	{NULL,		 	0},
};

#define	PLIST(x) static const char *x[]

/* Pure multiplexer. */
#define	MUX(_id, cname, plists, o, s, w)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = plists,					\
	.clkdef.parent_cnt = nitems(plists),				\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.shift  = s,							\
	.width = w,							\
}

/* Fractional divider (7.1). */
#define	DIV7_1(_id, cname, plist, o, s)					\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags =  CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.i_shift = (s) + 1,						\
	.i_width = 7,							\
	.f_shift = s,							\
	.f_width = 1,							\
}

/* Integer divider. */
#define	DIV(_id, cname, plist, o, s, w, f)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags =  CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.i_shift = s,							\
	.i_width = w,							\
	.div_flags = f,							\
}

/* Gate in PLL block. */
#define	GATE_PLL(_id, cname, plist, o, s)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.shift = s,							\
	.mask = 3,							\
	.on_value = 3,							\
	.off_value = 0,							\
}

/* Standard gate. */
#define	GATE(_id, cname, plist, o, s)					\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.shift = s,							\
	.mask = 1,							\
	.on_value = 1,							\
	.off_value = 0,							\
}

/* Inverted gate. */
#define	GATE_INV(_id, cname, plist, o, s)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.offset = o,							\
	.shift = s,							\
	.mask = 1,							\
	.on_value = 0,							\
	.off_value = 1,							\
}

/* Fixed rate clock. */
#define	FRATE(_id, cname, _freq)					\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = NULL,					\
	.clkdef.parent_cnt = 0,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.freq = _freq,							\
}

/* Fixed rate multipier/divider. */
#define	FACT(_id, cname, pname, _mult, _div)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){pname},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.mult = _mult,							\
	.div = _div,							\
}

static uint32_t osc_freqs[16] = {
	 [0] =  13000000,
	 [1] =  16800000,
	 [4] =  19200000,
	 [5] =  38400000,
	 [8] =  12000000,
	 [9] =  48000000,
	[12] = 260000000,
};


/* Parent lists. */
PLIST(mux_pll_srcs) = {"osc_div_clk", NULL, "pllP_out0", NULL}; /* FIXME */
PLIST(mux_plle_src1) = {"osc_div_clk", "pllP_out0"};
PLIST(mux_plle_src) = {"pllE_src1", "pllREFE_out"};
PLIST(mux_plld_out0_plld2_out0) = {"pllD_out0", "pllD2_out0"};
PLIST(mux_xusb_hs) = {"xusb_ss_div2", "pllU_60"};
PLIST(mux_xusb_ss) = {"pc_xusb_ss", "osc_div_clk"};


/* Clocks ajusted online. */
static struct clk_fixed_def fixed_clk_m =
	FRATE(TEGRA124_CLK_CLK_M, "clk_m", 12000000);
static struct clk_fixed_def fixed_osc_div_clk =
	FACT(0, "osc_div_clk", "clk_m", 1, 1);

static struct clk_fixed_def tegra124_fixed_clks[] = {
	/* Core clocks. */
	FRATE(0, "clk_s", 32768),
	FACT(0, "clk_m_div2", "clk_m", 1, 2),
	FACT(0, "clk_m_div4", "clk_m", 1, 3),
	FACT(0, "pllU_60", "pllU_out", 1, 8),
	FACT(0, "pllU_48", "pllU_out", 1, 10),
	FACT(0, "pllU_12", "pllU_out", 1, 40),
	FACT(TEGRA124_CLK_PLL_D_OUT0, "pllD_out0", "pllD_out", 1, 2),
	FACT(TEGRA124_CLK_PLL_D2_OUT0, "pllD2_out0", "pllD2_out", 1, 1),
	FACT(0, "pllX_out0", "pllX_out", 1, 2),
	FACT(0, "pllC_UD", "pllC_out0", 1, 1),
	FACT(0, "pllM_UD", "pllM_out0", 1, 1),

	/* Audio clocks. */
	FRATE(0, "audio0", 10000000),
	FRATE(0, "audio1", 10000000),
	FRATE(0, "audio2", 10000000),
	FRATE(0, "audio3", 10000000),
	FRATE(0, "audio4", 10000000),
	FRATE(0, "ext_vimclk", 10000000),

	/* XUSB */
	FACT(TEGRA124_CLK_XUSB_SS_DIV2, "xusb_ss_div2", "xusb_ss", 1, 2),

};


static struct clk_mux_def tegra124_mux_clks[] = {
	/* Core clocks. */
	MUX(0, "pllD2_src", mux_pll_srcs, PLLD2_BASE, 25, 2),
	MUX(0, "pllDP_src", mux_pll_srcs, PLLDP_BASE, 25, 2),
	MUX(0, "pllC4_src", mux_pll_srcs, PLLC4_BASE, 25, 2),
	MUX(0, "pllE_src1", mux_plle_src1, PLLE_AUX, 2, 1),
	MUX(0, "pllE_src",  mux_plle_src, PLLE_AUX, 28, 1),

	/* Base peripheral clocks. */
	MUX(0, "dsia_mux", mux_plld_out0_plld2_out0, PLLD_BASE, 25, 1),
	MUX(0, "dsib_mux", mux_plld_out0_plld2_out0, PLLD2_BASE, 25, 1),

	/* USB. */
	MUX(TEGRA124_CLK_XUSB_HS_SRC, "xusb_hs", mux_xusb_hs, CLK_SOURCE_XUSB_SS, 25, 1),
	MUX(0, "xusb_ss_mux", mux_xusb_ss, CLK_SOURCE_XUSB_SS, 24, 1),

};


static struct clk_gate_def tegra124_gate_clks[] = {
	/* Core clocks. */
	GATE_PLL(0, "pllC_out1", "pllC_out1_div", PLLC_OUT, 0),
	GATE_PLL(0, "pllM_out1", "pllM_out1_div", PLLM_OUT, 0),
	GATE_PLL(TEGRA124_CLK_PLL_U_480M, "pllU_480", "pllU_out", PLLU_BASE, 22),
	GATE_PLL(0, "pllP_outX0", "pllP_outX0_div", PLLP_RESHIFT, 0),
	GATE_PLL(0, "pllP_out1", "pllP_out1_div", PLLP_OUTA, 0),
	GATE_PLL(0, "pllP_out2", "pllP_out2_div", PLLP_OUTA, 16),
	GATE_PLL(0, "pllP_out3", "pllP_out3_div", PLLP_OUTB, 0),
	GATE_PLL(0, "pllP_out4", "pllP_out4_div", PLLP_OUTB, 16),
	GATE_PLL(0, "pllP_out5", "pllP_out5_div", PLLP_OUTC, 16),
	GATE_PLL(0, "pllA_out0", "pllA_out1_div", PLLA_OUT, 0),

	/* Base peripheral clocks. */
	GATE(TEGRA124_CLK_CML0, "cml0", "pllE_out0", PLLE_AUX, 0),
	GATE(TEGRA124_CLK_CML1, "cml1", "pllE_out0", PLLE_AUX, 1),
	GATE_INV(TEGRA124_CLK_HCLK, "hclk", "hclk_div", CLK_SYSTEM_RATE, 7),
	GATE_INV(TEGRA124_CLK_PCLK, "pclk", "pclk_div", CLK_SYSTEM_RATE, 3),
};

static struct clk_div_def tegra124_div_clks[] = {
	/* Core clocks. */
	DIV7_1(0, "pllC_out1_div", "pllC_out0", PLLC_OUT, 2),
	DIV7_1(0, "pllM_out1_div", "pllM_out0", PLLM_OUT, 8),
	DIV7_1(0, "pllP_outX0_div", "pllP_out0", PLLP_RESHIFT, 2),
	DIV7_1(0, "pllP_out1_div", "pllP_out0", PLLP_OUTA, 8),
	DIV7_1(0, "pllP_out2_div", "pllP_out0", PLLP_OUTA, 24),
	DIV7_1(0, "pllP_out3_div", "pllP_out0", PLLP_OUTB, 8),
	DIV7_1(0, "pllP_out4_div", "pllP_out0", PLLP_OUTB, 24),
	DIV7_1(0, "pllP_out5_div", "pllP_out0", PLLP_OUTC, 24),
	DIV7_1(0, "pllA_out1_div", "pllA_out", PLLA_OUT, 8),

	/* Base peripheral clocks. */
	DIV(0, "hclk_div", "sclk", CLK_SYSTEM_RATE, 4, 2, 0),
	DIV(0, "pclk_div", "hclk", CLK_SYSTEM_RATE, 0, 2, 0),
};

/* Initial setup table. */
static struct  tegra124_init_item clk_init_table[] = {
	/* clock, partent, frequency, enable */
	{"uarta", "pllP_out0", 408000000, 0},
	{"uartb", "pllP_out0", 408000000, 0},
	{"uartc", "pllP_out0", 408000000, 0},
	{"uartd", "pllP_out0", 408000000, 0},
	{"pllA_out", NULL, 282240000, 1},
	{"pllA_out0", NULL, 11289600, 1},
	{"extperiph1", "pllA_out0", 0, 1},
	{"i2s0", "pllA_out0", 11289600, 0},
	{"i2s1", "pllA_out0", 11289600, 0},
	{"i2s2", "pllA_out0", 11289600, 0},
	{"i2s3", "pllA_out0", 11289600, 0},
	{"i2s4", "pllA_out0", 11289600, 0},
	{"vde", "pllP_out0", 0, 0},
	{"host1x", "pllP_out0", 136000000, 1},
	{"sclk", "pllP_out2", 102000000, 1},
	{"dvfs_soc", "pllP_out0", 51000000, 1},
	{"dvfs_ref", "pllP_out0", 51000000, 1},
	{"pllC_out0", NULL, 600000000, 0},
	{"pllC_out1", NULL, 100000000, 0},
	{"spi4", "pllP_out0", 12000000, 1},
	{"tsec", "pllC3_out0", 0, 0},
	{"msenc", "pllC3_out0", 0, 0},
	{"pllREFE_out", NULL, 672000000, 0},
	{"pc_xusb_ss", "pllU_480", 120000000, 0},
	{"xusb_ss", "pc_xusb_ss", 120000000, 0},
	{"pc_xusb_fs", "pllU_48", 48000000, 0},
	{"xusb_hs", "pllU_60", 60000000, 0},
	{"pc_xusb_falcon", "pllREFE_out", 224000000, 0},
	{"xusb_core_host", "pllREFE_out", 112000000, 0},
	{"sata", "pllP_out0", 102000000, 0},
	{"sata_oob", "pllP_out0", 204000000, 0},
	{"sata_cold", NULL, 0, 1},
	{"emc", NULL, 0, 1},
	{"mselect", NULL, 0, 1},
	{"csite", NULL, 0, 1},
	{"tsensor", "clk_m", 400000, 0},

	/* tegra124 only*/
	{"soc_therm", "pllP_out0", 51000000, 0},
	{"cclk_g", NULL, 0, 1},
	{"hda", "pllP_out0", 102000000, 0},
	{"hda2codec_2x", "pllP_out0", 48000000, 0},
};

static void
init_divs(struct tegra124_car_softc *sc, struct clk_div_def *clks, int nclks)
{
	int i, rv;

	for (i = 0; i < nclks; i++) {
		rv = clknode_div_register(sc->clkdom, clks + i);
		if (rv != 0)
			panic("clk_div_register failed");
	}
}

static void
init_gates(struct tegra124_car_softc *sc, struct clk_gate_def *clks, int nclks)
{
	int i, rv;


	for (i = 0; i < nclks; i++) {
		rv = clknode_gate_register(sc->clkdom, clks + i);
		if (rv != 0)
			panic("clk_gate_register failed");
	}
}

static void
init_muxes(struct tegra124_car_softc *sc, struct clk_mux_def *clks, int nclks)
{
	int i, rv;


	for (i = 0; i < nclks; i++) {
		rv = clknode_mux_register(sc->clkdom, clks + i);
		if (rv != 0)
			panic("clk_mux_register failed");
	}
}

static void
init_fixeds(struct tegra124_car_softc *sc, struct clk_fixed_def *clks,
    int nclks)
{
	int i, rv;
	uint32_t val;
	int osc_idx;

	CLKDEV_READ_4(sc->dev, OSC_CTRL, &val);
	osc_idx = val >> OSC_CTRL_OSC_FREQ_SHIFT;
	fixed_clk_m.freq = osc_freqs[osc_idx];
	if (fixed_clk_m.freq == 0)
		panic("Undefined input frequency");
	rv = clknode_fixed_register(sc->clkdom, &fixed_clk_m);
	if (rv != 0) panic("clk_fixed_register failed");

	val = (val >> OSC_CTRL_PLL_REF_DIV_SHIFT) & 3;
	fixed_osc_div_clk.div = 1 << val;
	rv = clknode_fixed_register(sc->clkdom, &fixed_osc_div_clk);
	if (rv != 0) panic("clk_fixed_register failed");

	for (i = 0; i < nclks; i++) {
		rv = clknode_fixed_register(sc->clkdom, clks + i);
		if (rv != 0)
			panic("clk_fixed_register failed");
	}
}

static void
postinit_clock(struct tegra124_car_softc *sc)
{
	int i;
	struct tegra124_init_item *tbl;
	struct clknode *clknode;
	int rv;

	for (i = 0; i < nitems(clk_init_table); i++) {
		tbl = &clk_init_table[i];

		clknode =  clknode_find_by_name(tbl->name);
		if (clknode == NULL) {
			device_printf(sc->dev, "Cannot find clock %s\n",
			    tbl->name);
			continue;
		}
		if (tbl->parent != NULL) {
			rv = clknode_set_parent_by_name(clknode, tbl->parent);
			if (rv != 0) {
				device_printf(sc->dev,
				    "Cannot set parent for %s (to %s): %d\n",
				    tbl->name, tbl->parent, rv);
				continue;
			}
		}
		if (tbl->frequency != 0) {
			rv = clknode_set_freq(clknode, tbl->frequency, 0 , 9999);
			if (rv != 0) {
				device_printf(sc->dev,
				    "Cannot set frequency for %s: %d\n",
				    tbl->name, rv);
				continue;
			}
		}
		if (tbl->enable!= 0) {
			rv = clknode_enable(clknode);
			if (rv != 0) {
				device_printf(sc->dev,
				    "Cannot enable %s: %d\n", tbl->name, rv);
				continue;
			}
		}
	}
}

static void
register_clocks(device_t dev)
{
	struct tegra124_car_softc *sc;

	sc = device_get_softc(dev);
	sc->clkdom = clkdom_create(dev);
	if (sc->clkdom == NULL)
		panic("clkdom == NULL");

	tegra124_init_plls(sc);
	init_fixeds(sc, tegra124_fixed_clks, nitems(tegra124_fixed_clks));
	init_muxes(sc, tegra124_mux_clks, nitems(tegra124_mux_clks));
	init_divs(sc, tegra124_div_clks, nitems(tegra124_div_clks));
	init_gates(sc, tegra124_gate_clks, nitems(tegra124_gate_clks));
	tegra124_periph_clock(sc);
	tegra124_super_mux_clock(sc);
	clkdom_finit(sc->clkdom);
	clkdom_xlock(sc->clkdom);
	postinit_clock(sc);
	clkdom_unlock(sc->clkdom);
	if (bootverbose)
		clkdom_dump(sc->clkdom);
}

static int
tegra124_car_clkdev_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
{
	struct tegra124_car_softc *sc;

	sc = device_get_softc(dev);
	*val = bus_read_4(sc->mem_res, addr);
	return (0);
}

static int
tegra124_car_clkdev_write_4(device_t dev, bus_addr_t addr, uint32_t val)
{
	struct tegra124_car_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->mem_res, addr, val);
	return (0);
}

static int
tegra124_car_clkdev_modify_4(device_t dev, bus_addr_t addr, uint32_t clear_mask,
    uint32_t set_mask)
{
	struct tegra124_car_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);
	reg = bus_read_4(sc->mem_res, addr);
	reg &= ~clear_mask;
	reg |= set_mask;
	bus_write_4(sc->mem_res, addr, reg);
	return (0);
}

static void
tegra124_car_clkdev_device_lock(device_t dev)
{
	struct tegra124_car_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
}

static void
tegra124_car_clkdev_device_unlock(device_t dev)
{
	struct tegra124_car_softc *sc;

	sc = device_get_softc(dev);
	mtx_unlock(&sc->mtx);
}

static int
tegra124_car_detach(device_t dev)
{

	device_printf(dev, "Error: Clock driver cannot be detached\n");
	return (EBUSY);
}

static int
tegra124_car_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Tegra Clock Driver");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
tegra124_car_attach(device_t dev)
{
	struct tegra124_car_softc *sc = device_get_softc(dev);
	int rid, rv;

	sc->dev = dev;

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);
	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	/* Resource setup. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->mem_res) {
		device_printf(dev, "cannot allocate memory resource\n");
		rv = ENXIO;
		goto fail;
	}

	register_clocks(dev);
	hwreset_register_ofw_provider(dev);
	return (0);

fail:
	if (sc->mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (rv);
}

static int
tegra124_car_hwreset_assert(device_t dev, intptr_t id, bool value)
{
	struct tegra124_car_softc *sc = device_get_softc(dev);

	return (tegra124_hwreset_by_idx(sc, id, value));
}

static device_method_t tegra124_car_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra124_car_probe),
	DEVMETHOD(device_attach,	tegra124_car_attach),
	DEVMETHOD(device_detach,	tegra124_car_detach),

	/* Clkdev  interface*/
	DEVMETHOD(clkdev_read_4,	tegra124_car_clkdev_read_4),
	DEVMETHOD(clkdev_write_4,	tegra124_car_clkdev_write_4),
	DEVMETHOD(clkdev_modify_4,	tegra124_car_clkdev_modify_4),
	DEVMETHOD(clkdev_device_lock,	tegra124_car_clkdev_device_lock),
	DEVMETHOD(clkdev_device_unlock,	tegra124_car_clkdev_device_unlock),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	tegra124_car_hwreset_assert),

	DEVMETHOD_END
};

static devclass_t tegra124_car_devclass;
static DEFINE_CLASS_0(car, tegra124_car_driver, tegra124_car_methods,
    sizeof(struct tegra124_car_softc));
EARLY_DRIVER_MODULE(tegra124_car, simplebus, tegra124_car_driver,
    tegra124_car_devclass, NULL, NULL, BUS_PASS_TIMER);
