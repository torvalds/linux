/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,2018 Emmanuel Vadot <manu@freebsd.org>
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

#if defined(__aarch64__)
#include "opt_soc.h"
#endif

#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_mux.h>

#include <arm/allwinner/clkng/aw_ccung.h>

#include <gnu/dts/include/dt-bindings/clock/sun8i-r-ccu.h>
#include <gnu/dts/include/dt-bindings/reset/sun8i-r-ccu.h>

/* Non-exported clocks */
#define	CLK_AHB0	1
#define	CLK_APB0	2

static struct aw_ccung_reset ccu_sun8i_r_resets[] = {
	CCU_RESET(RST_APB0_IR, 0xb0, 1)
	CCU_RESET(RST_APB0_TIMER, 0xb0, 2)
	CCU_RESET(RST_APB0_RSB, 0xb0, 4)
	CCU_RESET(RST_APB0_UART, 0xb0, 6)
};

static struct aw_ccung_gate ccu_sun8i_r_gates[] = {
	CCU_GATE(CLK_APB0_PIO, "apb0-pio", "apb0", 0x28, 0)
	CCU_GATE(CLK_APB0_IR, "apb0-ir", "apb0", 0x28, 1)
	CCU_GATE(CLK_APB0_TIMER, "apb0-timer", "apb0", 0x28, 2)
	CCU_GATE(CLK_APB0_RSB, "apb0-rsb", "apb0", 0x28, 3)
	CCU_GATE(CLK_APB0_UART, "apb0-uart", "apb0", 0x28, 4)
	CCU_GATE(CLK_APB0_I2C, "apb0-i2c", "apb0", 0x28, 6)
	CCU_GATE(CLK_APB0_TWD, "apb0-twd", "apb0", 0x28, 7)
};

static const char *ar100_parents[] = {"osc32k", "osc24M", "pll_periph0", "iosc"};
static const char *a83t_ar100_parents[] = {"osc16M-d512", "osc24M", "pll_periph", "osc16M"};
PREDIV_CLK(ar100_clk, CLK_AR100,				/* id */
    "ar100", ar100_parents,					/* name, parents */
    0x00,							/* offset */
    16, 2,							/* mux */
    4, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,			/* div */
    8, 5, 0, AW_CLK_FACTOR_HAS_COND,				/* prediv */
    16, 2, 2);							/* prediv condition */
PREDIV_CLK(a83t_ar100_clk, CLK_AR100,				/* id */
    "ar100", a83t_ar100_parents,				/* name, parents */
    0x00,							/* offset */
    16, 2,							/* mux */
    4, 2, 0, AW_CLK_FACTOR_POWER_OF_TWO,			/* div */
    8, 5, 0, AW_CLK_FACTOR_HAS_COND,				/* prediv */
    16, 2, 2);							/* prediv condition */

static const char *ahb0_parents[] = {"ar100"};
FIXED_CLK(ahb0_clk,
    CLK_AHB0,			/* id */
    "ahb0",			/* name */
    ahb0_parents,		/* parent */
    0,				/* freq */
    1,				/* mult */
    1,				/* div */
    0);				/* flags */

static const char *apb0_parents[] = {"ahb0"};
DIV_CLK(apb0_clk,
    CLK_APB0,			/* id */
    "apb0", apb0_parents,	/* name, parents */
    0x0c,			/* offset */
    0, 2,			/* shift, width */
    0, NULL);			/* flags, div table */

static const char *r_ccu_ir_parents[] = {"osc32k", "osc24M"};
NM_CLK(r_ccu_ir_clk,
    CLK_IR,				/* id */
    "ir", r_ccu_ir_parents,		/* names, parents */
    0x54,				/* offset */
    0, 4, 0, 0,				/* N factor */
    16, 2, 0, 0,			/* M flags */
    24, 2,				/* mux */
    31,					/* gate */
    AW_CLK_HAS_MUX | AW_CLK_REPARENT);	/* flags */

