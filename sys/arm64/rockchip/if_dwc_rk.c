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
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/dwc/if_dwc.h>
#include <dev/dwc/if_dwcvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>

#include <dev/extres/syscon/syscon.h>

#include "syscon_if.h"

#include "if_dwc_if.h"

#define	RK3328_GRF_MAC_CON0		0x0900
#define	 RK3328_GRF_MAC_CON0_TX_MASK	0x7F
#define	 RK3328_GRF_MAC_CON0_TX_SHIFT	0
#define	 RK3328_GRF_MAC_CON0_RX_MASK	0x7F
#define	 RK3328_GRF_MAC_CON0_RX_SHIFT	7

#define	RK3328_GRF_MAC_CON1		0x0904
#define	RK3328_GRF_MAC_CON2		0x0908
#define	RK3328_GRF_MACPHY_CON0		0x0B00
#define	RK3328_GRF_MACPHY_CON1		0x0B04
#define	RK3328_GRF_MACPHY_CON2		0x0B08
#define	RK3328_GRF_MACPHY_CON3		0x0B0C
#define	RK3328_GRF_MACPHY_STATUS	0x0B10

static void
rk3328_set_delays(struct syscon *grf, phandle_t node)
{
	uint32_t tx, rx;

	if (OF_getencprop(node, "tx_delay", &tx, sizeof(tx)) <= 0)
		tx = 0x30;
	if (OF_getencprop(node, "rx_delay", &rx, sizeof(rx)) <= 0)
		rx = 0x10;

	tx = ((tx & RK3328_GRF_MAC_CON0_TX_MASK) <<
	    RK3328_GRF_MAC_CON0_TX_SHIFT);
	rx = ((rx & RK3328_GRF_MAC_CON0_TX_MASK) <<
	    RK3328_GRF_MAC_CON0_RX_SHIFT);

	SYSCON_WRITE_4(grf, RK3328_GRF_MAC_CON0, tx | rx | 0xFFFF0000);
}

#define	RK3399_GRF_SOC_CON6		0xc218
#define	 RK3399_GRF_SOC_CON6_TX_MASK	0x7F
#define	 RK3399_GRF_SOC_CON6_TX_SHIFT	0
#define	 RK3399_GRF_SOC_CON6_RX_MASK	0x7F
#define	 RK3399_GRF_SOC_CON6_RX_SHIFT	8

static void
rk3399_set_delays(struct syscon *grf, phandle_t node)
{
	uint32_t tx, rx;

	if (OF_getencprop(node, "tx_delay", &tx, sizeof(tx)) <= 0)
		tx = 0x30;
	if (OF_getencprop(node, "rx_delay", &rx, sizeof(rx)) <= 0)
		rx = 0x10;

	tx = ((tx & RK3399_GRF_SOC_CON6_TX_MASK) <<
	    RK3399_GRF_SOC_CON6_TX_SHIFT);
	rx = ((rx & RK3399_GRF_SOC_CON6_TX_MASK) <<
	    RK3399_GRF_SOC_CON6_RX_SHIFT);

	SYSCON_WRITE_4(grf, RK3399_GRF_SOC_CON6, tx | rx | 0xFFFF0000);
}

static int
if_dwc_rk_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!(ofw_bus_is_compatible(dev, "rockchip,rk3328-gmac") ||
	      ofw_bus_is_compatible(dev, "rockchip,rk3399-gmac")))
		return (ENXIO);
	device_set_desc(dev, "Rockchip Gigabit Ethernet Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
if_dwc_rk_init(device_t dev)
{
	phandle_t node;
	struct syscon *grf = NULL;

	node = ofw_bus_get_node(dev);
	if (OF_hasprop(node, "rockchip,grf") &&
	    syscon_get_by_ofw_property(dev, node,
	    "rockchip,grf", &grf) != 0) {
		device_printf(dev, "cannot get grf driver handle\n");
		return (ENXIO);
	}

#ifdef notyet
	if (ofw_bus_is_compatible(dev, "rockchip,rk3399-gmac"))
	    rk3399_set_delays(grf, node);
	else if (ofw_bus_is_compatible(dev, "rockchip,rk3328-gmac"))
	    rk3328_set_delays(grf, node);
#endif

	/* Mode should be set according to dtb property */

	return (0);
}

static int
if_dwc_rk_mac_type(device_t dev)
{

	return (DWC_GMAC_ALT_DESC);
}

static int
if_dwc_rk_mii_clk(device_t dev)
{

	/* Should be calculated from the clock */
	return (GMAC_MII_CLK_150_250M_DIV102);
}

static device_method_t if_dwc_rk_methods[] = {
	DEVMETHOD(device_probe,		if_dwc_rk_probe),

	DEVMETHOD(if_dwc_init,		if_dwc_rk_init),
	DEVMETHOD(if_dwc_mac_type,	if_dwc_rk_mac_type),
	DEVMETHOD(if_dwc_mii_clk,	if_dwc_rk_mii_clk),

	DEVMETHOD_END
};

static devclass_t dwc_rk_devclass;

extern driver_t dwc_driver;

DEFINE_CLASS_1(dwc, dwc_rk_driver, if_dwc_rk_methods,
    sizeof(struct dwc_softc), dwc_driver);
DRIVER_MODULE(dwc_rk, simplebus, dwc_rk_driver, dwc_rk_devclass, 0, 0);
MODULE_DEPEND(dwc_rk, dwc, 1, 1, 1);
