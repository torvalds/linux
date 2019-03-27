/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@freebsd.org>
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
 * RockChip Clock and Reset Unit
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

#include <dev/extres/clk/clk.h>
#include <dev/extres/clk/clk_gate.h>
#include <dev/extres/hwreset/hwreset.h>

#include <arm64/rockchip/clk/rk_clk_composite.h>
#include <arm64/rockchip/clk/rk_clk_gate.h>
#include <arm64/rockchip/clk/rk_clk_mux.h>
#include <arm64/rockchip/clk/rk_clk_pll.h>
#include <arm64/rockchip/clk/rk_cru.h>

#include "clkdev_if.h"
#include "hwreset_if.h"

static struct resource_spec rk_cru_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	CCU_READ4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	CCU_WRITE4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

void	rk3328_cru_register_clocks(struct rk_cru_softc *sc);

static int
rk_cru_write_4(device_t dev, bus_addr_t addr, uint32_t val)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);
	CCU_WRITE4(sc, addr, val);
	return (0);
}

static int
rk_cru_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);

	*val = CCU_READ4(sc, addr);
	return (0);
}

static int
rk_cru_modify_4(device_t dev, bus_addr_t addr, uint32_t clr, uint32_t set)
{
	struct rk_cru_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	reg = CCU_READ4(sc, addr);
	reg &= ~clr;
	reg |= set;
	CCU_WRITE4(sc, addr, reg);

	return (0);
}

static int
rk_cru_reset_assert(device_t dev, intptr_t id, bool reset)
{
	struct rk_cru_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	if (id >= sc->nresets || sc->resets[id].offset == 0)
		return (0);

	mtx_lock(&sc->mtx);
	val = CCU_READ4(sc, sc->resets[id].offset);
	if (reset)
		val &= ~(1 << sc->resets[id].shift);
	else
		val |= 1 << sc->resets[id].shift;
	CCU_WRITE4(sc, sc->resets[id].offset, val);
	mtx_unlock(&sc->mtx);

	return (0);
}

static int
rk_cru_reset_is_asserted(device_t dev, intptr_t id, bool *reset)
{
	struct rk_cru_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	if (id >= sc->nresets || sc->resets[id].offset == 0)
		return (0);

	mtx_lock(&sc->mtx);
	val = CCU_READ4(sc, sc->resets[id].offset);
	*reset = (val & (1 << sc->resets[id].shift)) != 0 ? false : true;
	mtx_unlock(&sc->mtx);

	return (0);
}

static void
rk_cru_device_lock(device_t dev)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
}

static void
rk_cru_device_unlock(device_t dev)
{
	struct rk_cru_softc *sc;

	sc = device_get_softc(dev);
	mtx_unlock(&sc->mtx);
}

static int
rk_cru_register_gates(struct rk_cru_softc *sc)
{
	struct rk_clk_gate_def def;
	int i;

	for (i = 0; i < sc->ngates; i++) {
		if (sc->gates[i].name == NULL)
			continue;
		memset(&def, 0, sizeof(def));
		def.clkdef.id = sc->gates[i].id;
		def.clkdef.name = sc->gates[i].name;
		def.clkdef.parent_names = &sc->gates[i].parent_name;
		def.clkdef.parent_cnt = 1;
		def.offset = sc->gates[i].offset;
		def.shift = sc->gates[i].shift;
		def.mask = 1;
		def.on_value = 0;
		def.off_value = 1;
		rk_clk_gate_register(sc->clkdom, &def);
	}

	return (0);
}

int
rk_cru_attach(device_t dev)
{
	struct rk_cru_softc *sc;
	phandle_t node;
	int	i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	node = ofw_bus_get_node(dev);

	if (bus_alloc_resources(dev, rk_cru_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	sc->clkdom = clkdom_create(dev);
	if (sc->clkdom == NULL)
		panic("Cannot create clkdom\n");

	for (i = 0; i < sc->nclks; i++) {
		switch (sc->clks[i].type) {
		case RK_CLK_UNDEFINED:
			break;
		case RK3328_CLK_PLL:
			rk3328_clk_pll_register(sc->clkdom, sc->clks[i].clk.pll);
			break;
		case RK3399_CLK_PLL:
			rk3399_clk_pll_register(sc->clkdom, sc->clks[i].clk.pll);
			break;
		case RK_CLK_COMPOSITE:
			rk_clk_composite_register(sc->clkdom,
			    sc->clks[i].clk.composite);
			break;
		case RK_CLK_MUX:
			rk_clk_mux_register(sc->clkdom, sc->clks[i].clk.mux);
			break;
		case RK_CLK_ARMCLK:
			rk_clk_armclk_register(sc->clkdom, sc->clks[i].clk.armclk);
			break;
		default:
			device_printf(dev, "Unknown clock type\n");
			return (ENXIO);
			break;
		}
	}
	if (sc->gates)
		rk_cru_register_gates(sc);

	if (clkdom_finit(sc->clkdom) != 0)
		panic("cannot finalize clkdom initialization\n");

	if (bootverbose)
		clkdom_dump(sc->clkdom);

	clk_set_assigned(dev, node);

	/* If we have resets, register our self as a reset provider */
	if (sc->resets)
		hwreset_register_ofw_provider(dev);

	return (0);
}

static device_method_t rk_cru_methods[] = {
	/* clkdev interface */
	DEVMETHOD(clkdev_write_4,	rk_cru_write_4),
	DEVMETHOD(clkdev_read_4,	rk_cru_read_4),
	DEVMETHOD(clkdev_modify_4,	rk_cru_modify_4),
	DEVMETHOD(clkdev_device_lock,	rk_cru_device_lock),
	DEVMETHOD(clkdev_device_unlock,	rk_cru_device_unlock),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	rk_cru_reset_assert),
	DEVMETHOD(hwreset_is_asserted,	rk_cru_reset_is_asserted),

	DEVMETHOD_END
};

DEFINE_CLASS_0(rk_cru, rk_cru_driver, rk_cru_methods,
    sizeof(struct rk_cru_softc));
