/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Rubicon Communications, LLC (Netgate)
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk_fixed.h>
#include <dev/extres/clk/clk_gate.h>

#include <arm/mv/mv_cp110_clock.h>

#include "clkdev_if.h"

/* Clocks */
static struct clk_fixed_def cp110_clk_pll_0 = {
	.clkdef.id = CP110_PLL_0,
	.freq = 1000000000,
};

static const char *clk_parents_0[] = {"cp110-pll0-0"};
static const char *clk_parents_1[] = {"cp110-pll0-1"};

static struct clk_fixed_def cp110_clk_ppv2_core = {
	.clkdef.id = CP110_PPV2_CORE,
	.clkdef.parent_cnt = 1,
	.mult = 1,
	.div = 3,
};

static struct clk_fixed_def cp110_clk_x2core = {
	.clkdef.id = CP110_X2CORE,
	.clkdef.parent_cnt = 1,
	.mult = 1,
	.div = 2,
};

static const char *core_parents_0[] = {"cp110-x2core-0"};
static const char *core_parents_1[] = {"cp110-x2core-1"};

static struct clk_fixed_def cp110_clk_core = {
	.clkdef.id = CP110_CORE,
	.clkdef.parent_cnt = 1,
	.mult = 1,
	.div = 2,
};

static struct clk_fixed_def cp110_clk_sdio = {
	.clkdef.id = CP110_SDIO,
	.clkdef.parent_cnt = 1,
	.mult = 2,
	.div = 5,
};

/* Gates */

static struct cp110_gate cp110_gates[] = {
	CCU_GATE(CP110_GATE_AUDIO, "cp110-gate-audio", 0)
	CCU_GATE(CP110_GATE_COMM_UNIT, "cp110-gate-comm_unit", 1)
	/* CCU_GATE(CP110_GATE_NAND, "cp110-gate-nand", 2) */
	CCU_GATE(CP110_GATE_PPV2, "cp110-gate-ppv2", 3)
	CCU_GATE(CP110_GATE_SDIO, "cp110-gate-sdio", 4)
	CCU_GATE(CP110_GATE_MG, "cp110-gate-mg", 5)
	CCU_GATE(CP110_GATE_MG_CORE, "cp110-gate-mg_core", 6)
	CCU_GATE(CP110_GATE_XOR1, "cp110-gate-xor1", 7)
	CCU_GATE(CP110_GATE_XOR0, "cp110-gate-xor0", 8)
	CCU_GATE(CP110_GATE_GOP_DP, "cp110-gate-gop_dp", 9)
	CCU_GATE(CP110_GATE_PCIE_X1_0, "cp110-gate-pcie_x10", 11)
	CCU_GATE(CP110_GATE_PCIE_X1_1, "cp110-gate-pcie_x11", 12)
	CCU_GATE(CP110_GATE_PCIE_X4, "cp110-gate-pcie_x4", 13)
	CCU_GATE(CP110_GATE_PCIE_XOR, "cp110-gate-pcie_xor", 14)
	CCU_GATE(CP110_GATE_SATA, "cp110-gate-sata", 15)
	CCU_GATE(CP110_GATE_SATA_USB, "cp110-gate-sata_usb", 16)
	CCU_GATE(CP110_GATE_MAIN, "cp110-gate-main", 17)
	CCU_GATE(CP110_GATE_SDMMC_GOP, "cp110-gate-sdmmc_gop", 18)
	CCU_GATE(CP110_GATE_SLOW_IO, "cp110-gate-slow_io", 21)
	CCU_GATE(CP110_GATE_USB3H0, "cp110-gate-usb3h0", 22)
	CCU_GATE(CP110_GATE_USB3H1, "cp110-gate-usb3h1", 23)
	CCU_GATE(CP110_GATE_USB3DEV, "cp110-gate-usb3dev", 24)
	CCU_GATE(CP110_GATE_EIP150, "cp110-gate-eip150", 25)
	CCU_GATE(CP110_GATE_EIP197, "cp110-gate-eip197", 26)
};

struct mv_cp110_clock_softc {
	struct simplebus_softc	simplebus_sc;
	device_t		dev;
	struct resource		*res;
	struct mtx		mtx;
};

static struct resource_spec mv_cp110_clock_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"marvell,cp110-clock", 1},
	{NULL,             0}
};

#define	RD4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	WR4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static char *
mv_cp110_clock_name(device_t dev, const char *name)
{
	char *clkname = NULL;
	int unit;

	unit = device_get_unit(dev);
	if (asprintf(&clkname, M_DEVBUF, "%s-%d", name, unit) <= 0)
		panic("Cannot generate unique clock name for %s\n", name);
	return (clkname);
}

static int
mv_cp110_clock_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell CP110 Clock Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
cp110_ofw_map(struct clkdom *clkdom, uint32_t ncells,
    phandle_t *cells, struct clknode **clk)
{
	int id = 0;

	if (ncells != 2)
		return (ENXIO);

	id = cells[1];
	if (cells[0] == 1)
		id += CP110_MAX_CLOCK;

	*clk = clknode_find_by_id(clkdom, id);

	return (0);
}

