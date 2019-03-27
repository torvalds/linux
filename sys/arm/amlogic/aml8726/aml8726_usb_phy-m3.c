/*-
 * Copyright 2014-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726-m3 USB physical layer driver.
 *
 * Both USB physical interfaces share the same configuration register.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "gpio_if.h"

struct aml8726_usb_phy_gpio {
	device_t	dev;
	uint32_t	pin;
	uint32_t	pol;
};

struct aml8726_usb_phy_softc {
	device_t			dev;
	struct resource			*res[1];
	uint32_t			npwr_en;
	struct aml8726_usb_phy_gpio	*pwr_en;
};

static struct resource_spec aml8726_usb_phy_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_USB_PHY_CFG_REG			0
#define	AML_USB_PHY_CFG_A_CLK_DETECTED		(1U << 31)
#define	AML_USB_PHY_CFG_CLK_DIV_MASK		(0x7f << 24)
#define	AML_USB_PHY_CFG_CLK_DIV_SHIFT		24
#define	AML_USB_PHY_CFG_B_CLK_DETECTED		(1 << 22)
#define	AML_USB_PHY_CFG_A_PLL_RST		(1 << 19)
#define	AML_USB_PHY_CFG_A_PHYS_RST		(1 << 18)
#define	AML_USB_PHY_CFG_A_RST			(1 << 17)
#define	AML_USB_PHY_CFG_B_PLL_RST		(1 << 13)
#define	AML_USB_PHY_CFG_B_PHYS_RST		(1 << 12)
#define	AML_USB_PHY_CFG_B_RST			(1 << 11)
#define	AML_USB_PHY_CFG_CLK_EN			(1 << 8)
#define	AML_USB_PHY_CFG_CLK_SEL_MASK		(7 << 5)
#define	AML_USB_PHY_CFG_CLK_SEL_XTAL		(0 << 5)
#define	AML_USB_PHY_CFG_CLK_SEL_XTAL_DIV2	(1 << 5)
#define	AML_USB_PHY_CFG_B_POR			(1 << 1)
#define	AML_USB_PHY_CFG_A_POR			(1 << 0)

#define	AML_USB_PHY_CFG_CLK_DETECTED \
    (AML_USB_PHY_CFG_A_CLK_DETECTED | AML_USB_PHY_CFG_B_CLK_DETECTED)

#define	AML_USB_PHY_MISC_A_REG			12
#define	AML_USB_PHY_MISC_B_REG			16
#define	AML_USB_PHY_MISC_ID_OVERIDE_EN		(1 << 23)
#define	AML_USB_PHY_MISC_ID_OVERIDE_DEVICE	(1 << 22)
#define	AML_USB_PHY_MISC_ID_OVERIDE_HOST	(0 << 22)

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	PIN_ON_FLAG(pol)		((pol) == 0 ?	\
    GPIO_PIN_LOW : GPIO_PIN_HIGH)
#define	PIN_OFF_FLAG(pol)		((pol) == 0 ?	\
    GPIO_PIN_HIGH : GPIO_PIN_LOW)

static int
aml8726_usb_phy_mode(const char *dwcotg_path, uint32_t *mode)
{
	char *usb_mode;
	phandle_t node;
	ssize_t len;
	
	if ((node = OF_finddevice(dwcotg_path)) == -1)
		return (ENXIO);

	if (fdt_is_compatible_strict(node, "synopsys,designware-hs-otg2") == 0)
		return (ENXIO);

	*mode = 0;

	len = OF_getprop_alloc(node, "dr_mode",
	    (void **)&usb_mode);

	if (len <= 0)
		return (0);

	if (strcasecmp(usb_mode, "host") == 0) {
		*mode = AML_USB_PHY_MISC_ID_OVERIDE_EN |
		    AML_USB_PHY_MISC_ID_OVERIDE_HOST;
	} else if (strcasecmp(usb_mode, "peripheral") == 0) {
		*mode = AML_USB_PHY_MISC_ID_OVERIDE_EN |
		    AML_USB_PHY_MISC_ID_OVERIDE_DEVICE;
	}

	OF_prop_free(usb_mode);

	return (0);
}

static int
aml8726_usb_phy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-m3-usb-phy"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726-m3 USB PHY");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_usb_phy_attach(device_t dev)
{
	struct aml8726_usb_phy_softc *sc = device_get_softc(dev);
	int err;
	int npwr_en;
	pcell_t *prop;
	phandle_t node;
	ssize_t len;
	uint32_t div;
	uint32_t i;
	uint32_t mode_a;
	uint32_t mode_b;
	uint32_t value;

	sc->dev = dev;

	if (aml8726_usb_phy_mode("/soc/usb@c9040000", &mode_a) != 0) {
		device_printf(dev, "missing usb@c9040000 node in FDT\n");
		return (ENXIO);
	}

	if (aml8726_usb_phy_mode("/soc/usb@c90c0000", &mode_b) != 0) {
		device_printf(dev, "missing usb@c90c0000 node in FDT\n");
		return (ENXIO);
	}

	if (bus_alloc_resources(dev, aml8726_usb_phy_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);

	err = 0;

	len = OF_getencprop_alloc_multi(node, "usb-pwr-en",
	    3 * sizeof(pcell_t), (void **)&prop);
	npwr_en = (len > 0) ? len : 0;

	sc->npwr_en = 0;
	sc->pwr_en = (struct aml8726_usb_phy_gpio *)
	    malloc(npwr_en * sizeof (*sc->pwr_en), M_DEVBUF, M_WAITOK);

	for (i = 0; i < npwr_en; i++) {
		sc->pwr_en[i].dev = OF_device_from_xref(prop[i * 3]);
		sc->pwr_en[i].pin = prop[i * 3 + 1];
		sc->pwr_en[i].pol = prop[i * 3 + 2];

		if (sc->pwr_en[i].dev == NULL) {
			err = 1;
			break;
		}
	}

	OF_prop_free(prop);

	if (err) {
		device_printf(dev, "unable to parse gpio\n");
		goto fail;
	}

	/* Turn on power by setting pin and then enabling output driver. */
	for (i = 0; i < npwr_en; i++) {
		if (GPIO_PIN_SET(sc->pwr_en[i].dev, sc->pwr_en[i].pin,
		    PIN_ON_FLAG(sc->pwr_en[i].pol)) != 0 ||
		    GPIO_PIN_SETFLAGS(sc->pwr_en[i].dev, sc->pwr_en[i].pin,
		    GPIO_PIN_OUTPUT) != 0) {
			device_printf(dev,
			    "could not use gpio to control power\n");
			goto fail;
		}

		sc->npwr_en++;
	}

	/*
	 * Configure the clock source and divider.
	 */

	div = 2;

	value = CSR_READ_4(sc, AML_USB_PHY_CFG_REG);

	value &= ~(AML_USB_PHY_CFG_CLK_DIV_MASK | AML_USB_PHY_CFG_CLK_SEL_MASK);

	value &= ~(AML_USB_PHY_CFG_A_RST | AML_USB_PHY_CFG_B_RST);
	value &= ~(AML_USB_PHY_CFG_A_PLL_RST | AML_USB_PHY_CFG_B_PLL_RST);
	value &= ~(AML_USB_PHY_CFG_A_PHYS_RST | AML_USB_PHY_CFG_B_PHYS_RST);
	value &= ~(AML_USB_PHY_CFG_A_POR | AML_USB_PHY_CFG_B_POR);

	value |= AML_USB_PHY_CFG_CLK_SEL_XTAL;
	value |= ((div - 1) << AML_USB_PHY_CFG_CLK_DIV_SHIFT) &
	    AML_USB_PHY_CFG_CLK_DIV_MASK;
	value |= AML_USB_PHY_CFG_CLK_EN;

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	/*
	 * Issue the reset sequence.
	 */

	value |= (AML_USB_PHY_CFG_A_RST | AML_USB_PHY_CFG_B_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value &= ~(AML_USB_PHY_CFG_A_RST | AML_USB_PHY_CFG_B_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value |= (AML_USB_PHY_CFG_A_PLL_RST | AML_USB_PHY_CFG_B_PLL_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value &= ~(AML_USB_PHY_CFG_A_PLL_RST | AML_USB_PHY_CFG_B_PLL_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value |= (AML_USB_PHY_CFG_A_PHYS_RST | AML_USB_PHY_CFG_B_PHYS_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value &= ~(AML_USB_PHY_CFG_A_PHYS_RST | AML_USB_PHY_CFG_B_PHYS_RST);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	value |= (AML_USB_PHY_CFG_A_POR | AML_USB_PHY_CFG_B_POR);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);
	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	/*
	 * Enable by clearing the power on reset.
	 */

	value &= ~(AML_USB_PHY_CFG_A_POR | AML_USB_PHY_CFG_B_POR);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	DELAY(200);

	/*
	 * Check if the clock was detected.
	 */
	value = CSR_READ_4(sc, AML_USB_PHY_CFG_REG);
	if ((value & AML_USB_PHY_CFG_CLK_DETECTED) !=
	    AML_USB_PHY_CFG_CLK_DETECTED)
		device_printf(dev, "PHY Clock not detected\n");

	/*
	 * Configure the mode for each port.
	 */

	value = CSR_READ_4(sc, AML_USB_PHY_MISC_A_REG);

	value &= ~(AML_USB_PHY_MISC_ID_OVERIDE_EN |
	    AML_USB_PHY_MISC_ID_OVERIDE_DEVICE |
	    AML_USB_PHY_MISC_ID_OVERIDE_HOST);
	value |= mode_a;

	CSR_WRITE_4(sc, AML_USB_PHY_MISC_A_REG, value);

	value = CSR_READ_4(sc, AML_USB_PHY_MISC_B_REG);

	value &= ~(AML_USB_PHY_MISC_ID_OVERIDE_EN |
	    AML_USB_PHY_MISC_ID_OVERIDE_DEVICE |
	    AML_USB_PHY_MISC_ID_OVERIDE_HOST);
	value |= mode_b;

	CSR_WRITE_4(sc, AML_USB_PHY_MISC_B_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_MISC_B_REG);

	return (0);

fail:
	/* In the event of problems attempt to turn things back off. */
	i = sc->npwr_en;
	while (i-- != 0) {
		GPIO_PIN_SET(sc->pwr_en[i].dev, sc->pwr_en[i].pin,
		    PIN_OFF_FLAG(sc->pwr_en[i].pol));
	}

	free (sc->pwr_en, M_DEVBUF);
	sc->pwr_en = NULL;

	bus_release_resources(dev, aml8726_usb_phy_spec, sc->res);

	return (ENXIO);
}

static int
aml8726_usb_phy_detach(device_t dev)
{
	struct aml8726_usb_phy_softc *sc = device_get_softc(dev);
	uint32_t i;
	uint32_t value;

	/*
	 * Disable by issuing a power on reset.
	 */

	value = CSR_READ_4(sc, AML_USB_PHY_CFG_REG);

	value |= (AML_USB_PHY_CFG_A_POR | AML_USB_PHY_CFG_B_POR);

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	/* Turn off power */
	i = sc->npwr_en;
	while (i-- != 0) {
		(void)GPIO_PIN_SET(sc->pwr_en[i].dev, sc->pwr_en[i].pin,
		    PIN_OFF_FLAG(sc->pwr_en[i].pol));
	}
	free (sc->pwr_en, M_DEVBUF);
	sc->pwr_en = NULL;

	bus_release_resources(dev, aml8726_usb_phy_spec, sc->res);

	return (0);
}

static device_method_t aml8726_usb_phy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_usb_phy_probe),
	DEVMETHOD(device_attach,	aml8726_usb_phy_attach),
	DEVMETHOD(device_detach,	aml8726_usb_phy_detach),

	DEVMETHOD_END
};

static driver_t aml8726_usb_phy_driver = {
	"usbphy",
	aml8726_usb_phy_methods,
	sizeof(struct aml8726_usb_phy_softc),
};

static devclass_t aml8726_usb_phy_devclass;

DRIVER_MODULE(aml8726_m3usbphy, simplebus, aml8726_usb_phy_driver,
    aml8726_usb_phy_devclass, 0, 0);
MODULE_DEPEND(aml8726_m3usbphy, aml8726_gpio, 1, 1, 1);
