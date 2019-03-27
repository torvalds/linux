/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Vybrid Family Clock Controller Module (CCM)
 * Chapter 10, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>

#define	CCM_CCR		0x00	/* Control Register */
#define	CCM_CSR		0x04	/* Status Register */
#define	CCM_CCSR	0x08	/* Clock Switcher Register */
#define	CCM_CACRR	0x0C	/* ARM Clock Root Register */
#define	CCM_CSCMR1	0x10	/* Serial Clock Multiplexer Register 1 */
#define	CCM_CSCDR1	0x14	/* Serial Clock Divider Register 1 */
#define	CCM_CSCDR2	0x18	/* Serial Clock Divider Register 2 */
#define	CCM_CSCDR3	0x1C	/* Serial Clock Divider Register 3 */
#define	CCM_CSCMR2	0x20	/* Serial Clock Multiplexer Register 2 */
#define	CCM_CTOR	0x28	/* Testing Observability Register */
#define	CCM_CLPCR	0x2C	/* Low Power Control Register */
#define	CCM_CISR	0x30	/* Interrupt Status Register */
#define	CCM_CIMR	0x34	/* Interrupt Mask Register */
#define	CCM_CCOSR	0x38	/* Clock Output Source Register */
#define	CCM_CGPR	0x3C	/* General Purpose Register */

#define	CCM_CCGRN	12
#define	CCM_CCGR(n)	(0x40 + (n * 0x04))	/* Clock Gating Register */
#define	CCM_CMEOR(n)	(0x70 + (n * 0x70))	/* Module Enable Override */
#define	CCM_CCPGR(n)	(0x90 + (n * 0x04))	/* Platform Clock Gating */

#define	CCM_CPPDSR	0x88	/* PLL PFD Disable Status Register */
#define	CCM_CCOWR	0x8C	/* CORE Wakeup Register */

#define	PLL3_PFD4_EN	(1U << 31)
#define	PLL3_PFD3_EN	(1 << 30)
#define	PLL3_PFD2_EN	(1 << 29)
#define	PLL3_PFD1_EN	(1 << 28)
#define	PLL2_PFD4_EN	(1 << 15)
#define	PLL2_PFD3_EN	(1 << 14)
#define	PLL2_PFD2_EN	(1 << 13)
#define	PLL2_PFD1_EN	(1 << 12)
#define	PLL1_PFD4_EN	(1 << 11)
#define	PLL1_PFD3_EN	(1 << 10)
#define	PLL1_PFD2_EN	(1 << 9)
#define	PLL1_PFD1_EN	(1 << 8)

/* CCM_CCR */
#define	FIRC_EN		(1 << 16)
#define	FXOSC_EN	(1 << 12)
#define	FXOSC_RDY	(1 << 5)

/* CCM_CSCDR1 */
#define	ENET_TS_EN	(1 << 23)
#define	RMII_CLK_EN	(1 << 24)
#define	SAI3_EN		(1 << 19)

/* CCM_CSCDR2 */
#define	ESAI_EN		(1 << 30)
#define	ESDHC1_EN	(1 << 29)
#define	ESDHC0_EN	(1 << 28)
#define	NFC_EN		(1 << 9)
#define	ESDHC1_DIV_S	20
#define	ESDHC1_DIV_M	0xf
#define	ESDHC0_DIV_S	16
#define	ESDHC0_DIV_M	0xf

/* CCM_CSCDR3 */
#define	DCU0_EN			(1 << 19)

#define	QSPI1_EN		(1 << 12)
#define	QSPI1_DIV		(1 << 11)
#define	QSPI1_X2_DIV		(1 << 10)
#define	QSPI1_X4_DIV_M		0x3
#define	QSPI1_X4_DIV_S		8

#define	QSPI0_EN		(1 << 4)
#define	QSPI0_DIV		(1 << 3)
#define	QSPI0_X2_DIV		(1 << 2)
#define	QSPI0_X4_DIV_M		0x3
#define	QSPI0_X4_DIV_S		0

#define	SAI3_DIV_SHIFT		12
#define	SAI3_DIV_MASK		0xf
#define	ESAI_DIV_SHIFT		24
#define	ESAI_DIV_MASK		0xf

#define	PLL4_CLK_DIV_SHIFT	6
#define	PLL4_CLK_DIV_MASK	0x7

#define	IPG_CLK_DIV_SHIFT	11
#define	IPG_CLK_DIV_MASK	0x3

#define	ESAI_CLK_SEL_SHIFT	20
#define	ESAI_CLK_SEL_MASK	0x3

