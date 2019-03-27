/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@freebsd.org>
 * Copyright (c) 2018 Greg V <greg@unrelenting.technology>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_mux.h>

#include <arm64/rockchip/clk/rk_cru.h>

/* GATES */

#define	PCLK_PMU		20
#define	PCLK_GPIO0_PMU		23
#define	PCLK_GPIO1_PMU		24
#define	PCLK_I2C0_PMU		27
#define	PCLK_I2C4_PMU		28
#define	PCLK_I2C8_PMU		29

static struct rk_cru_gate rk3399_pmu_gates[] = {
	/* PMUCRU_CLKGATE_CON1 */
	CRU_GATE(PCLK_PMU, "pclk_pmu", "pclk_pmu_src", 0x104, 0)
	CRU_GATE(PCLK_GPIO0_PMU, "pclk_gpio0_pmu", "pclk_pmu_src", 0x104, 3)
	CRU_GATE(PCLK_GPIO1_PMU, "pclk_gpio1_pmu", "pclk_pmu_src", 0x104, 4)
	CRU_GATE(PCLK_I2C0_PMU, "pclk_i2c0_pmu", "pclk_pmu_src", 0x104, 7)
	CRU_GATE(PCLK_I2C4_PMU, "pclk_i2c4_pmu", "pclk_pmu_src", 0x104, 8)
	CRU_GATE(PCLK_I2C8_PMU, "pclk_i2c8_pmu", "pclk_pmu_src", 0x104, 9)
};


/*
 * PLLs
 */

#define PLL_PPLL	1

