/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * DWC3 USB 3.0 DRD (dual role device) PHY
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
#include <sys/gpio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/samsung/exynos/exynos5_common.h>
#include <arm/samsung/exynos/exynos5_pmu.h>

#include "gpio_if.h"

#define	USB_DRD_LINKSYSTEM			0x04
#define	 LINKSYSTEM_FLADJ_MASK			(0x3f << 1)
#define	 LINKSYSTEM_FLADJ(x)			((x) << 1)
#define	 LINKSYSTEM_XHCI_VERSION_CTRL		(1 << 27)
#define	USB_DRD_PHYUTMI				0x08
#define	 PHYUTMI_OTGDISABLE			(1 << 6)
#define	 PHYUTMI_FORCESUSPEND			(1 << 1)
#define	 PHYUTMI_FORCESLEEP			(1 << 0)
#define	USB_DRD_PHYPIPE				0x0c
#define	USB_DRD_PHYCLKRST			0x10
#define	 PHYCLKRST_PORTRESET			(1 << 1)
#define	 PHYCLKRST_COMMONONN			(1 << 0)
#define	 PHYCLKRST_EN_UTMISUSPEND		(1 << 31)
#define	 PHYCLKRST_SSC_REFCLKSEL_MASK		(0xff << 23)
#define	 PHYCLKRST_SSC_REFCLKSEL(x)		((x) << 23)
#define	 PHYCLKRST_SSC_RANGE_MASK		(0x03 << 21)
#define	 PHYCLKRST_SSC_RANGE(x)			((x) << 21)
#define	 PHYCLKRST_SSC_EN			(1 << 20)
#define	 PHYCLKRST_REF_SSP_EN			(1 << 19)
#define	 PHYCLKRST_REF_CLKDIV2			(1 << 18)
#define	 PHYCLKRST_MPLL_MLTPR_MASK		(0x7f << 11)
#define	 PHYCLKRST_MPLL_MLTPR_100MHZ		(0x19 << 11)
#define	 PHYCLKRST_MPLL_MLTPR_50M		(0x32 << 11)
#define	 PHYCLKRST_MPLL_MLTPR_24MHZ		(0x68 << 11)
#define	 PHYCLKRST_MPLL_MLTPR_20MHZ		(0x7d << 11)
#define	 PHYCLKRST_MPLL_MLTPR_19200KHZ		(0x02 << 11)
#define	 PHYCLKRST_FSEL_UTMI_MASK		(0x7 << 5)
#define	 PHYCLKRST_FSEL_PIPE_MASK		(0x7 << 8)
#define	 PHYCLKRST_FSEL(x)			((x) << 5)
#define	 PHYCLKRST_FSEL_9MHZ6			0x0
#define	 PHYCLKRST_FSEL_10MHZ			0x1
#define	 PHYCLKRST_FSEL_12MHZ			0x2
#define	 PHYCLKRST_FSEL_19MHZ2			0x3
#define	 PHYCLKRST_FSEL_20MHZ			0x4
#define	 PHYCLKRST_FSEL_24MHZ			0x5
#define	 PHYCLKRST_FSEL_50MHZ			0x7
#define	 PHYCLKRST_RETENABLEN			(1 << 4)
#define	 PHYCLKRST_REFCLKSEL_MASK		(0x03 << 2)
#define	 PHYCLKRST_REFCLKSEL_PAD_REFCLK		(0x2 << 2)
#define	 PHYCLKRST_REFCLKSEL_EXT_REFCLK		(0x3 << 2)
#define	USB_DRD_PHYREG0				0x14
#define	USB_DRD_PHYREG1				0x18
#define	USB_DRD_PHYPARAM0			0x1c
#define	 PHYPARAM0_REF_USE_PAD			(1 << 31)
#define	 PHYPARAM0_REF_LOSLEVEL_MASK		(0x1f << 26)
#define	 PHYPARAM0_REF_LOSLEVEL			(0x9 << 26)
#define	USB_DRD_PHYPARAM1			0x20
#define	 PHYPARAM1_PCS_TXDEEMPH_MASK		(0x1f << 0)
#define	 PHYPARAM1_PCS_TXDEEMPH			(0x1c)
#define	USB_DRD_PHYTERM				0x24
#define	USB_DRD_PHYTEST				0x28
#define	 PHYTEST_POWERDOWN_SSP			(1 << 3)
#define	 PHYTEST_POWERDOWN_HSP			(1 << 2)
#define	USB_DRD_PHYADP				0x2c
#define	USB_DRD_PHYUTMICLKSEL			0x30
#define	 PHYUTMICLKSEL_UTMI_CLKSEL		(1 << 2)
#define	USB_DRD_PHYRESUME			0x34
#define	USB_DRD_LINKPORT			0x44

