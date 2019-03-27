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
 * Amlogic aml8726-m6 (and later) USB physical layer driver.
 *
 * Each USB physical interface has a dedicated register block.
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>

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
	boolean_t			force_aca;
	struct aml8726_usb_phy_gpio	hub_rst;
};

static struct resource_spec aml8726_usb_phy_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_USB_PHY_CFG_REG			0
#define	AML_USB_PHY_CFG_CLK_SEL_32K_ALT		(1 << 15)
#define	AML_USB_PHY_CFG_CLK_DIV_MASK		(0x7f << 4)
#define	AML_USB_PHY_CFG_CLK_DIV_SHIFT		4
#define	AML_USB_PHY_CFG_CLK_SEL_MASK		(7 << 1)
#define	AML_USB_PHY_CFG_CLK_SEL_XTAL		(0 << 1)
#define	AML_USB_PHY_CFG_CLK_SEL_XTAL_DIV2	(1 << 1)
#define	AML_USB_PHY_CFG_CLK_EN			(1 << 0)

#define	AML_USB_PHY_CTRL_REG			4
#define	AML_USB_PHY_CTRL_FSEL_MASK		(7 << 22)
#define	AML_USB_PHY_CTRL_FSEL_12M		(2 << 22)
#define	AML_USB_PHY_CTRL_FSEL_24M		(5 << 22)
#define	AML_USB_PHY_CTRL_POR			(1 << 15)
#define	AML_USB_PHY_CTRL_CLK_DETECTED		(1 << 8)

#define	AML_USB_PHY_ADP_BC_REG			12
#define	AML_USB_PHY_ADP_BC_ACA_FLOATING		(1 << 26)
#define	AML_USB_PHY_ADP_BC_ACA_EN		(1 << 16)

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	PIN_ON_FLAG(pol)		((pol) == 0 ?	\
    GPIO_PIN_LOW : GPIO_PIN_HIGH)
#define	PIN_OFF_FLAG(pol)		((pol) == 0 ?	\
    GPIO_PIN_HIGH : GPIO_PIN_LOW)

