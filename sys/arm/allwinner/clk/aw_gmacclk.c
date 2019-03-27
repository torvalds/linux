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
 * Allwinner GMAC clock
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

#define	GMAC_CLK_PIT		(0x1 << 2)
#define	GMAC_CLK_PIT_SHIFT	2
#define	GMAC_CLK_PIT_MII	0
#define	GMAC_CLK_PIT_RGMII	1
#define	GMAC_CLK_SRC		(0x3 << 0)
#define	GMAC_CLK_SRC_SHIFT	0
#define	GMAC_CLK_SRC_MII	0
#define	GMAC_CLK_SRC_EXT_RGMII	1
#define	GMAC_CLK_SRC_RGMII	2

#define	EMAC_TXC_DIV_CFG	(1 << 15)
#define	EMAC_TXC_DIV_CFG_SHIFT	15
#define	EMAC_TXC_DIV_CFG_125MHZ	0
#define	EMAC_TXC_DIV_CFG_25MHZ	1
#define	EMAC_PHY_SELECT		(1 << 16)
#define	EMAC_PHY_SELECT_SHIFT	16
#define	EMAC_PHY_SELECT_INT	0
#define	EMAC_PHY_SELECT_EXT	1
#define	EMAC_ETXDC		(0x7 << 10)
#define	EMAC_ETXDC_SHIFT	10
#define	EMAC_ERXDC		(0x1f << 5)
#define	EMAC_ERXDC_SHIFT	5

#define	CLK_IDX_MII		0
#define	CLK_IDX_RGMII		1
#define	CLK_IDX_COUNT		2

enum aw_gmacclk_type {
	GMACCLK_A20 = 1,
	GMACCLK_A83T,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun7i-a20-gmac-clk",	GMACCLK_A20 },
	{ "allwinner,sun8i-a83t-emac-clk",	GMACCLK_A83T },
	{ NULL, 0 }
};

struct aw_gmacclk_sc {
	device_t	clkdev;
	bus_addr_t	reg;
	enum aw_gmacclk_type type;

	int		rx_delay;
	int		tx_delay;
};

#define	GMACCLK_READ(sc, val)	CLKDEV_READ_4((sc)->clkdev, (sc)->reg, (val))
#define	GMACCLK_WRITE(sc, val)	CLKDEV_WRITE_4((sc)->clkdev, (sc)->reg, (val))
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

static int
aw_gmacclk_init(struct clknode *clk, device_t dev)
{
	struct aw_gmacclk_sc *sc;
	uint32_t val, index;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	GMACCLK_READ(sc, &val);
	DEVICE_UNLOCK(sc);

	switch ((val & GMAC_CLK_SRC) >> GMAC_CLK_SRC_SHIFT) {
	case GMAC_CLK_SRC_MII:
		index = CLK_IDX_MII;
		break;
	case GMAC_CLK_SRC_RGMII:
		index = CLK_IDX_RGMII;
		break;
	default:
		return (ENXIO);
	}

	clknode_init_parent_idx(clk, index);
	return (0);
}

static int
aw_gmacclk_set_mux(struct clknode *clk, int index)
{
	struct aw_gmacclk_sc *sc;
	uint32_t val, clk_src, pit, txc_div;
	int error;

	sc = clknode_get_softc(clk);
	error = 0;

	switch (index) {
	case CLK_IDX_MII:
		clk_src = GMAC_CLK_SRC_MII;
		pit = GMAC_CLK_PIT_MII;
		txc_div = EMAC_TXC_DIV_CFG_25MHZ;
		break;
	case CLK_IDX_RGMII:
		clk_src = GMAC_CLK_SRC_RGMII;
		pit = GMAC_CLK_PIT_RGMII;
		txc_div = EMAC_TXC_DIV_CFG_125MHZ;
		break;
	default:
		return (ENXIO);
	}

	DEVICE_LOCK(sc);
	GMACCLK_READ(sc, &val);
	val &= ~(GMAC_CLK_SRC | GMAC_CLK_PIT);
	val |= (clk_src << GMAC_CLK_SRC_SHIFT);
	val |= (pit << GMAC_CLK_PIT_SHIFT);
	if (sc->type == GMACCLK_A83T) {
		val &= ~EMAC_TXC_DIV_CFG;
		val |= (txc_div << EMAC_TXC_DIV_CFG_SHIFT);
		val &= ~EMAC_PHY_SELECT;
		val |= (EMAC_PHY_SELECT_EXT << EMAC_PHY_SELECT_SHIFT);
		if (sc->tx_delay >= 0) {
			val &= ~EMAC_ETXDC;
			val |= (sc->tx_delay << EMAC_ETXDC_SHIFT);
		}
		if (sc->rx_delay >= 0) {
			val &= ~EMAC_ERXDC;
			val |= (sc->rx_delay << EMAC_ERXDC_SHIFT);
		}
	}
	GMACCLK_WRITE(sc, val);
	DEVICE_UNLOCK(sc);

	return (0);
}