#define	SAI3_CLK_SEL_SHIFT	6
#define	SAI3_CLK_SEL_MASK	0x3

#define	CKO1_EN			(1 << 10)
#define	CKO1_DIV_MASK		0xf
#define	CKO1_DIV_SHIFT		6
#define	CKO1_SEL_MASK		0x3f
#define	CKO1_SEL_SHIFT		0
#define	CKO1_PLL4_MAIN		0x6
#define	CKO1_PLL4_DIVD		0x7

struct clk {
	uint32_t	reg;
	uint32_t	enable_reg;
	uint32_t	div_mask;
	uint32_t	div_shift;
	uint32_t	div_val;
	uint32_t	sel_reg;
	uint32_t	sel_mask;
	uint32_t	sel_shift;
	uint32_t	sel_val;
};

static struct clk ipg_clk = {
	.reg = CCM_CACRR,
	.enable_reg = 0,
	.div_mask = IPG_CLK_DIV_MASK,
	.div_shift = IPG_CLK_DIV_SHIFT,
	.div_val = 1, /* Divide by 2 */
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

/*
  PLL4 clock divider (before switching the clocks should be gated)
  000 Divide by 1 (only if PLL frequency less than or equal to 650 MHz)
  001 Divide by 4
  010 Divide by 6
  011 Divide by 8
  100 Divide by 10
  101 Divide by 12
  110 Divide by 14
  111 Divide by 16
*/

static struct clk pll4_clk = {
	.reg = CCM_CACRR,
	.enable_reg = 0,
	.div_mask = PLL4_CLK_DIV_MASK,
	.div_shift = PLL4_CLK_DIV_SHIFT,
	.div_val = 5, /* Divide by 12 */
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk sai3_clk = {
	.reg = CCM_CSCDR1,
	.enable_reg = SAI3_EN,
	.div_mask = SAI3_DIV_MASK,
	.div_shift = SAI3_DIV_SHIFT,
	.div_val = 1,
	.sel_reg = CCM_CSCMR1,
	.sel_mask = SAI3_CLK_SEL_MASK,
	.sel_shift = SAI3_CLK_SEL_SHIFT,
	.sel_val = 0x3, /* Divided PLL4 main clock */
};

static struct clk cko1_clk = {
	.reg = CCM_CCOSR,
	.enable_reg = CKO1_EN,
	.div_mask = CKO1_DIV_MASK,
	.div_shift = CKO1_DIV_SHIFT,
	.div_val = 1,
	.sel_reg = CCM_CCOSR,
	.sel_mask = CKO1_SEL_MASK,
	.sel_shift = CKO1_SEL_SHIFT,
	.sel_val = CKO1_PLL4_DIVD,
};

static struct clk esdhc0_clk = {
	.reg = CCM_CSCDR2,
	.enable_reg = ESDHC0_EN,
	.div_mask = ESDHC0_DIV_M,
	.div_shift = ESDHC0_DIV_S,
	.div_val = 0x9,
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk esdhc1_clk = {
	.reg = CCM_CSCDR2,
	.enable_reg = ESDHC1_EN,
	.div_mask = ESDHC1_DIV_M,
	.div_shift = ESDHC1_DIV_S,
	.div_val = 0x9,
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk qspi0_clk = {
	.reg = CCM_CSCDR3,
	.enable_reg = QSPI0_EN,
	.div_mask = 0,
	.div_shift = 0,
	.div_val = 0,
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk dcu0_clk = {
	.reg = CCM_CSCDR3,
	.enable_reg = DCU0_EN,
	.div_mask = 0x7,
	.div_shift = 16, /* DCU0_DIV */
	.div_val = 0, /* divide by 1 */
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk enet_clk = {
	.reg = CCM_CSCDR1,
	.enable_reg = (ENET_TS_EN | RMII_CLK_EN),
	.div_mask = 0,
	.div_shift = 0,
	.div_val = 0,
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

static struct clk nand_clk = {
	.reg = CCM_CSCDR2,
	.enable_reg = NFC_EN,
	.div_mask = 0,
	.div_shift = 0,
	.div_val = 0,
	.sel_reg = 0,
	.sel_mask = 0,
	.sel_shift = 0,
	.sel_val = 0,
};

/*
  Divider to generate ESAI clock
  0000    Divide by 1
  0001    Divide by 2
  ...     ...
  1111    Divide by 16
*/

static struct clk esai_clk = {
	.reg = CCM_CSCDR2,
	.enable_reg = ESAI_EN,
	.div_mask = ESAI_DIV_MASK,
	.div_shift = ESAI_DIV_SHIFT,
	.div_val = 3, /* Divide by 4 */
	.sel_reg = CCM_CSCMR1,
	.sel_mask = ESAI_CLK_SEL_MASK,
	.sel_shift = ESAI_CLK_SEL_SHIFT,
	.sel_val = 0x3, /* Divided PLL4 main clock */
};

struct clock_entry {
	char		*name;
	struct clk	*clk;
};

static struct clock_entry clock_map[] = {
	{"ipg",		&ipg_clk},
	{"pll4",	&pll4_clk},
	{"sai3",	&sai3_clk},
	{"cko1",	&cko1_clk},
	{"esdhc0",	&esdhc0_clk},
	{"esdhc1",	&esdhc1_clk},
	{"qspi0",	&qspi0_clk},
	{"dcu0",	&dcu0_clk},
	{"enet",	&enet_clk},
	{"nand",	&nand_clk},
	{"esai",	&esai_clk},
	{NULL,	NULL}
};

struct ccm_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
};

static struct resource_spec ccm_spec[] = {
	{ SYS_RES_MEMORY,       0,      RF_ACTIVE },
	{ -1, 0 }
};

static int
ccm_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-ccm"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family CCM Unit");
	return (BUS_PROBE_DEFAULT);
}

static int
set_clock(struct ccm_softc *sc, char *name)
{
	struct clk *clk;
	int reg;
	int i;

	for (i = 0; clock_map[i].name != NULL; i++) {
		if (strcmp(clock_map[i].name, name) == 0) {
#if 0
			device_printf(sc->dev, "Configuring %s clk\n", name);
#endif
			clk = clock_map[i].clk;
			if (clk->sel_reg != 0) {
				reg = READ4(sc, clk->sel_reg);
				reg &= ~(clk->sel_mask << clk->sel_shift);
				reg |= (clk->sel_val << clk->sel_shift);
				WRITE4(sc, clk->sel_reg, reg);
			}

			reg = READ4(sc, clk->reg);
			reg |= clk->enable_reg;
			reg &= ~(clk->div_mask << clk->div_shift);
			reg |= (clk->div_val << clk->div_shift);
			WRITE4(sc, clk->reg, reg);
		}
	}

	return (0);
}

static int
ccm_fdt_set(struct ccm_softc *sc)
{
	phandle_t child, parent, root;
	int len;
	char *fdt_config, *name;

	root = OF_finddevice("/");
	len = 0;
	parent = root;

	/* Find 'clock_names' prop in the tree */
	for (child = OF_child(parent); child != 0; child = OF_peer(child)) {

		/* Find a 'leaf'. Start the search from this node. */
		while (OF_child(child)) {
			parent = child;
			child = OF_child(child);
		}

		if (!ofw_bus_node_status_okay(child))
			continue;

		if ((len = OF_getproplen(child, "clock_names")) > 0) {
			len = OF_getproplen(child, "clock_names");
			OF_getprop_alloc(child, "clock_names",
			    (void **)&fdt_config);

			while (len > 0) {
				name = fdt_config;
				fdt_config += strlen(name) + 1;
				len -= strlen(name) + 1;
				set_clock(sc, name);
			}
		}

		if (OF_peer(child) == 0) {
			/* No more siblings. */
			child = parent;
			parent = OF_parent(child);
		}
	}

	return (0);
}

static int
ccm_attach(device_t dev)
{
	struct ccm_softc *sc;
	int reg;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, ccm_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* Enable oscillator */
	reg = READ4(sc, CCM_CCR);
	reg |= (FIRC_EN | FXOSC_EN);
	WRITE4(sc, CCM_CCR, reg);

	/* Wait 10 times */
	for (i = 0; i < 10; i++) {
		if (READ4(sc, CCM_CSR) & FXOSC_RDY) {
			device_printf(sc->dev, "On board oscillator is ready.\n");
			break;
		}

		cpufunc_nullop();
	}

	/* Clock is on during all modes, except stop mode. */
	for (i = 0; i < CCM_CCGRN; i++) {
		WRITE4(sc, CCM_CCGR(i), 0xffffffff);
	}

	/* Take and apply FDT clocks */
	ccm_fdt_set(sc);

	return (0);
}

static device_method_t ccm_methods[] = {
	DEVMETHOD(device_probe,		ccm_probe),
	DEVMETHOD(device_attach,	ccm_attach),
	{ 0, 0 }
};

static driver_t ccm_driver = {
	"ccm",
	ccm_methods,
	sizeof(struct ccm_softc),
};

static devclass_t ccm_devclass;

DRIVER_MODULE(ccm, simplebus, ccm_driver, ccm_devclass, 0, 0);
