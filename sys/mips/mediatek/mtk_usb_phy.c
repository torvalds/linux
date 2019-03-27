/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_clock.h>
#include <mips/mediatek/fdt_reset.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/mediatek/mtk_sysctl.h>
#include <mips/mediatek/mtk_soc.h>
#include <mips/mediatek/mtk_usb_phy.h>

#define RESET_ASSERT_DELAY	1000
#define RESET_DEASSERT_DELAY	10000

struct mtk_usb_phy_softc {
	device_t		dev;
	struct resource *	res;
	uint32_t		fm_base;
	uint32_t		u2_base;
	uint32_t		sr_coef;
	uint32_t		socid;
};

#define USB_PHY_READ(_sc, _off)		bus_read_4((_sc)->res, (_off))
#define USB_PHY_WRITE(_sc, _off, _val)	bus_write_4((_sc)->res, (_off), (_val))
#define USB_PHY_CLR_SET(_sc, _off, _clr, _set)	\
	USB_PHY_WRITE(_sc, _off, ((USB_PHY_READ(_sc, _off) & ~(_clr)) | (_set)))

#define USB_PHY_READ_U2(_sc, _off)			\
	USB_PHY_READ((_sc), ((_sc)->u2_base + (_off)))
#define USB_PHY_WRITE_U2(_sc, _off, _val)		\
	USB_PHY_WRITE((_sc), ((_sc)->u2_base + (_off)), (_val))
#define USB_PHY_CLR_SET_U2(_sc, _off, _clr, _set)	\
	USB_PHY_WRITE_U2((_sc), (_off), ((USB_PHY_READ_U2((_sc), (_off)) & \
	    ~(_clr)) | (_set)))
#define USB_PHY_BARRIER(_sc)	bus_barrier((_sc)->res, 0, 0, \
			BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ)

#define USB_PHY_READ_FM(_sc, _off)			\
	USB_PHY_READ((_sc), ((_sc)->fm_base + (_off)))
#define USB_PHY_WRITE_FM(_sc, _off)			\
	USB_PHY_WRITE((_sc), ((_sc)->fm_base + (_off)), (_val))
#define USB_PHY_CLR_SET_FM(_sc, _off, _clr, _set)	\
	USB_PHY_WRITE_U2((_sc), (_off), ((USB_PHY_READ_U2((_sc), (_off)) & \
	    ~(_clr)) | (_set)))

static void mtk_usb_phy_mt7621_init(device_t);
static void mtk_usb_phy_mt7628_init(device_t);

static struct ofw_compat_data compat_data[] = {
	{ "ralink,mt7620-usbphy",	MTK_SOC_MT7620A },
	{ "mediatek,mt7620-usbphy",	MTK_SOC_MT7620A },
	{ "ralink,mt7628an-usbphy",	MTK_SOC_MT7628 },
	{ "ralink,rt3352-usbphy",	MTK_SOC_RT3352 },
	{ "ralink,rt3050-usbphy",	MTK_SOC_RT3050 },
	{ NULL,				MTK_SOC_UNKNOWN }
};

static int
mtk_usb_phy_probe(device_t dev)
{
	struct mtk_usb_phy_softc *sc = device_get_softc(dev);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if ((sc->socid =
	    ofw_bus_search_compatible(dev, compat_data)->ocd_data) ==
	    MTK_SOC_UNKNOWN)
		return (ENXIO);

	device_set_desc(dev, "MTK USB PHY");

	return (0);
}

static int
mtk_usb_phy_attach(device_t dev)
{
	struct mtk_usb_phy_softc * sc = device_get_softc(dev);
	phandle_t node;
	uint32_t val;
	int rid;

	sc->dev = dev;

	/* Get our FDT node and SoC id */
	node = ofw_bus_get_node(dev);

	/* Now let's see about setting USB to host or device mode */
	/* XXX: is it the same for all SoCs? */
	val = mtk_sysctl_get(SYSCTL_SYSCFG1);
	if (OF_hasprop(node, "mtk,usb-device"))
		val &= ~SYSCFG1_USB_HOST_MODE;
	else
		val |= SYSCFG1_USB_HOST_MODE;
	mtk_sysctl_set(SYSCTL_SYSCFG1, val);

	/* If we have clocks defined - enable them */
	if (OF_hasprop(node, "clocks"))
		fdt_clock_enable_all(dev);

	/* If we have resets defined - perform a reset sequence */
	if (OF_hasprop(node, "resets")) {
		fdt_reset_assert_all(dev);
		DELAY(RESET_ASSERT_DELAY);
		fdt_reset_deassert_all(dev);
		DELAY(RESET_DEASSERT_DELAY);
	}

	/* Careful, some devices actually require resources */
	if (OF_hasprop(node, "reg")) {
		rid = 0;
		sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (sc->res == NULL) {
			device_printf(dev, "could not map memory\n");
			return (ENXIO);
		}
	} else {
		sc->res = NULL;
	}

	/* Some SoCs require specific USB PHY init... handle these */
	switch (sc->socid) {
	case MTK_SOC_MT7628: /* Fallthrough */
	case MTK_SOC_MT7688:
		if (sc->res == NULL)
			return (ENXIO);
		sc->fm_base = MT7628_FM_FEG_BASE;
		sc->u2_base = MT7628_U2_BASE;
		sc->sr_coef = MT7628_SR_COEF;
		mtk_usb_phy_mt7628_init(dev);
		break;
	case MTK_SOC_MT7621:
		if (sc->res == NULL)
			return (ENXIO);
		sc->fm_base = MT7621_FM_FEG_BASE;
		sc->u2_base = MT7621_U2_BASE;
		sc->sr_coef = MT7621_SR_COEF;
		mtk_usb_phy_mt7621_init(dev);
		break;
	}

	/* We no longer need the resources, release them */
	if (sc->res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->res);

	return (0);
}

