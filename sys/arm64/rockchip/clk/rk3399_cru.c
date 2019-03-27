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

#define	PCLK_GPIO2		336
#define	PCLK_GPIO3		337
#define	PCLK_GPIO4		338
#define	PCLK_I2C1		341
#define	PCLK_I2C2		342
#define	PCLK_I2C3		343
#define	PCLK_I2C5		344
#define	PCLK_I2C6		345
#define	PCLK_I2C7		346
#define	HCLK_SDMMC		462

static struct rk_cru_gate rk3399_gates[] = {
	/* CRU_CLKGATE_CON0 */
	CRU_GATE(0, "clk_core_l_lpll_src", "lpll", 0x300, 0)
	CRU_GATE(0, "clk_core_l_bpll_src", "bpll", 0x300, 1)
	CRU_GATE(0, "clk_core_l_dpll_src", "dpll", 0x300, 2)
	CRU_GATE(0, "clk_core_l_gpll_src", "gpll", 0x300, 3)

	/* CRU_CLKGATE_CON1 */
	CRU_GATE(0, "clk_core_b_lpll_src", "lpll", 0x304, 0)
	CRU_GATE(0, "clk_core_b_bpll_src", "bpll", 0x304, 1)
	CRU_GATE(0, "clk_core_b_dpll_src", "dpll", 0x304, 2)
	CRU_GATE(0, "clk_core_b_gpll_src", "gpll", 0x304, 3)

	/* CRU_CLKGATE_CON5 */
	CRU_GATE(0, "cpll_aclk_perihp_src", "cpll", 0x314, 0)
	CRU_GATE(0, "gpll_aclk_perihp_src", "gpll", 0x314, 1)

	/* CRU_CLKGATE_CON7 */
	CRU_GATE(0, "gpll_aclk_perilp0_src", "gpll", 0x31C, 0)
	CRU_GATE(0, "cpll_aclk_perilp0_src", "cpll", 0x31C, 1)

	/* CRU_CLKGATE_CON8 */
	CRU_GATE(0, "hclk_perilp1_cpll_src", "cpll", 0x320, 1)
	CRU_GATE(0, "hclk_perilp1_gpll_src", "gpll", 0x320, 0)

	/* CRU_CLKGATE_CON22 */
	CRU_GATE(PCLK_I2C7, "pclk_rki2c7", "pclk_perilp1", 0x358, 5)
	CRU_GATE(PCLK_I2C1, "pclk_rki2c1", "pclk_perilp1", 0x358, 6)
	CRU_GATE(PCLK_I2C5, "pclk_rki2c5", "pclk_perilp1", 0x358, 7)
	CRU_GATE(PCLK_I2C6, "pclk_rki2c6", "pclk_perilp1", 0x358, 8)
	CRU_GATE(PCLK_I2C2, "pclk_rki2c2", "pclk_perilp1", 0x358, 9)
	CRU_GATE(PCLK_I2C3, "pclk_rki2c3", "pclk_perilp1", 0x358, 10)

	/* CRU_CLKGATE_CON31 */
	CRU_GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_alive", 0x37c, 3)
	CRU_GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_alive", 0x37c, 4)
	CRU_GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_alive", 0x37c, 5)

	/* CRU_CLKGATE_CON33 */
	CRU_GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_sd", 0x384, 8)
};


/*
 * PLLs
 */

#define PLL_APLLL			1
#define PLL_APLLB			2
#define PLL_DPLL			3
#define PLL_CPLL			4
#define PLL_GPLL			5
#define PLL_NPLL			6
#define PLL_VPLL			7

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