static struct rk_clk_pll_rate rk3399_pll_rates[] = {
	{
		.freq = 2208000000,
		.refdiv = 1,
		.fbdiv = 92,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2184000000,
		.refdiv = 1,
		.fbdiv = 91,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2160000000,
		.refdiv = 1,
		.fbdiv = 90,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2136000000,
		.refdiv = 1,
		.fbdiv = 89,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2112000000,
		.refdiv = 1,
		.fbdiv = 88,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2088000000,
		.refdiv = 1,
		.fbdiv = 87,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2064000000,
		.refdiv = 1,
		.fbdiv = 86,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2040000000,
		.refdiv = 1,
		.fbdiv = 85,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 2016000000,
		.refdiv = 1,
		.fbdiv = 84,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1992000000,
		.refdiv = 1,
		.fbdiv = 83,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1968000000,
		.refdiv = 1,
		.fbdiv = 82,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1944000000,
		.refdiv = 1,
		.fbdiv = 81,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1920000000,
		.refdiv = 1,
		.fbdiv = 80,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1896000000,
		.refdiv = 1,
		.fbdiv = 79,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1872000000,
		.refdiv = 1,
		.fbdiv = 78,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1848000000,
		.refdiv = 1,
		.fbdiv = 77,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1824000000,
		.refdiv = 1,
		.fbdiv = 76,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1800000000,
		.refdiv = 1,
		.fbdiv = 75,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1776000000,
		.refdiv = 1,
		.fbdiv = 74,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1752000000,
		.refdiv = 1,
		.fbdiv = 73,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1728000000,
		.refdiv = 1,
		.fbdiv = 72,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1704000000,
		.refdiv = 1,
		.fbdiv = 71,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1680000000,
		.refdiv = 1,
		.fbdiv = 70,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1656000000,
		.refdiv = 1,
		.fbdiv = 69,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1632000000,
		.refdiv = 1,
		.fbdiv = 68,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1608000000,
		.refdiv = 1,
		.fbdiv = 67,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1600000000,
		.refdiv = 3,
		.fbdiv = 200,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1584000000,
		.refdiv = 1,
		.fbdiv = 66,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1560000000,
		.refdiv = 1,
		.fbdiv = 65,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1536000000,
		.refdiv = 1,
		.fbdiv = 64,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1512000000,
		.refdiv = 1,
		.fbdiv = 63,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1488000000,
		.refdiv = 1,
		.fbdiv = 62,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1464000000,
		.refdiv = 1,
		.fbdiv = 61,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1440000000,
		.refdiv = 1,
		.fbdiv = 60,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1416000000,
		.refdiv = 1,
		.fbdiv = 59,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1392000000,
		.refdiv = 1,
		.fbdiv = 58,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1368000000,
		.refdiv = 1,
		.fbdiv = 57,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1344000000,
		.refdiv = 1,
		.fbdiv = 56,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1320000000,
		.refdiv = 1,
		.fbdiv = 55,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1296000000,
		.refdiv = 1,
		.fbdiv = 54,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1272000000,
		.refdiv = 1,
		.fbdiv = 53,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1248000000,
		.refdiv = 1,
		.fbdiv = 52,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1200000000,
		.refdiv = 1,
		.fbdiv = 50,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1188000000,
		.refdiv = 2,
		.fbdiv = 99,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1104000000,
		.refdiv = 1,
		.fbdiv = 46,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1100000000,
		.refdiv = 12,
		.fbdiv = 550,
		.postdiv1 = 1,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1008000000,
		.refdiv = 1,
		.fbdiv = 84,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 1000000000,
		.refdiv = 1,
		.fbdiv = 125,
		.postdiv1 = 3,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 984000000,
		.refdiv = 1,
		.fbdiv = 82,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 960000000,
		.refdiv = 1,
		.fbdiv = 80,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 936000000,
		.refdiv = 1,
		.fbdiv = 78,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 912000000,
		.refdiv = 1,
		.fbdiv = 76,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 900000000,
		.refdiv = 4,
		.fbdiv = 300,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 888000000,
		.refdiv = 1,
		.fbdiv = 74,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 864000000,
		.refdiv = 1,
		.fbdiv = 72,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 840000000,
		.refdiv = 1,
		.fbdiv = 70,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 816000000,
		.refdiv = 1,
		.fbdiv = 68,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 800000000,
		.refdiv = 1,
		.fbdiv = 100,
		.postdiv1 = 3,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 700000000,
		.refdiv = 6,
		.fbdiv = 350,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 696000000,
		.refdiv = 1,
		.fbdiv = 58,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 676000000,
		.refdiv = 3,
		.fbdiv = 169,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 600000000,
		.refdiv = 1,
		.fbdiv = 75,
		.postdiv1 = 3,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 594000000,
		.refdiv = 1,
		.fbdiv = 99,
		.postdiv1 = 4,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 533250000,
		.refdiv = 8,
		.fbdiv = 711,
		.postdiv1 = 4,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 504000000,
		.refdiv = 1,
		.fbdiv = 63,
		.postdiv1 = 3,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 500000000,
		.refdiv = 6,
		.fbdiv = 250,
		.postdiv1 = 2,
		.postdiv2 = 1,
		.dsmpd = 1,
	},
	{
		.freq = 408000000,
		.refdiv = 1,
		.fbdiv = 68,
		.postdiv1 = 2,
		.postdiv2 = 2,
		.dsmpd = 1,
	},
	{
		.freq = 312000000,
		.refdiv = 1,
		.fbdiv = 52,
		.postdiv1 = 2,
		.postdiv2 = 2,
		.dsmpd = 1,
	},
	{
		.freq = 297000000,
		.refdiv = 1,
		.fbdiv = 99,
		.postdiv1 = 4,
		.postdiv2 = 2,
		.dsmpd = 1,
	},
	{
		.freq = 216000000,
		.refdiv = 1,
		.fbdiv = 72,
		.postdiv1 = 4,
		.postdiv2 = 2,
		.dsmpd = 1,
	},
	{
		.freq = 148500000,
		.refdiv = 1,
		.fbdiv = 99,
		.postdiv1 = 4,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 106500000,
		.refdiv = 1,
		.fbdiv = 71,
		.postdiv1 = 4,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 96000000,
		.refdiv = 1,
		.fbdiv = 64,
		.postdiv1 = 4,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 74250000,
		.refdiv = 2,
		.fbdiv = 99,
		.postdiv1 = 4,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 65000000,
		.refdiv = 1,
		.fbdiv = 65,
		.postdiv1 = 6,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 54000000,
		.refdiv = 1,
		.fbdiv = 54,
		.postdiv1 = 6,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{
		.freq = 27000000,
		.refdiv = 1,
		.fbdiv = 27,
		.postdiv1 = 6,
		.postdiv2 = 4,
		.dsmpd = 1,
	},
	{},
};

static const char *pll_parents[] = {"xin24m"};

static struct rk_clk_pll_def ppll = {
	.clkdef = {
		.id = PLL_PPLL,
		.name = "ppll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x00,

	.rates = rk3399_pll_rates,
};

static const char *pmu_parents[] = {"ppll"};

#define	PCLK_PMU_SRC			19

static struct rk_clk_composite_def pclk_pmu_src = {
	.clkdef = {
		.id = PCLK_PMU_SRC,
		.name = "pclk_pmu_src",
		.parent_names = pmu_parents,
		.parent_cnt = nitems(pmu_parents),
	},
	/* PMUCRU_CLKSEL_CON0 */
	.muxdiv_offset = 0x80,

