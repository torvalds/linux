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
 * Allwinner USB PHY
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/gpio.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/gpio/gpiobusvar.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/extres/phy/phy_usb.h>

#include "phynode_if.h"

enum awusbphy_type {
	AWUSBPHY_TYPE_A10 = 1,
	AWUSBPHY_TYPE_A13,
	AWUSBPHY_TYPE_A20,
	AWUSBPHY_TYPE_A31,
	AWUSBPHY_TYPE_H3,
	AWUSBPHY_TYPE_A64,
	AWUSBPHY_TYPE_A83T
};

struct aw_usbphy_conf {
	int			num_phys;
	enum awusbphy_type	phy_type;
	bool			pmu_unk1;
	bool			phy0_route;
};

static const struct aw_usbphy_conf a10_usbphy_conf = {
	.num_phys = 3,
	.phy_type = AWUSBPHY_TYPE_A10,
	.pmu_unk1 = false,
	.phy0_route = false,
};

static const struct aw_usbphy_conf a13_usbphy_conf = {
	.num_phys = 2,
	.phy_type = AWUSBPHY_TYPE_A13,
	.pmu_unk1 = false,
	.phy0_route = false,
};

static const struct aw_usbphy_conf a20_usbphy_conf = {
	.num_phys = 3,
	.phy_type = AWUSBPHY_TYPE_A20,
	.pmu_unk1 = false,
	.phy0_route = false,
};

static const struct aw_usbphy_conf a31_usbphy_conf = {
	.num_phys = 3,
	.phy_type = AWUSBPHY_TYPE_A31,
	.pmu_unk1 = false,
	.phy0_route = false,
};

static const struct aw_usbphy_conf h3_usbphy_conf = {
	.num_phys = 4,
	.phy_type = AWUSBPHY_TYPE_H3,
	.pmu_unk1 = true,
	.phy0_route = false,
};

static const struct aw_usbphy_conf a64_usbphy_conf = {
	.num_phys = 2,
	.phy_type = AWUSBPHY_TYPE_A64,
	.pmu_unk1 = true,
	.phy0_route = true,
};

static const struct aw_usbphy_conf a83t_usbphy_conf = {
	.num_phys = 3,
	.phy_type = AWUSBPHY_TYPE_A83T,
	.pmu_unk1 = false,
	.phy0_route = false,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-usb-phy",	(uintptr_t)&a10_usbphy_conf },
	{ "allwinner,sun5i-a13-usb-phy",	(uintptr_t)&a13_usbphy_conf },
	{ "allwinner,sun6i-a31-usb-phy",	(uintptr_t)&a31_usbphy_conf },
	{ "allwinner,sun7i-a20-usb-phy",	(uintptr_t)&a20_usbphy_conf },
	{ "allwinner,sun8i-h3-usb-phy",		(uintptr_t)&h3_usbphy_conf },
	{ "allwinner,sun50i-a64-usb-phy",	(uintptr_t)&a64_usbphy_conf },
	{ "allwinner,sun8i-a83t-usb-phy",	(uintptr_t)&a83t_usbphy_conf },
	{ NULL,					0 }
};

struct awusbphy_softc {
	struct resource *	phy_ctrl;
	struct resource **	pmu;
	regulator_t *		reg;
	gpio_pin_t		id_det_pin;
	int			id_det_valid;
	gpio_pin_t		vbus_det_pin;
	int			vbus_det_valid;
	struct aw_usbphy_conf	*phy_conf;
	int			mode;
};

 /* Phy class and methods. */
static int awusbphy_phy_enable(struct phynode *phy, bool enable);
static int awusbphy_get_mode(struct phynode *phy, int *mode);
static int awusbphy_set_mode(struct phynode *phy, int mode);
static phynode_usb_method_t awusbphy_phynode_methods[] = {
	PHYNODEMETHOD(phynode_enable, awusbphy_phy_enable),
	PHYNODEMETHOD(phynode_usb_get_mode, awusbphy_get_mode),
	PHYNODEMETHOD(phynode_usb_set_mode, awusbphy_set_mode),

	PHYNODEMETHOD_END
};
DEFINE_CLASS_1(awusbphy_phynode, awusbphy_phynode_class, awusbphy_phynode_methods,
  sizeof(struct phynode_usb_sc), phynode_usb_class);

#define	RD4(res, o)	bus_read_4(res, (o))
#define	WR4(res, o, v)	bus_write_4(res, (o), (v))
#define	CLR4(res, o, m)	WR4(res, o, RD4(res, o) & ~(m))
#define	SET4(res, o, m)	WR4(res, o, RD4(res, o) | (m))

