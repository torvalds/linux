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

/*
 * Allwinner Clock Control Unit
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

#include <arm/allwinner/clkng/aw_ccung.h>
#include <arm/allwinner/clkng/aw_clk.h>

#ifdef __aarch64__
#include "opt_soc.h"
#endif

#include "clkdev_if.h"
#include "hwreset_if.h"

static struct resource_spec aw_ccung_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	CCU_READ4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	CCU_WRITE4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static int
aw_ccung_write_4(device_t dev, bus_addr_t addr, uint32_t val)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);
	CCU_WRITE4(sc, addr, val);
	return (0);
}

static int
aw_ccung_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);

	*val = CCU_READ4(sc, addr);
	return (0);
}

static int
aw_ccung_modify_4(device_t dev, bus_addr_t addr, uint32_t clr, uint32_t set)
{
	struct aw_ccung_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	reg = CCU_READ4(sc, addr);
	reg &= ~clr;
	reg |= set;
	CCU_WRITE4(sc, addr, reg);

	return (0);
}

static int
aw_ccung_reset_assert(device_t dev, intptr_t id, bool reset)
{
	struct aw_ccung_softc *sc;
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
aw_ccung_reset_is_asserted(device_t dev, intptr_t id, bool *reset)
{
	struct aw_ccung_softc *sc;
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
aw_ccung_device_lock(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
}

static void
aw_ccung_device_unlock(device_t dev)
{
	struct aw_ccung_softc *sc;

	sc = device_get_softc(dev);
	mtx_unlock(&sc->mtx);
}

static int
aw_ccung_register_gates(struct aw_ccung_softc *sc)
{
	struct clk_gate_def def;
	int i;

	for (i = 0; i < sc->ngates; i++) {
		if (sc->gates[i].name == NULL)
			continue;
		memset(&def, 0, sizeof(def));
		def.clkdef.id = i;
		def.clkdef.name = sc->gates[i].name;
		def.clkdef.parent_names = &sc->gates[i].parent_name;
		def.clkdef.parent_cnt = 1;
		def.offset = sc->gates[i].offset;
		def.shift = sc->gates[i].shift;
		def.mask = 1;
		def.on_value = 1;
		def.off_value = 0;
		clknode_gate_register(sc->clkdom, &def);
	}

	return (0);
}

static void
aw_ccung_init_clocks(struct aw_ccung_softc *sc)
{
	struct clknode *clknode;
	int i, error;

	for (i = 0; i < sc->n_clk_init; i++) {
		clknode = clknode_find_by_name(sc->clk_init[i].name);
		if (clknode == NULL) {
			device_printf(sc->dev, "Cannot find clock %s\n",
			    sc->clk_init[i].name);
			continue;
		}

		if (sc->clk_init[i].parent_name != NULL) {
			if (bootverbose)
				device_printf(sc->dev, "Setting %s as parent for %s\n",
				    sc->clk_init[i].parent_name,
				    sc->clk_init[i].name);
			error = clknode_set_parent_by_name(clknode,
			    sc->clk_init[i].parent_name);
			if (error != 0) {
				device_printf(sc->dev,
				    "Cannot set parent to %s for %s\n",
				    sc->clk_init[i].parent_name,
				    sc->clk_init[i].name);
				continue;
			}
		}
		if (sc->clk_init[i].default_freq != 0) {
			error = clknode_set_freq(clknode,
			    sc->clk_init[i].default_freq, 0 , 0);
			if (error != 0) {
				device_printf(sc->dev,
				    "Cannot set frequency for %s to %ju\n",
				    sc->clk_init[i].name,
				    sc->clk_init[i].default_freq);
				continue;
			}
		}
		if (sc->clk_init[i].enable) {
			error = clknode_enable(clknode);
			if (error != 0) {
				device_printf(sc->dev,
				    "Cannot enable %s\n",
				    sc->clk_init[i].name);
				continue;
			}
		}
	}
}

int
aw_ccung_attach(device_t dev)
{
	struct aw_ccung_softc *sc;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, aw_ccung_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	sc->clkdom = clkdom_create(dev);
	if (sc->clkdom == NULL)
		panic("Cannot create clkdom\n");

	for (i = 0; i < sc->nclks; i++) {
		switch (sc->clks[i].type) {
		case AW_CLK_UNDEFINED:
			break;
		case AW_CLK_MUX:
			clknode_mux_register(sc->clkdom, sc->clks[i].clk.mux);
			break;
		case AW_CLK_DIV:
			clknode_div_register(sc->clkdom, sc->clks[i].clk.div);
			break;
		case AW_CLK_FIXED:
			clknode_fixed_register(sc->clkdom,
			    sc->clks[i].clk.fixed);
			break;
		case AW_CLK_NKMP:
			aw_clk_nkmp_register(sc->clkdom, sc->clks[i].clk.nkmp);
			break;
		case AW_CLK_NM:
			aw_clk_nm_register(sc->clkdom, sc->clks[i].clk.nm);
			break;
		case AW_CLK_PREDIV_MUX:
			aw_clk_prediv_mux_register(sc->clkdom,
			    sc->clks[i].clk.prediv_mux);
			break;
		}
	}

	if (sc->gates)
		aw_ccung_register_gates(sc);
	if (clkdom_finit(sc->clkdom) != 0)
		panic("cannot finalize clkdom initialization\n");

	clkdom_xlock(sc->clkdom);
	aw_ccung_init_clocks(sc);
	clkdom_unlock(sc->clkdom);

	if (bootverbose)
		clkdom_dump(sc->clkdom);

	/* If we have resets, register our self as a reset provider */
	if (sc->resets)
		hwreset_register_ofw_provider(dev);

	return (0);
}

static device_method_t aw_ccung_methods[] = {
	/* clkdev interface */
	DEVMETHOD(clkdev_write_4,	aw_ccung_write_4),
	DEVMETHOD(clkdev_read_4,	aw_ccung_read_4),
	DEVMETHOD(clkdev_modify_4,	aw_ccung_modify_4),
	DEVMETHOD(clkdev_device_lock,	aw_ccung_device_lock),
	DEVMETHOD(clkdev_device_unlock,	aw_ccung_device_unlock),

	/* Reset interface */
	DEVMETHOD(hwreset_assert,	aw_ccung_reset_assert),
	DEVMETHOD(hwreset_is_asserted,	aw_ccung_reset_is_asserted),

	DEVMETHOD_END
};

DEFINE_CLASS_0(aw_ccung, aw_ccung_driver, aw_ccung_methods,
    sizeof(struct aw_ccung_softc));
