/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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

/*
 * USBPHY driver for Freescale i.MX6 family of SoCs.
 */

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/freescale/imx/imx_ccmvar.h>
#include <arm/freescale/imx/imx6_anatopreg.h>
#include <arm/freescale/imx/imx6_anatopvar.h>

/*
 * Hardware register defines.
 */
#define	PWD_REG				0x0000
#define	CTRL_STATUS_REG			0x0030
#define	CTRL_SET_REG			0x0034
#define	CTRL_CLR_REG			0x0038
#define	CTRL_TOGGLE_REG			0x003c
#define	  CTRL_SFTRST			  (1U << 31)
#define	  CTRL_CLKGATE			  (1 << 30)
#define	  CTRL_ENUTMILEVEL3		  (1 << 15)
#define	  CTRL_ENUTMILEVEL2		  (1 << 14)

struct usbphy_softc {
	device_t	dev;
	struct resource	*mem_res;
	u_int		phy_num;
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-usbphy",	true},
	{"fsl,imx6ul-usbphy",	true},
	{NULL,			false}
};

static int
usbphy_detach(device_t dev)
{
	struct usbphy_softc *sc;

	sc = device_get_softc(dev);

	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (0);
}

static int
usbphy_attach(device_t dev)
{
	struct usbphy_softc *sc;
	int err, regoff, rid;

	sc = device_get_softc(dev);
	err = 0;

	/* Allocate bus_space resources. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		err = ENXIO;
		goto out;
	}

	/*
	 * XXX Totally lame way to get the unit number (but not quite as lame as
	 * adding an ad-hoc property to the fdt data).  This works as long as
	 * this driver is used for imx6 only.
	 */
	const uint32_t PWD_PHY1_REG_PHYSADDR = 0x020c9000;
	if (BUS_SPACE_PHYSADDR(sc->mem_res, 0) == PWD_PHY1_REG_PHYSADDR) {
		sc->phy_num = 0;
		regoff = 0;
	} else {
		sc->phy_num = 1;
		regoff = 0x60;
	}

	/*
	 * Based on a note in the u-boot source code, disable charger detection
	 * to avoid degrading the differential signaling on the DP line.  Note
	 * that this disables (by design) both charger detection and contact
	 * detection, because of the screwball mix of active-high and active-low
	 * bits in this register.
	 */
	imx6_anatop_write_4(IMX6_ANALOG_USB1_CHRG_DETECT + regoff, 
	    IMX6_ANALOG_USB_CHRG_DETECT_N_ENABLE | 
	    IMX6_ANALOG_USB_CHRG_DETECT_N_CHK_CHRG);

	imx6_anatop_write_4(IMX6_ANALOG_USB1_CHRG_DETECT + regoff, 
	    IMX6_ANALOG_USB_CHRG_DETECT_N_ENABLE | 
	    IMX6_ANALOG_USB_CHRG_DETECT_N_CHK_CHRG);

	/* XXX Configure the overcurrent detection here. */

	/*
	 * Turn on the phy clocks.
	 */
	imx_ccm_usbphy_enable(dev);

	/*
	 * Set the software reset bit, then clear both it and the clock gate bit
	 * to bring the device out of reset with the clock running.
	 */
	bus_write_4(sc->mem_res, CTRL_SET_REG, CTRL_SFTRST);
	bus_write_4(sc->mem_res, CTRL_CLR_REG, CTRL_SFTRST | CTRL_CLKGATE);

	/* Set UTMI+ level 2+3 bits to enable low and full speed devices. */
	bus_write_4(sc->mem_res, CTRL_SET_REG,
	    CTRL_ENUTMILEVEL2 | CTRL_ENUTMILEVEL3);

	/* Power up: clear all bits in the powerdown register. */
	bus_write_4(sc->mem_res, PWD_REG, 0);

	err = 0;

out:

	if (err != 0)
		usbphy_detach(dev);

	return (err);
}

static int
usbphy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX6 USB PHY");

	return (BUS_PROBE_DEFAULT);
}

static device_method_t usbphy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  usbphy_probe),
	DEVMETHOD(device_attach, usbphy_attach),
	DEVMETHOD(device_detach, usbphy_detach),

	DEVMETHOD_END
};

static driver_t usbphy_driver = {
	"usbphy",
	usbphy_methods,
	sizeof(struct usbphy_softc)
};

static devclass_t usbphy_devclass;

/*
 * This driver needs to start before the ehci driver, but later than the usual
 * "special" drivers like clocks and cpu.  Ehci starts at DEFAULT so SUPPORTDEV
 * is where this driver fits most.
 */
EARLY_DRIVER_MODULE(usbphy, simplebus, usbphy_driver, usbphy_devclass, 0, 0,
    BUS_PASS_SUPPORTDEV + BUS_PASS_ORDER_MIDDLE);