static clknode_method_t aw_gmacclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_gmacclk_init),
	CLKNODEMETHOD(clknode_set_mux,		aw_gmacclk_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(aw_gmacclk_clknode, aw_gmacclk_clknode_class,
    aw_gmacclk_clknode_methods, sizeof(struct aw_gmacclk_sc), clknode_class);

static int
aw_gmacclk_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner GMAC Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_gmacclk_attach(device_t dev)
{
	struct clknode_init_def def;
	struct aw_gmacclk_sc *sc;
	struct clkdom *clkdom;
	struct clknode *clk;
	clk_t clk_parent;
	bus_addr_t paddr;
	bus_size_t psize;
	phandle_t node;
	int error, ncells, i;

	node = ofw_bus_get_node(dev);

	if (ofw_reg_to_paddr(node, 0, &paddr, &psize, NULL) != 0) {
		device_printf(dev, "cannot parse 'reg' property\n");
		return (ENXIO);
	}

	error = ofw_bus_parse_xref_list_get_length(node, "clocks",
	    "#clock-cells", &ncells);
	if (error != 0 || ncells != CLK_IDX_COUNT) {
		device_printf(dev, "couldn't find parent clocks\n");
		return (ENXIO);
	}

	clkdom = clkdom_create(dev);

	memset(&def, 0, sizeof(def));
	error = clk_parse_ofw_clk_name(dev, node, &def.name);
	if (error != 0) {
		device_printf(dev, "cannot parse clock name\n");
		error = ENXIO;
		goto fail;
	}
	def.id = 1;
	def.parent_names = malloc(sizeof(char *) * ncells, M_OFWPROP, M_WAITOK);
	for (i = 0; i < ncells; i++) {
		error = clk_get_by_ofw_index(dev, 0, i, &clk_parent);
		if (error != 0) {
			device_printf(dev, "cannot get clock %d\n", error);
			goto fail;
		}
		def.parent_names[i] = clk_get_name(clk_parent);
		clk_release(clk_parent);
	}
	def.parent_cnt = ncells;

	clk = clknode_create(clkdom, &aw_gmacclk_clknode_class, &def);
	if (clk == NULL) {
		device_printf(dev, "cannot create clknode\n");
		error = ENXIO;
		goto fail;
	}

	sc = clknode_get_softc(clk);
	sc->reg = paddr;
	sc->clkdev = device_get_parent(dev);
	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	sc->tx_delay = sc->rx_delay = -1;
	OF_getencprop(node, "tx-delay", &sc->tx_delay, sizeof(sc->tx_delay));
	OF_getencprop(node, "rx-delay", &sc->rx_delay, sizeof(sc->rx_delay));

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

static device_method_t aw_gmacclk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_gmacclk_probe),
	DEVMETHOD(device_attach,	aw_gmacclk_attach),

	DEVMETHOD_END
};

static driver_t aw_gmacclk_driver = {
	"aw_gmacclk",
	aw_gmacclk_methods,
	0
};

static devclass_t aw_gmacclk_devclass;

EARLY_DRIVER_MODULE(aw_gmacclk, simplebus, aw_gmacclk_driver,
    aw_gmacclk_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