#define	OTG_PHY_CFG	0x20
#define	 OTG_PHY_ROUTE_OTG	(1 << 0)
#define	PMU_IRQ_ENABLE	0x00
#define	 PMU_AHB_INCR8		(1 << 10)
#define	 PMU_AHB_INCR4		(1 << 9)
#define	 PMU_AHB_INCRX_ALIGN	(1 << 8)
#define	 PMU_ULPI_BYPASS	(1 << 0)
#define	PMU_UNK_H3	0x10
#define	 PMU_UNK_H3_CLR		0x2
#define	PHY_CSR		0x00
#define	 ID_PULLUP_EN		(1 << 17)
#define	 DPDM_PULLUP_EN		(1 << 16)
#define	 FORCE_ID		(0x3 << 14)
#define	 FORCE_ID_SHIFT		14
#define	 FORCE_ID_LOW		2
#define	 FORCE_VBUS_VALID	(0x3 << 12)
#define	 FORCE_VBUS_VALID_SHIFT	12
#define	 FORCE_VBUS_VALID_HIGH	3
#define	 VBUS_CHANGE_DET	(1 << 6)
#define	 ID_CHANGE_DET		(1 << 5)
#define	 DPDM_CHANGE_DET	(1 << 4)

static void
awusbphy_configure(device_t dev, int phyno)
{
	struct awusbphy_softc *sc;

	sc = device_get_softc(dev);

	if (sc->pmu[phyno] == NULL)
		return;

	if (sc->phy_conf->pmu_unk1 == true)
		CLR4(sc->pmu[phyno], PMU_UNK_H3, PMU_UNK_H3_CLR);

	SET4(sc->pmu[phyno], PMU_IRQ_ENABLE, PMU_ULPI_BYPASS |
	    PMU_AHB_INCR8 | PMU_AHB_INCR4 | PMU_AHB_INCRX_ALIGN);
}