static int
aml8726_usb_phy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-m6-usb-phy") &&
	    !ofw_bus_is_compatible(dev, "amlogic,aml8726-m8-usb-phy"))
		return (ENXIO);

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
	case AML_SOC_HW_REV_M8B:
		device_set_desc(dev, "Amlogic aml8726-m8 USB PHY");
		break;
	default:
		device_set_desc(dev, "Amlogic aml8726-m6 USB PHY");
		break;
	}

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_usb_phy_attach(device_t dev)
{
	struct aml8726_usb_phy_softc *sc = device_get_softc(dev);
	char *force_aca;
	int err;
	int npwr_en;
	pcell_t *prop;
	phandle_t node;
	ssize_t len;
	uint32_t div;
	uint32_t i;
	uint32_t value;

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_usb_phy_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);

	len = OF_getprop_alloc(node, "force-aca",
	    (void **)&force_aca);

	sc->force_aca = FALSE;

	if (len > 0) {
		if (strncmp(force_aca, "true", len) == 0)
			sc->force_aca = TRUE;
	}

	OF_prop_free(force_aca);

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

	len = OF_getencprop_alloc_multi(node, "usb-hub-rst",
	    3 * sizeof(pcell_t), (void **)&prop);
	if (len > 0) {
		sc->hub_rst.dev = OF_device_from_xref(prop[0]);
		sc->hub_rst.pin = prop[1];
		sc->hub_rst.pol = prop[2];

		if (len > 1 || sc->hub_rst.dev == NULL)
			err = 1;
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

	value = CSR_READ_4(sc, AML_USB_PHY_CFG_REG);

	value &= ~(AML_USB_PHY_CFG_CLK_SEL_32K_ALT |
	    AML_USB_PHY_CFG_CLK_DIV_MASK |
	    AML_USB_PHY_CFG_CLK_SEL_MASK |
	    AML_USB_PHY_CFG_CLK_EN);

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
	case AML_SOC_HW_REV_M8B:
		value |= AML_USB_PHY_CFG_CLK_SEL_32K_ALT;
		break;
	default:
		div = 2;
		value |= AML_USB_PHY_CFG_CLK_SEL_XTAL;
		value |= ((div - 1) << AML_USB_PHY_CFG_CLK_DIV_SHIFT) &
		    AML_USB_PHY_CFG_CLK_DIV_MASK;
		value |= AML_USB_PHY_CFG_CLK_EN;
		break;
	}

	CSR_WRITE_4(sc, AML_USB_PHY_CFG_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CFG_REG);

	/*
	 * Configure the clock frequency and issue a power on reset.
	 */

	value = CSR_READ_4(sc, AML_USB_PHY_CTRL_REG);

	value &= ~AML_USB_PHY_CTRL_FSEL_MASK;

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
	case AML_SOC_HW_REV_M8B:
		value |= AML_USB_PHY_CTRL_FSEL_24M;
		break;
	default:
		value |= AML_USB_PHY_CTRL_FSEL_12M;
		break;
	}

	value |= AML_USB_PHY_CTRL_POR;

	CSR_WRITE_4(sc, AML_USB_PHY_CTRL_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CTRL_REG);

	DELAY(500);

	/*
	 * Enable by clearing the power on reset.
	 */

	value &= ~AML_USB_PHY_CTRL_POR;

	CSR_WRITE_4(sc, AML_USB_PHY_CTRL_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CTRL_REG);

	DELAY(1000);

	/*
	 * Check if the clock was detected.
	 */
	value = CSR_READ_4(sc, AML_USB_PHY_CTRL_REG);
	if ((value & AML_USB_PHY_CTRL_CLK_DETECTED) == 0)
		device_printf(dev, "PHY Clock not detected\n");

	/*
	 * If necessary enabled Accessory Charger Adaptor detection
	 * so that the port knows what mode to operate in.
	 */
	if (sc->force_aca) {
		value = CSR_READ_4(sc, AML_USB_PHY_ADP_BC_REG);

		value |= AML_USB_PHY_ADP_BC_ACA_EN;

		CSR_WRITE_4(sc, AML_USB_PHY_ADP_BC_REG, value);

		CSR_BARRIER(sc, AML_USB_PHY_ADP_BC_REG);

		DELAY(50);

		value = CSR_READ_4(sc, AML_USB_PHY_ADP_BC_REG);

		if ((value & AML_USB_PHY_ADP_BC_ACA_FLOATING) != 0) {
			device_printf(dev,
			    "force-aca requires newer silicon\n");
			goto fail;
		}
	}

	/*
	 * Reset the hub.
	 */
	if (sc->hub_rst.dev != NULL) {
		err = 0;

		if (GPIO_PIN_SET(sc->hub_rst.dev, sc->hub_rst.pin,
		    PIN_ON_FLAG(sc->hub_rst.pol)) != 0 ||
		    GPIO_PIN_SETFLAGS(sc->hub_rst.dev, sc->hub_rst.pin,
		    GPIO_PIN_OUTPUT) != 0)
			err = 1;

		DELAY(30);

		if (GPIO_PIN_SET(sc->hub_rst.dev, sc->hub_rst.pin,
		    PIN_OFF_FLAG(sc->hub_rst.pol)) != 0)
			err = 1;

		DELAY(60000);

		if (err) {
			device_printf(dev,
			    "could not use gpio to reset hub\n");
			goto fail;
		}
	}

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

	value = CSR_READ_4(sc, AML_USB_PHY_CTRL_REG);

	value |= AML_USB_PHY_CTRL_POR;

	CSR_WRITE_4(sc, AML_USB_PHY_CTRL_REG, value);

	CSR_BARRIER(sc, AML_USB_PHY_CTRL_REG);

	/* Turn off power */
	i = sc->npwr_en;
	while (i-- != 0) {
		GPIO_PIN_SET(sc->pwr_en[i].dev, sc->pwr_en[i].pin,
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

DRIVER_MODULE(aml8726_m6usbphy, simplebus, aml8726_usb_phy_driver,
    aml8726_usb_phy_devclass, 0, 0);
MODULE_DEPEND(aml8726_m6usbphy, aml8726_gpio, 1, 1, 1);