static int
mtk_usb_phy_detach(device_t dev)
{
	struct mtk_usb_phy_softc *sc = device_get_softc(dev);
	phandle_t node;

	/* Get our FDT node */
	node = ofw_bus_get_node(dev);

	/* If we have resets defined - assert them */
	if (OF_hasprop(node, "resets"))
		fdt_reset_assert_all(dev);

	/* If we have clocks defined - disable them */
	if (OF_hasprop(node, "clocks"))
		fdt_clock_disable_all(dev);

	/* Finally, release resources, if any were allocated */
	if (sc->res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->res);

	return (0);
}

/*
 * Things currently seem to work a lot better without slew rate calibration
 * both on MT7621 and MT7688, so we leave it out for now.
 */
#ifdef notyet
static void
mtk_usb_phy_slew_rate_calibration(struct mtk_usb_phy_softc *sc)
{
	uint32_t val;
	int i;

	USB_PHY_CLR_SET_U2(sc, U2_PHY_ACR0, 0, SRCAL_EN);
	USB_PHY_BARRIER(sc);
	DELAY(1000);

	USB_PHY_CLR_SET_FM(sc, U2_PHY_FMMONR1, 0, FRCK_EN);
	USB_PHY_BARRIER(sc);
	USB_PHY_CLR_SET_FM(sc, U2_PHY_FMCR0, CYCLECNT, 0x400);
	USB_PHY_BARRIER(sc);
	USB_PHY_CLR_SET_FM(sc, U2_PHY_FMCR0, 0, FDET_EN);
	USB_PHY_BARRIER(sc);

	for (i = 0; i < 1000; i++) {
		if ((val = USB_PHY_READ_FM(sc, U2_PHY_FMMONR0)) != 0) {
			device_printf(sc->dev, "DONE with FDET\n");
			break;
		}
		DELAY(10000);
	}
	device_printf(sc->dev, "After FDET\n");

	USB_PHY_CLR_SET_FM(sc, U2_PHY_FMCR0, FDET_EN, 0);
	USB_PHY_BARRIER(sc);
	USB_PHY_CLR_SET_FM(sc, U2_PHY_FMMONR1, FRCK_EN, 0);
	USB_PHY_BARRIER(sc);
	USB_PHY_CLR_SET_U2(sc, U2_PHY_ACR0, SRCAL_EN, 0);
	USB_PHY_BARRIER(sc);
	DELAY(1000);

	if (val == 0) {
		USB_PHY_CLR_SET_U2(sc, U2_PHY_ACR0, SRCTRL, 0x4 << SRCTRL_OFF);
		USB_PHY_BARRIER(sc);
	} else {
		val = ((((1024 * 25 * sc->sr_coef) / val) + 500) / 1000) &
		    SRCTRL_MSK;
		USB_PHY_CLR_SET_U2(sc, U2_PHY_ACR0, SRCTRL, val << SRCTRL_OFF);
		USB_PHY_BARRIER(sc);
	}
}
#endif

static void
mtk_usb_phy_mt7621_init(device_t dev)
{
#ifdef notyet
	struct mtk_usb_phy_softc *sc = device_get_softc(dev);

	/* Slew rate calibration only, but for 2 ports */
	mtk_usb_phy_slew_rate_calibration(sc);

	sc->u2_base = MT7621_U2_BASE_P1;
	mtk_usb_phy_slew_rate_calibration(sc);
#endif
}

static void
mtk_usb_phy_mt7628_init(device_t dev)
{
	struct mtk_usb_phy_softc *sc = device_get_softc(dev);

	/* XXX: possibly add barriers between the next writes? */
	USB_PHY_WRITE_U2(sc, U2_PHY_DCR0, 0x00ffff02);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc, U2_PHY_DCR0, 0x00555502);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc, U2_PHY_DCR0, 0x00aaaa02);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc, U2_PHY_DCR0, 0x00000402);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc,  U2_PHY_AC0, 0x0048086a);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc,  U2_PHY_AC1, 0x4400001c);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc, U2_PHY_ACR3, 0xc0200000);
	USB_PHY_BARRIER(sc);
	USB_PHY_WRITE_U2(sc, U2_PHY_DTM0, 0x02000000);
	USB_PHY_BARRIER(sc);

#ifdef notyet
	/* Slew rate calibration */
	mtk_usb_phy_slew_rate_calibration(sc);
#endif
}

static device_method_t mtk_usb_phy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtk_usb_phy_probe),
	DEVMETHOD(device_attach,	mtk_usb_phy_attach),
	DEVMETHOD(device_detach,	mtk_usb_phy_detach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t mtk_usb_phy_driver = {
	.name = "usbphy",
	.methods = mtk_usb_phy_methods,
	.size = sizeof(struct mtk_usb_phy_softc),
};

static devclass_t mtk_usb_phy_devclass;

DRIVER_MODULE(usbphy, simplebus, mtk_usb_phy_driver, mtk_usb_phy_devclass, 0,
    0);