	.div_shift = 0,
	.div_width = 5,
};


#define	SCLK_I2C0_PMU	9
#define	SCLK_I2C4_PMU	10
#define	SCLK_I2C8_PMU	11

static struct rk_clk_composite_def i2c0 = {
	.clkdef = {
		.id = SCLK_I2C0_PMU,
		.name = "clk_i2c0_pmu",
		.parent_names = pmu_parents,
		.parent_cnt = nitems(pmu_parents),
	},
	/* PMUCRU_CLKSEL_CON2 */
	.muxdiv_offset = 0x88,

	.div_shift = 0,
	.div_width = 7,

	/* PMUCRU_CLKGATE_CON0 */
	.gate_offset = 0x100,
	.gate_shift = 9,

	.flags = RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c8 = {
	.clkdef = {
		.id = SCLK_I2C8_PMU,
		.name = "clk_i2c8_pmu",
		.parent_names = pmu_parents,
		.parent_cnt = nitems(pmu_parents),
	},
	/* PMUCRU_CLKSEL_CON2 */
	.muxdiv_offset = 0x88,

	.div_shift = 8,
	.div_width = 7,

	/* PMUCRU_CLKGATE_CON0 */
	.gate_offset = 0x100,
	.gate_shift = 11,

	.flags = RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c4 = {
	.clkdef = {
		.id = SCLK_I2C4_PMU,
		.name = "clk_i2c4_pmu",
		.parent_names = pmu_parents,
		.parent_cnt = nitems(pmu_parents),
	},
	/* PMUCRU_CLKSEL_CON3 */
	.muxdiv_offset = 0x8c,

	.div_shift = 0,
	.div_width = 7,

	/* PMUCRU_CLKGATE_CON0 */
	.gate_offset = 0x100,
	.gate_shift = 10,

	.flags = RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk rk3399_pmu_clks[] = {
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &ppll
	},

	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &pclk_pmu_src
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c0
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c4
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c8
	},
};

static int
rk3399_pmucru_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "rockchip,rk3399-pmucru")) {
		device_set_desc(dev, "Rockchip RK3399 PMU Clock and Reset Unit");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
rk3399_pmucru_attach(device_t dev)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->gates = rk3399_pmu_gates;
	sc->ngates = nitems(rk3399_pmu_gates);

	sc->clks = rk3399_pmu_clks;
	sc->nclks = nitems(rk3399_pmu_clks);

	return (rk_cru_attach(dev));
}

static device_method_t rk3399_pmucru_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rk3399_pmucru_probe),
	DEVMETHOD(device_attach,	rk3399_pmucru_attach),

	DEVMETHOD_END
};

static devclass_t rk3399_pmucru_devclass;

DEFINE_CLASS_1(rk3399_pmucru, rk3399_pmucru_driver, rk3399_pmucru_methods,
  sizeof(struct rk_cru_softc), rk_cru_driver);

EARLY_DRIVER_MODULE(rk3399_pmucru, simplebus, rk3399_pmucru_driver,
    rk3399_pmucru_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