static struct rk_clk_pll_def lpll = {
	.clkdef = {
		.id = PLL_APLLL,
		.name = "lpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x00,
	.gate_offset = 0x300,
	.gate_shift = 0,
	.flags = RK_CLK_PLL_HAVE_GATE,
	.rates = rk3399_pll_rates,
};

static struct rk_clk_pll_def bpll = {
	.clkdef = {
		.id = PLL_APLLB,
		.name = "bpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x20,
	.gate_offset = 0x300,
	.gate_shift = 1,
	.flags = RK_CLK_PLL_HAVE_GATE,
	.rates = rk3399_pll_rates,
};

static struct rk_clk_pll_def dpll = {
	.clkdef = {
		.id = PLL_DPLL,
		.name = "dpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x40,
	.gate_offset = 0x300,
	.gate_shift = 2,
	.flags = RK_CLK_PLL_HAVE_GATE,
	.rates = rk3399_pll_rates,
};


static struct rk_clk_pll_def cpll = {
	.clkdef = {
		.id = PLL_CPLL,
		.name = "cpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x60,
	.rates = rk3399_pll_rates,
};

static struct rk_clk_pll_def gpll = {
	.clkdef = {
		.id = PLL_GPLL,
		.name = "gpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0x80,
	.gate_offset = 0x300,
	.gate_shift = 3,
	.flags = RK_CLK_PLL_HAVE_GATE,
	.rates = rk3399_pll_rates,
};

static struct rk_clk_pll_def npll = {
	.clkdef = {
		.id = PLL_NPLL,
		.name = "npll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0xa0,
	.rates = rk3399_pll_rates,
};

static struct rk_clk_pll_def vpll = {
	.clkdef = {
		.id = PLL_VPLL,
		.name = "vpll",
		.parent_names = pll_parents,
		.parent_cnt = nitems(pll_parents),
	},
	.base_offset = 0xc0,
	.rates = rk3399_pll_rates,
};

#define	ACLK_PERIHP	192
#define	HCLK_PERIHP	448
#define	PCLK_PERIHP	320

static const char *aclk_perihp_parents[] = {"cpll_aclk_perihp_src", "gpll_aclk_perihp_src"};

static struct rk_clk_composite_def aclk_perihp = {
	.clkdef = {
		.id = ACLK_PERIHP,
		.name = "aclk_perihp",
		.parent_names = aclk_perihp_parents,
		.parent_cnt = nitems(aclk_perihp_parents),
	},
	/* CRU_CLKSEL_CON14 */
	.muxdiv_offset = 0x138,

	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 5,

	/* CRU_CLKGATE_CON5 */
	.gate_offset = 0x314,
	.gate_shift = 2,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static const char *hclk_pclk_perihp_parents[] = {"aclk_perihp"};

static struct rk_clk_composite_def hclk_perihp = {
	.clkdef = {
		.id = HCLK_PERIHP,
		.name = "hclk_perihp",
		.parent_names = hclk_pclk_perihp_parents,
		.parent_cnt = nitems(hclk_pclk_perihp_parents),
	},
	/* CRU_CLKSEL_CON14 */
	.muxdiv_offset = 0x138,

	.div_shift = 8,
	.div_width = 2,