struct usb_phy_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
};

static struct resource_spec usb_phy_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
usb_phy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "samsung,exynos5420-usbdrd-phy"))
		return (ENXIO);

	device_set_desc(dev, "Samsung Exynos 5 USB PHY");
	return (BUS_PROBE_DEFAULT);
}

static int
vbus_on(struct usb_phy_softc *sc)
{
	pcell_t dts_value[3];
	device_t gpio_dev;
	phandle_t node;
	pcell_t pin;
	int len;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (-1);

	/* Power pin */
	if ((len = OF_getproplen(node, "vbus-supply")) <= 0)
		return (-1);
	OF_getencprop(node, "vbus-supply", dts_value, len);
	pin = dts_value[0];

	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		device_printf(sc->dev, "can't find gpio_dev\n");
		return (1);
	}

	GPIO_PIN_SETFLAGS(gpio_dev, pin, GPIO_PIN_OUTPUT);
	GPIO_PIN_SET(gpio_dev, pin, GPIO_PIN_HIGH);

	return (0);
}

static int
usb3_phy_init(struct usb_phy_softc *sc)
{
	int reg;

	/* Reset USB 3.0 PHY */
	WRITE4(sc, USB_DRD_PHYREG0, 0);

	reg = READ4(sc, USB_DRD_PHYPARAM0);
	/* PHY CLK src */
	reg &= ~(PHYPARAM0_REF_USE_PAD);
	reg &= ~(PHYPARAM0_REF_LOSLEVEL_MASK);
	reg |= (PHYPARAM0_REF_LOSLEVEL);
	WRITE4(sc, USB_DRD_PHYPARAM0, reg);
	WRITE4(sc, USB_DRD_PHYRESUME, 0);

	reg = (LINKSYSTEM_XHCI_VERSION_CTRL |
	    LINKSYSTEM_FLADJ(0x20));
	WRITE4(sc, USB_DRD_LINKSYSTEM, reg);

	reg = READ4(sc, USB_DRD_PHYPARAM1);
	reg &= ~(PHYPARAM1_PCS_TXDEEMPH_MASK);
	reg |= (PHYPARAM1_PCS_TXDEEMPH);
	WRITE4(sc, USB_DRD_PHYPARAM1, reg);

	reg = READ4(sc, USB_DRD_PHYUTMICLKSEL);
	reg |= (PHYUTMICLKSEL_UTMI_CLKSEL);
	WRITE4(sc, USB_DRD_PHYUTMICLKSEL, reg);

	reg = READ4(sc, USB_DRD_PHYTEST);
	reg &= ~(PHYTEST_POWERDOWN_HSP);
	reg &= ~(PHYTEST_POWERDOWN_SSP);
	WRITE4(sc, USB_DRD_PHYTEST, reg);

	WRITE4(sc, USB_DRD_PHYUTMI, PHYUTMI_OTGDISABLE);

	/* Clock */
	reg = (PHYCLKRST_REFCLKSEL_EXT_REFCLK);
	reg |= (PHYCLKRST_FSEL(PHYCLKRST_FSEL_24MHZ));
	reg |= (PHYCLKRST_MPLL_MLTPR_24MHZ);
	reg |= (PHYCLKRST_SSC_REFCLKSEL(0x88));
	reg |= (PHYCLKRST_RETENABLEN |
	    PHYCLKRST_REF_SSP_EN | /* Super speed */
	    PHYCLKRST_SSC_EN | /* Spread spectrum */
	    PHYCLKRST_COMMONONN |
	    PHYCLKRST_PORTRESET);

	WRITE4(sc, USB_DRD_PHYCLKRST, reg);
	DELAY(50000);
	reg &= ~PHYCLKRST_PORTRESET;
	WRITE4(sc, USB_DRD_PHYCLKRST, reg);

	return (0);
}

static int
usb_phy_attach(device_t dev)
{
	struct usb_phy_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, usb_phy_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	vbus_on(sc);

	usbdrd_phy_power_on();

	DELAY(100);

	usb3_phy_init(sc);

	return (0);
}

static device_method_t usb_phy_methods[] = {
	DEVMETHOD(device_probe,		usb_phy_probe),
	DEVMETHOD(device_attach,	usb_phy_attach),
	{ 0, 0 }
};

static driver_t usb_phy_driver = {
	"usb_phy",
	usb_phy_methods,
	sizeof(struct usb_phy_softc),
};

static devclass_t usb_phy_devclass;

DRIVER_MODULE(usb_phy, simplebus, usb_phy_driver, usb_phy_devclass, 0, 0);