static const char *a83t_ir_parents[] = {"osc16M", "osc24M"};
static struct aw_clk_nm_def a83t_ir_clk = {
	.clkdef = {
		.id = CLK_IR,
		.name = "ir",
		.parent_names = a83t_ir_parents,
		.parent_cnt = nitems(a83t_ir_parents),
	},
	.offset = 0x54,
	.n = {.shift = 0, .width = 4, .flags = AW_CLK_FACTOR_POWER_OF_TWO, },
	.m = {.shift = 16, .width = 2},
	.prediv = {
		.cond_shift = 24,
		.cond_width = 2,
		.cond_value = 0,
		.value = 16
	},
	.mux_shift = 24,
	.mux_width = 2,
	.flags = AW_CLK_HAS_MUX | AW_CLK_HAS_PREDIV,
};

static struct aw_ccung_clk clks[] = {
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &ar100_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb0_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &ahb0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &r_ccu_ir_clk},
};

static struct aw_ccung_clk a83t_clks[] = {
	{ .type = AW_CLK_PREDIV_MUX, .clk.prediv_mux = &a83t_ar100_clk},
	{ .type = AW_CLK_DIV, .clk.div = &apb0_clk},
	{ .type = AW_CLK_FIXED, .clk.fixed = &ahb0_clk},
	{ .type = AW_CLK_NM, .clk.nm = &a83t_ir_clk},
};

static struct ofw_compat_data compat_data[] = {
#if defined(SOC_ALLWINNER_H3) || defined(SOC_ALLWINNER_H5)
	{ "allwinner,sun8i-h3-r-ccu", 1 },
#endif
#if defined(SOC_ALLWINNER_A64)
	{ "allwinner,sun50i-a64-r-ccu", 1 },
#endif
	{ NULL, 0},
};

static int
ccu_sun8i_r_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner SUN8I_R Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_sun8i_r_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = ccu_sun8i_r_resets;
	sc->nresets = nitems(ccu_sun8i_r_resets);
	sc->gates = ccu_sun8i_r_gates;
	sc->ngates = nitems(ccu_sun8i_r_gates);
	sc->clks = clks;
	sc->nclks = nitems(clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_sun8i_r_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_sun8i_r_probe),
	DEVMETHOD(device_attach,	ccu_sun8i_r_attach),

	DEVMETHOD_END
};

static devclass_t ccu_sun8i_r_devclass;

DEFINE_CLASS_1(ccu_sun8i_r, ccu_sun8i_r_driver, ccu_sun8i_r_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_sun8i_r, simplebus, ccu_sun8i_r_driver,
    ccu_sun8i_r_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_LAST);

static int
ccu_a83t_r_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun8i-a83t-r-ccu"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner A83T_R Clock Control Unit NG");
	return (BUS_PROBE_DEFAULT);
}

static int
ccu_a83t_r_attach(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	sc->resets = ccu_sun8i_r_resets;
	sc->nresets = nitems(ccu_sun8i_r_resets);
	sc->gates = ccu_sun8i_r_gates;
	sc->ngates = nitems(ccu_sun8i_r_gates);
	sc->clks = a83t_clks;
	sc->nclks = nitems(a83t_clks);

	return (aw_ccung_attach(dev));
}

static device_method_t ccu_a83t_r_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ccu_a83t_r_probe),
	DEVMETHOD(device_attach,	ccu_a83t_r_attach),

	DEVMETHOD_END
};

static devclass_t ccu_a83t_r_devclass;

DEFINE_CLASS_1(ccu_a83t_r, ccu_a83t_r_driver, ccu_a83t_r_methods,
  sizeof(struct aw_ccung_softc), aw_ccung_driver);

EARLY_DRIVER_MODULE(ccu_a83t_r, simplebus, ccu_a83t_r_driver,
    ccu_a83t_r_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_LAST);