	/* CRU_CLKGATE_CON5 */
	.gate_offset = 0x314,
	.gate_shift = 3,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def pclk_perihp = {
	.clkdef = {
		.id = PCLK_PERIHP,
		.name = "pclk_perihp",
		.parent_names = hclk_pclk_perihp_parents,
		.parent_cnt = nitems(hclk_pclk_perihp_parents),
	},
	/* CRU_CLKSEL_CON14 */
	.muxdiv_offset = 0x138,

	.div_shift = 12,
	.div_width = 3,

	/* CRU_CLKGATE_CON5 */
	.gate_offset = 0x314,
	.gate_shift = 4,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

#define	ACLK_PERILP0	194
#define	HCLK_PERILP0	449
#define	PCLK_PERILP0	322

static const char *aclk_perilp0_parents[] = {"cpll_aclk_perilp0_src", "gpll_aclk_perilp0_src"};

static struct rk_clk_composite_def aclk_perilp0 = {
	.clkdef = {
		.id = ACLK_PERILP0,
		.name = "aclk_perilp0",
		.parent_names = aclk_perilp0_parents,
		.parent_cnt = nitems(aclk_perilp0_parents),
	},
	/* CRU_CLKSEL_CON14 */
	.muxdiv_offset = 0x15C,

	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 5,

	/* CRU_CLKGATE_CON7 */
	.gate_offset = 0x31C,
	.gate_shift = 2,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static const char *hclk_pclk_perilp0_parents[] = {"aclk_perilp0"};

static struct rk_clk_composite_def hclk_perilp0 = {
	.clkdef = {
		.id = HCLK_PERILP0,
		.name = "hclk_perilp0",
		.parent_names = hclk_pclk_perilp0_parents,
		.parent_cnt = nitems(hclk_pclk_perilp0_parents),
	},
	/* CRU_CLKSEL_CON23 */
	.muxdiv_offset = 0x15C,

	.div_shift = 8,
	.div_width = 2,

	/* CRU_CLKGATE_CON7 */
	.gate_offset = 0x31C,
	.gate_shift = 3,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def pclk_perilp0 = {
	.clkdef = {
		.id = PCLK_PERILP0,
		.name = "pclk_perilp0",
		.parent_names = hclk_pclk_perilp0_parents,
		.parent_cnt = nitems(hclk_pclk_perilp0_parents),
	},
	/* CRU_CLKSEL_CON23 */
	.muxdiv_offset = 0x15C,

	.div_shift = 12,
	.div_width = 3,

	/* CRU_CLKGATE_CON7 */
	.gate_offset = 0x31C,
	.gate_shift = 4,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

/*
 * misc
 */
#define	PCLK_ALIVE		390

static const char *alive_parents[] = {"gpll"};

static struct rk_clk_composite_def pclk_alive = {
	.clkdef = {
		.id = PCLK_ALIVE,
		.name = "pclk_alive",
		.parent_names = alive_parents,
		.parent_cnt = nitems(alive_parents),
	},
	/* CRU_CLKSEL_CON57 */
	.muxdiv_offset = 0x01e4,

	.div_shift = 0,
	.div_width = 5,
};

#define	HCLK_PERILP1		450
#define	PCLK_PERILP1		323

static const char *hclk_perilp1_parents[] = {"cpll", "gpll"};

static struct rk_clk_composite_def hclk_perilp1 = {
	.clkdef = {
		.id = HCLK_PERILP1,
		.name = "hclk_perilp1",
		.parent_names = hclk_perilp1_parents,
		.parent_cnt = nitems(hclk_perilp1_parents),
	},
	/* CRU_CLKSEL_CON25 */
	.muxdiv_offset = 0x164,
	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 5,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX,
};

static const char *pclk_perilp1_parents[] = {"hclk_perilp1"};

static struct rk_clk_composite_def pclk_perilp1 = {
	.clkdef = {
		.id = PCLK_PERILP1,
		.name = "pclk_perilp1",
		.parent_names = pclk_perilp1_parents,
		.parent_cnt = nitems(pclk_perilp1_parents),
	},
	/* CRU_CLKSEL_CON25 */
	.muxdiv_offset = 0x164,

	.div_shift = 8,
	.div_width = 3,

	/* CRU_CLKGATE_CON8 */
	.gate_offset = 0x320,
	.gate_shift = 2,

	.flags = RK_CLK_COMPOSITE_HAVE_GATE,
};

/*
 * i2c
 */
static const char *i2c_parents[] = {"cpll", "gpll"};

#define	SCLK_I2C1	65
#define	SCLK_I2C2	66
#define	SCLK_I2C3	67
#define	SCLK_I2C5	68
#define	SCLK_I2C6	69
#define	SCLK_I2C7	70

static struct rk_clk_composite_def i2c1 = {
	.clkdef = {
		.id = SCLK_I2C1,
		.name = "clk_i2c1",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON61 */
	.muxdiv_offset = 0x01f4,
	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 0,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c2 = {
	.clkdef = {
		.id = SCLK_I2C2,
		.name = "clk_i2c2",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON62 */
	.muxdiv_offset = 0x01f8,
	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 2,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c3 = {
	.clkdef = {
		.id = SCLK_I2C3,
		.name = "clk_i2c3",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON63 */
	.muxdiv_offset = 0x01fc,
	.mux_shift = 7,
	.mux_width = 1,

	.div_shift = 0,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 4,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c5 = {
	.clkdef = {
		.id = SCLK_I2C5,
		.name = "clk_i2c5",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON61 */
	.muxdiv_offset = 0x01f4,
	.mux_shift = 15,
	.mux_width = 1,

	.div_shift = 8,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 1,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c6 = {
	.clkdef = {
		.id = SCLK_I2C6,
		.name = "clk_i2c6",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON62 */
	.muxdiv_offset = 0x01f8,
	.mux_shift = 15,
	.mux_width = 1,

	.div_shift = 8,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 3,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk_composite_def i2c7 = {
	.clkdef = {
		.id = SCLK_I2C7,
		.name = "clk_i2c7",
		.parent_names = i2c_parents,
		.parent_cnt = nitems(i2c_parents),
	},
	/* CRU_CLKSEL_CON63 */
	.muxdiv_offset = 0x01fc,
	.mux_shift = 15,
	.mux_width = 1,

	.div_shift = 8,
	.div_width = 7,

	/* CRU_CLKGATE_CON10 */
	.gate_offset = 0x0328,
	.gate_shift = 5,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

/*
 * ARM CPU clocks (LITTLE and big)
 */
#define ARMCLKL				8
#define ARMCLKB				9

static const char *armclk_parents[] = {"lpll", "bpll", "dpll", "gpll"};

static struct rk_clk_armclk_rates rk3399_armclkl_rates[] = {
	{
		.freq = 1800000000,
		.div = 1,
	},
	{
		.freq = 1704000000,
		.div = 1,
	},
	{
		.freq = 1608000000,
		.div = 1,
	},
	{
		.freq = 1512000000,
		.div = 1,
	},
	{
		.freq = 1488000000,
		.div = 1,
	},
	{
		.freq = 1416000000,
		.div = 1,
	},
	{
		.freq = 1200000000,
		.div = 1,
	},
	{
		.freq = 1008000000,
		.div = 1,
	},
	{
		.freq = 816000000,
		.div = 1,
	},
	{
		.freq = 696000000,
		.div = 1,
	},
	{
		.freq = 600000000,
		.div = 1,
	},
	{
		.freq = 408000000,
		.div = 1,
	},
	{
		.freq = 312000000,
		.div = 1,
	},
	{
		.freq = 216000000,
		.div = 1,
	},
	{
		.freq = 96000000,
		.div = 1,
	},
};

static struct rk_clk_armclk_def armclk_l = {
	.clkdef = {
		.id = ARMCLKL,
		.name = "armclkl",
		.parent_names = armclk_parents,
		.parent_cnt = nitems(armclk_parents),
	},
	/* CRU_CLKSEL_CON0 */
	.muxdiv_offset = 0x100,
	.mux_shift = 6,
	.mux_width = 2,

	.div_shift = 0,
	.div_width = 5,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX,
	.main_parent = 0,
	.alt_parent = 3,

	.rates = rk3399_armclkl_rates,
	.nrates = nitems(rk3399_armclkl_rates),
};

static struct rk_clk_armclk_rates rk3399_armclkb_rates[] = {
	{
		.freq = 2208000000,
		.div = 1,
	},
	{
		.freq = 2184000000,
		.div = 1,
	},
	{
		.freq = 2088000000,
		.div = 1,
	},
	{
		.freq = 2040000000,
		.div = 1,
	},
	{
		.freq = 2016000000,
		.div = 1,
	},
	{
		.freq = 1992000000,
		.div = 1,
	},
	{
		.freq = 1896000000,
		.div = 1,
	},
	{
		.freq = 1800000000,
		.div = 1,
	},
	{
		.freq = 1704000000,
		.div = 1,
	},
	{
		.freq = 1608000000,
		.div = 1,
	},
	{
		.freq = 1512000000,
		.div = 1,
	},
	{
		.freq = 1488000000,
		.div = 1,
	},
	{
		.freq = 1416000000,
		.div = 1,
	},
	{
		.freq = 1200000000,
		.div = 1,
	},
	{
		.freq = 1008000000,
		.div = 1,
	},
	{
		.freq = 816000000,
		.div = 1,
	},
	{
		.freq = 696000000,
		.div = 1,
	},
	{
		.freq = 600000000,
		.div = 1,
	},
	{
		.freq = 408000000,
		.div = 1,
	},
	{
		.freq = 312000000,
		.div = 1,
	},
	{
		.freq = 216000000,
		.div = 1,
	},
	{
		.freq = 96000000,
		.div = 1,
	},
};

static struct rk_clk_armclk_def armclk_b = {
	.clkdef = {
		.id = ARMCLKB,
		.name = "armclkb",
		.parent_names = armclk_parents,
		.parent_cnt = nitems(armclk_parents),
	},
	.muxdiv_offset = 0x108,
	.mux_shift = 6,
	.mux_width = 2,

	.div_shift = 0,
	.div_width = 5,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX,
	.main_parent = 1,
	.alt_parent = 3,

	.rates = rk3399_armclkb_rates,
	.nrates = nitems(rk3399_armclkb_rates),
};

/*
 * sdmmc
 */

#define	HCLK_SD		461

static const char *hclk_sd_parents[] = {"cpll", "gpll"};

static struct rk_clk_composite_def hclk_sd = {
	.clkdef = {
		.id = HCLK_SD,
		.name = "hclk_sd",
		.parent_names = hclk_sd_parents,
		.parent_cnt = nitems(hclk_sd_parents),
	},

	.muxdiv_offset = 0x134,
	.mux_shift = 15,
	.mux_width = 1,

	.div_shift = 8,
	.div_width = 5,

	.gate_offset = 0x330,
	.gate_shift = 13,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

#define	SCLK_SDMMC		76

static const char *sclk_sdmmc_parents[] = {"cpll", "gpll", "npll", "ppll"};

static struct rk_clk_composite_def sclk_sdmmc = {
	.clkdef = {
		.id = SCLK_SDMMC,
		.name = "sclk_sdmmc",
		.parent_names = sclk_sdmmc_parents,
		.parent_cnt = nitems(sclk_sdmmc_parents),
	},

	.muxdiv_offset = 0x140,
	.mux_shift = 8,
	.mux_width = 3,

	.div_shift = 0,
	.div_width = 7,

	.gate_offset = 0x318,
	.gate_shift = 1,

	.flags = RK_CLK_COMPOSITE_HAVE_MUX | RK_CLK_COMPOSITE_HAVE_GATE,
};

static struct rk_clk rk3399_clks[] = {
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &lpll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &bpll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &dpll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &cpll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &gpll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &npll
	},
	{
		.type = RK3399_CLK_PLL,
		.clk.pll = &vpll
	},

	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &aclk_perihp,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &hclk_perihp,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &pclk_perihp,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &aclk_perilp0,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &hclk_perilp0,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &pclk_perilp0,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &pclk_alive,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &hclk_perilp1,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &pclk_perilp1,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c1,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c2,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c3,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c5,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c6,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &i2c7,
	},

	{
		.type = RK_CLK_ARMCLK,
		.clk.armclk = &armclk_l,
	},
	{
		.type = RK_CLK_ARMCLK,
		.clk.armclk = &armclk_b,
	},

	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &hclk_sd,
	},
	{
		.type = RK_CLK_COMPOSITE,
		.clk.composite = &sclk_sdmmc,
	},
};

static int
rk3399_cru_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "rockchip,rk3399-cru")) {
		device_set_desc(dev, "Rockchip RK3399 Clock and Reset Unit");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
rk3399_cru_attach(device_t dev)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->gates = rk3399_gates;
	sc->ngates = nitems(rk3399_gates);

	sc->clks = rk3399_clks;
	sc->nclks = nitems(rk3399_clks);

	return (rk_cru_attach(dev));
}

static device_method_t rk3399_cru_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rk3399_cru_probe),
	DEVMETHOD(device_attach,	rk3399_cru_attach),

	DEVMETHOD_END
};

static devclass_t rk3399_cru_devclass;

DEFINE_CLASS_1(rk3399_cru, rk3399_cru_driver, rk3399_cru_methods,
  sizeof(struct rk_cru_softc), rk_cru_driver);

EARLY_DRIVER_MODULE(rk3399_cru, simplebus, rk3399_cru_driver,
    rk3399_cru_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