static int
mv_cp110_clock_attach(device_t dev)
{
	struct mv_cp110_clock_softc *sc;
	struct clkdom *clkdom;
	struct clk_gate_def def;
	char *pll0_name;
	int unit, i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, mv_cp110_clock_res_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	unit = device_get_unit(dev);
	if (unit > 1) {
		device_printf(dev, "Bogus cp110-system-controller unit %d\n", unit);
		return (ENXIO);
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	clkdom = clkdom_create(dev);
	clkdom_set_ofw_mapper(clkdom, cp110_ofw_map);

	pll0_name = mv_cp110_clock_name(dev, "cp110-pll0");
	cp110_clk_pll_0.clkdef.name = pll0_name;
	clknode_fixed_register(clkdom, &cp110_clk_pll_0);

	cp110_clk_ppv2_core.clkdef.name = mv_cp110_clock_name(dev, "cp110-ppv2");
	cp110_clk_ppv2_core.clkdef.parent_names = (unit == 0) ? clk_parents_0 : clk_parents_1;
	clknode_fixed_register(clkdom, &cp110_clk_ppv2_core);

	cp110_clk_x2core.clkdef.name = mv_cp110_clock_name(dev, "cp110-x2core");
	cp110_clk_x2core.clkdef.parent_names = (unit == 0) ? clk_parents_0 : clk_parents_1;
	clknode_fixed_register(clkdom, &cp110_clk_x2core);

	cp110_clk_core.clkdef.name = mv_cp110_clock_name(dev, "cp110-core");
	cp110_clk_core.clkdef.parent_names = (unit == 0) ? core_parents_0 : core_parents_1;
	clknode_fixed_register(clkdom, &cp110_clk_core);

	/* NAND missing */

	cp110_clk_sdio.clkdef.name = mv_cp110_clock_name(dev, "cp110-sdio");
	cp110_clk_sdio.clkdef.parent_names = (unit == 0) ? clk_parents_0 : clk_parents_1;
	clknode_fixed_register(clkdom, &cp110_clk_sdio);

	for (i = 0; i < nitems(cp110_gates); i++) {
		if (cp110_gates[i].name == NULL)
			continue;

		memset(&def, 0, sizeof(def));
		def.clkdef.id = CP110_MAX_CLOCK + i;
		def.clkdef.name = mv_cp110_clock_name(dev, cp110_gates[i].name);
		def.clkdef.parent_cnt = 1;
		def.offset = CP110_CLOCK_GATING_OFFSET;
		def.shift = cp110_gates[i].shift;
		def.mask = 1;
		def.on_value = 1;
		def.off_value = 0;

		switch (i) {
		case CP110_GATE_MG:
		case CP110_GATE_GOP_DP:
		case CP110_GATE_PPV2:
			def.clkdef.parent_names = &cp110_clk_ppv2_core.clkdef.name;
			break;
		case CP110_GATE_SDIO:
			def.clkdef.parent_names = &cp110_clk_sdio.clkdef.name;
			break;
		case CP110_GATE_MAIN:
		case CP110_GATE_PCIE_XOR:
		case CP110_GATE_PCIE_X4:
		case CP110_GATE_EIP150:
		case CP110_GATE_EIP197:
			def.clkdef.parent_names = &cp110_clk_x2core.clkdef.name;
			break;
		default:
			def.clkdef.parent_names = &cp110_clk_core.clkdef.name;
			break;
		}

		clknode_gate_register(clkdom, &def);
	}

	clkdom_finit(clkdom);

	if (bootverbose)
		clkdom_dump(clkdom);

	return (0);
}

static int
mv_cp110_clock_detach(device_t dev)
{

	return (EBUSY);
}

static int
mv_cp110_clock_write_4(device_t dev, bus_addr_t addr, uint32_t val)
{
	struct mv_cp110_clock_softc *sc;

	sc = device_get_softc(dev);
	WR4(sc, addr, val);
	return (0);
}

static int
mv_cp110_clock_read_4(device_t dev, bus_addr_t addr, uint32_t *val)
{
	struct mv_cp110_clock_softc *sc;

	sc = device_get_softc(dev);

	*val = RD4(sc, addr);
	return (0);
}

static int
mv_cp110_clock_modify_4(device_t dev, bus_addr_t addr, uint32_t clr, uint32_t set)
{
	struct mv_cp110_clock_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	reg = RD4(sc, addr);
	reg &= ~clr;
	reg |= set;
	WR4(sc, addr, reg);

	return (0);
}

static void
mv_cp110_clock_device_lock(device_t dev)
{
	struct mv_cp110_clock_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
}

static void
mv_cp110_clock_device_unlock(device_t dev)
{
	struct mv_cp110_clock_softc *sc;

	sc = device_get_softc(dev);
	mtx_unlock(&sc->mtx);
}

static device_method_t mv_cp110_clock_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_cp110_clock_probe),
	DEVMETHOD(device_attach,	mv_cp110_clock_attach),
	DEVMETHOD(device_detach,	mv_cp110_clock_detach),

	/* clkdev interface */
	DEVMETHOD(clkdev_write_4,	mv_cp110_clock_write_4),
	DEVMETHOD(clkdev_read_4,	mv_cp110_clock_read_4),
	DEVMETHOD(clkdev_modify_4,	mv_cp110_clock_modify_4),
	DEVMETHOD(clkdev_device_lock,	mv_cp110_clock_device_lock),
	DEVMETHOD(clkdev_device_unlock,	mv_cp110_clock_device_unlock),

	DEVMETHOD_END
};

static devclass_t mv_cp110_clock_devclass;

static driver_t mv_cp110_clock_driver = {
	"mv_cp110_clock",
	mv_cp110_clock_methods,
	sizeof(struct mv_cp110_clock_softc),
};

EARLY_DRIVER_MODULE(mv_cp110_clock, simplebus, mv_cp110_clock_driver,
    mv_cp110_clock_devclass, 0, 0, BUS_PASS_RESOURCE + BUS_PASS_ORDER_LATE);