static int
awusbphy_init(device_t dev)
{
	struct awusbphy_softc *sc;
	phandle_t node;
	char pname[20];
	int error, off, rid;
	regulator_t reg;
	hwreset_t rst;
	clk_t clk;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	sc->phy_conf = (struct aw_usbphy_conf *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	/* Get phy_ctrl region */
	if (ofw_bus_find_string_index(node, "reg-names", "phy_ctrl", &rid) != 0) {
		device_printf(dev, "Cannot locate phy control resource\n");
		return (ENXIO);
	}
	sc->phy_ctrl = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->phy_ctrl == NULL) {
		device_printf(dev, "Cannot allocate resource\n");
		return (ENXIO);
	}

	/* Enable clocks */
	for (off = 0; clk_get_by_ofw_index(dev, 0, off, &clk) == 0; off++) {
		error = clk_enable(clk);
		if (error != 0) {
			device_printf(dev, "couldn't enable clock %s\n",
			    clk_get_name(clk));
			return (error);
		}
	}

	/* De-assert resets */
	for (off = 0; hwreset_get_by_ofw_idx(dev, 0, off, &rst) == 0; off++) {
		error = hwreset_deassert(rst);
		if (error != 0) {
			device_printf(dev, "couldn't de-assert reset %d\n",
			    off);
			return (error);
		}
	}

	/* Get GPIOs */
	error = gpio_pin_get_by_ofw_property(dev, node, "usb0_id_det-gpios",
	    &sc->id_det_pin);
	if (error == 0)
		sc->id_det_valid = 1;
	error = gpio_pin_get_by_ofw_property(dev, node, "usb0_vbus_det-gpios",
	    &sc->vbus_det_pin);
	if (error == 0)
		sc->vbus_det_valid = 1;

	sc->reg = malloc(sizeof(*(sc->reg)) * sc->phy_conf->num_phys, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	sc->pmu = malloc(sizeof(*(sc->pmu)) * sc->phy_conf->num_phys, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	/* Get regulators */
	for (off = 0; off < sc->phy_conf->num_phys; off++) {
		snprintf(pname, sizeof(pname), "usb%d_vbus-supply", off);
		if (regulator_get_by_ofw_property(dev, 0, pname, &reg) == 0)
			sc->reg[off] = reg;

		snprintf(pname, sizeof(pname), "pmu%d", off);
		if (ofw_bus_find_string_index(node, "reg-names",
		    pname, &rid) != 0)
			continue;

		sc->pmu[off] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (sc->pmu[off] == NULL) {
			device_printf(dev, "Cannot allocate resource\n");
			return (ENXIO);
		}
	}

	return (0);
}

static int
awusbphy_vbus_detect(device_t dev, int *val)
{
	struct awusbphy_softc *sc;
	bool active;
	int error;

	sc = device_get_softc(dev);

	if (sc->vbus_det_valid) {
		error = gpio_pin_is_active(sc->vbus_det_pin, &active);
		if (error != 0) {
			device_printf(dev, "Cannot get status of id pin %d\n",
			    error);
			return (error);
		}
		*val = active;
		return (0);
	}

	*val = 0;
	return (0);
}

static int
awusbphy_phy_enable(struct phynode *phynode, bool enable)
{
	device_t dev;
	intptr_t phy;
	struct awusbphy_softc *sc;
	regulator_t reg;
	int error, vbus_det;

	dev = phynode_get_device(phynode);
	phy = phynode_get_id(phynode);
	sc = device_get_softc(dev);

	if (phy < 0 || phy >= sc->phy_conf->num_phys)
		return (ERANGE);

	/* Configure PHY */
	awusbphy_configure(dev, phy);

	/* Regulators are optional. If not found, return success. */
	reg = sc->reg[phy];
	if (reg == NULL)
		return (0);

	if (phy == 0) {
		/* If an external vbus is detected, do not enable phy 0 */
		error = awusbphy_vbus_detect(dev, &vbus_det);
		if (error)
			goto out;

		if (vbus_det == 1) {
			if (bootverbose)
				device_printf(dev, "External VBUS detected, not enabling the regulator\n");

			return (0);
		}
	}
	if (enable) {
		/* Depending on the PHY we need to route OTG to OHCI/EHCI */
		error = regulator_enable(reg);
	} else
		error = regulator_disable(reg);

out:
	if (error != 0) {
		device_printf(dev,
		    "couldn't %s regulator for phy %jd\n",
		    enable ? "enable" : "disable", (intmax_t)phy);
		return (error);
	}

	return (0);
}

static int
awusbphy_get_mode(struct phynode *phynode, int *mode)
{
	struct awusbphy_softc *sc;
	device_t dev;

	dev = phynode_get_device(phynode);
	sc = device_get_softc(dev);

	*mode = sc->mode;

	return (0);
}

static int
awusbphy_set_mode(struct phynode *phynode, int mode)
{
	device_t dev;
	intptr_t phy;
	struct awusbphy_softc *sc;
	uint32_t val;
	int error, vbus_det;

	dev = phynode_get_device(phynode);
	phy = phynode_get_id(phynode);
	sc = device_get_softc(dev);

	if (phy != 0) {
		if (mode != PHY_USB_MODE_HOST)
			return (EINVAL);
		return (0);
	}

	switch (mode) {
	case PHY_USB_MODE_HOST:
		val = bus_read_4(sc->phy_ctrl, PHY_CSR);
		val &= ~(VBUS_CHANGE_DET | ID_CHANGE_DET | DPDM_CHANGE_DET);
		val |= (ID_PULLUP_EN | DPDM_PULLUP_EN);
		val &= ~FORCE_ID;
		val |= (FORCE_ID_LOW << FORCE_ID_SHIFT);
		val &= ~FORCE_VBUS_VALID;
		val |= (FORCE_VBUS_VALID_HIGH << FORCE_VBUS_VALID_SHIFT);
		bus_write_4(sc->phy_ctrl, PHY_CSR, val);
		if (sc->phy_conf->phy0_route == true) {
			error = awusbphy_vbus_detect(dev, &vbus_det);
			if (error)
				goto out;
			if (vbus_det == 0)
				CLR4(sc->phy_ctrl, OTG_PHY_CFG,
				  OTG_PHY_ROUTE_OTG);
			else
				SET4(sc->phy_ctrl, OTG_PHY_CFG,
				  OTG_PHY_ROUTE_OTG);
		}
		break;
	case PHY_USB_MODE_OTG:
		/* TODO */
		break;
	}

	sc->mode = mode;


out:
	return (0);
}

static int
awusbphy_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner USB PHY");
	return (BUS_PROBE_DEFAULT);
}

static int
awusbphy_attach(device_t dev)
{
	int error;
	struct phynode *phynode;
	struct phynode_init_def phy_init;
	struct awusbphy_softc *sc;
	int i;

	sc = device_get_softc(dev);
	error = awusbphy_init(dev);
	if (error) {
		device_printf(dev, "failed to initialize USB PHY, error %d\n",
		    error);
		return (error);
	}

	/* Create and register phys. */
	for (i = 0; i < sc->phy_conf->num_phys; i++) {
		bzero(&phy_init, sizeof(phy_init));
		phy_init.id = i;
		phy_init.ofw_node = ofw_bus_get_node(dev);
		phynode = phynode_create(dev, &awusbphy_phynode_class,
		    &phy_init);
		if (phynode == NULL) {
			device_printf(dev, "failed to create USB PHY\n");
			return (ENXIO);
		}
		if (phynode_register(phynode) == NULL) {
			device_printf(dev, "failed to create USB PHY\n");
			return (ENXIO);
		}
	}

	return (error);
}

static device_method_t awusbphy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		awusbphy_probe),
	DEVMETHOD(device_attach,	awusbphy_attach),

	DEVMETHOD_END
};

static driver_t awusbphy_driver = {
	"awusbphy",
	awusbphy_methods,
	sizeof(struct awusbphy_softc)
};

static devclass_t awusbphy_devclass;
/* aw_usbphy needs to come up after regulators/gpio/etc, but before ehci/ohci */
EARLY_DRIVER_MODULE(awusbphy, simplebus, awusbphy_driver, awusbphy_devclass,
    0, 0, BUS_PASS_SUPPORTDEV + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(awusbphy, 1);
