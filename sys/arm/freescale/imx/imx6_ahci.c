/*-
 * Copyright (c) 2017 Rogiel Sulzbach <rogiel@allogica.com>
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
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/ahci/ahci.h>
#include <arm/freescale/imx/imx_iomuxreg.h>
#include <arm/freescale/imx/imx_iomuxvar.h>
#include <arm/freescale/imx/imx_ccmvar.h>

#define	SATA_TIMER1MS				0x000000e0

#define	SATA_P0PHYCR				0x00000178
#define	  SATA_P0PHYCR_CR_READ			  (1 << 19)
#define	  SATA_P0PHYCR_CR_WRITE			  (1 << 18)
#define	  SATA_P0PHYCR_CR_CAP_DATA		  (1 << 17)
#define	  SATA_P0PHYCR_CR_CAP_ADDR		  (1 << 16)
#define	  SATA_P0PHYCR_CR_DATA_IN(v)		  ((v) & 0xffff)

#define	SATA_P0PHYSR				0x0000017c
#define	  SATA_P0PHYSR_CR_ACK			  (1 << 18)
#define	  SATA_P0PHYSR_CR_DATA_OUT(v)		  ((v) & 0xffff)

/* phy registers */
#define	SATA_PHY_CLOCK_RESET			0x7f3f
#define	  SATA_PHY_CLOCK_RESET_RST		  (1 << 0)

#define	SATA_PHY_LANE0_OUT_STAT			0x2003
#define	  SATA_PHY_LANE0_OUT_STAT_RX_PLL_STATE	  (1 << 1)

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-ahci", true},
	{NULL,             false}
};

static int
imx6_ahci_phy_ctrl(struct ahci_controller* sc, uint32_t bitmask, bool on)
{
	uint32_t v;
	int timeout;
	bool state;

	v = ATA_INL(sc->r_mem, SATA_P0PHYCR);
	if (on) {
		v |= bitmask;
	} else {
		v &= ~bitmask;
	}
	ATA_OUTL(sc->r_mem, SATA_P0PHYCR, v);

	for (timeout = 5000; timeout > 0; --timeout) {
		v = ATA_INL(sc->r_mem, SATA_P0PHYSR);
		state = (v & SATA_P0PHYSR_CR_ACK) == SATA_P0PHYSR_CR_ACK;
		if(state == on) {
			break;
		}
		DELAY(100);
	}

	if (timeout > 0) {
		return (0);
	}

	return (ETIMEDOUT);
}

static int
imx6_ahci_phy_addr(struct ahci_controller* sc, uint32_t addr)
{
	int error;

	DELAY(100);

	ATA_OUTL(sc->r_mem, SATA_P0PHYCR, addr);

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_CAP_ADDR, true);
	if (error != 0) {
		device_printf(sc->dev,
		    "%s: timeout on SATA_P0PHYCR_CR_CAP_ADDR=1\n",
		    __FUNCTION__);
		return (error);
	}

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_CAP_ADDR, false);
	if (error != 0) {
		device_printf(sc->dev,
		    "%s: timeout on SATA_P0PHYCR_CR_CAP_ADDR=0\n",
		    __FUNCTION__);
		return (error);
	}

	return (0);
}

static int
imx6_ahci_phy_write(struct ahci_controller* sc, uint32_t addr,
		    uint16_t data)
{
	int error;

	error = imx6_ahci_phy_addr(sc, addr);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on imx6_ahci_phy_addr\n",
		    __FUNCTION__);
		return (error);
	}

	ATA_OUTL(sc->r_mem, SATA_P0PHYCR, data);

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_CAP_DATA, true);
	if (error != 0) {
		device_printf(sc->dev,
		    "%s: error on SATA_P0PHYCR_CR_CAP_DATA=1\n", __FUNCTION__);
		return (error);
	}
	if (imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_CAP_DATA, false) != 0) {
		device_printf(sc->dev,
		    "%s: error on SATA_P0PHYCR_CR_CAP_DATA=0\n", __FUNCTION__);
		return (error);
	}

	if ((addr == SATA_PHY_CLOCK_RESET) && data) {
		/* we can't check ACK after RESET */
		ATA_OUTL(sc->r_mem, SATA_P0PHYCR,
		    SATA_P0PHYCR_CR_DATA_IN(data) | SATA_P0PHYCR_CR_WRITE);
		return (0);
	}

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_WRITE, true);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on SATA_P0PHYCR_CR_WRITE=1\n",
		    __FUNCTION__);
		return (error);
	}

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_WRITE, false);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on SATA_P0PHYCR_CR_WRITE=0\n",
		    __FUNCTION__);
		return (error);
	}

	return (0);
}

static int
imx6_ahci_phy_read(struct ahci_controller* sc, uint32_t addr, uint16_t* val)
{
	int error;
	uint32_t v;

	error = imx6_ahci_phy_addr(sc, addr);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on imx6_ahci_phy_addr\n",
		    __FUNCTION__);
		return (error);
	}

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_READ, true);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on SATA_P0PHYCR_CR_READ=1\n",
		    __FUNCTION__);
		return (error);
	}

	v = ATA_INL(sc->r_mem, SATA_P0PHYSR);

	error = imx6_ahci_phy_ctrl(sc, SATA_P0PHYCR_CR_READ, false);
	if (error != 0) {
		device_printf(sc->dev, "%s: error on SATA_P0PHYCR_CR_READ=0\n",
		    __FUNCTION__);
		return (error);
	}

	*val = SATA_P0PHYSR_CR_DATA_OUT(v);
	return (0);
}

static int
imx6_ahci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev)) {
		return (ENXIO);
	}

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
		return (ENXIO);
	}
	device_set_desc(dev, "i.MX6 Integrated AHCI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
imx6_ahci_attach(device_t dev)
{
	struct ahci_controller* ctlr;
	uint16_t pllstat;
	uint32_t v;
	int error, timeout;

	ctlr = device_get_softc(dev);

	/* Power up the controller and phy. */
	error = imx6_ccm_sata_enable();
	if (error != 0) {
		device_printf(dev, "error enabling controller and phy\n");
		return (error);
	}

	ctlr->vendorid = 0;
	ctlr->deviceid = 0;
	ctlr->subvendorid = 0;
	ctlr->subdeviceid = 0;
	ctlr->numirqs = 1;
	ctlr->r_rid = 0;
	if ((ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE)) == NULL) {
		return (ENXIO);
	}

	v = imx_iomux_gpr_get(IOMUX_GPR13);
	/* Clear out existing values; these numbers are bitmasks. */
	v &= ~(IOMUX_GPR13_SATA_PHY_8(7) 	|
	       IOMUX_GPR13_SATA_PHY_7(0x1f) 	|
	       IOMUX_GPR13_SATA_PHY_6(7) 	|
	       IOMUX_GPR13_SATA_SPEED(1) 	|
	       IOMUX_GPR13_SATA_PHY_5(1) 	|
	       IOMUX_GPR13_SATA_PHY_4(7) 	|
	       IOMUX_GPR13_SATA_PHY_3(0xf) 	|
	       IOMUX_GPR13_SATA_PHY_2(0x1f) 	|
	       IOMUX_GPR13_SATA_PHY_1(1) 	|
	       IOMUX_GPR13_SATA_PHY_0(1));
	/* setting */
	v |= IOMUX_GPR13_SATA_PHY_8(5) 		|     /* Rx 3.0db */
	     IOMUX_GPR13_SATA_PHY_7(0x12) 	|     /* Rx SATA2m */
	     IOMUX_GPR13_SATA_PHY_6(3) 		|     /* Rx DPLL mode */
	     IOMUX_GPR13_SATA_SPEED(1) 		|     /* 3.0GHz */
	     IOMUX_GPR13_SATA_PHY_5(0) 		|     /* SpreadSpectram */
	     IOMUX_GPR13_SATA_PHY_4(4) 		|     /* Tx Attenuation 9/16 */
	     IOMUX_GPR13_SATA_PHY_3(0) 		|     /* Tx Boost 0db */
	     IOMUX_GPR13_SATA_PHY_2(0x11) 	|     /* Tx Level 1.104V */
	     IOMUX_GPR13_SATA_PHY_1(1);               /* PLL clock enable */
	imx_iomux_gpr_set(IOMUX_GPR13, v);

	/* phy reset */
	error = imx6_ahci_phy_write(ctlr, SATA_PHY_CLOCK_RESET,
	    SATA_PHY_CLOCK_RESET_RST);
	if (error != 0) {
		device_printf(dev, "cannot reset PHY\n");
		goto fail;
	}

	for (timeout = 50; timeout > 0; --timeout) {
		DELAY(100);
		error = imx6_ahci_phy_read(ctlr, SATA_PHY_LANE0_OUT_STAT,
		    &pllstat);
		if (error != 0) {
			device_printf(dev, "cannot read LANE0 status\n");
			goto fail;
		}
		if (pllstat & SATA_PHY_LANE0_OUT_STAT_RX_PLL_STATE) {
			break;
		}
	}
	if (timeout <= 0) {
		device_printf(dev, "time out reading LANE0 status\n");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Support Staggered Spin-up */
	v = ATA_INL(ctlr->r_mem, AHCI_CAP);
	ATA_OUTL(ctlr->r_mem, AHCI_CAP, v | AHCI_CAP_SSS);

	/* Ports Implemented. must set 1 */
	v = ATA_INL(ctlr->r_mem, AHCI_PI);
	ATA_OUTL(ctlr->r_mem, AHCI_PI, v | (1 << 0));

	/* set 1ms-timer = AHB clock / 1000 */
	ATA_OUTL(ctlr->r_mem, SATA_TIMER1MS,
		 imx_ccm_ahb_hz() / 1000);

	/*
	 * Note: ahci_attach will release ctlr->r_mem on errors automatically
	 */
	return (ahci_attach(dev));

fail:
	bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
	return (error);
}

static int
imx6_ahci_detach(device_t dev)
{

	return (ahci_detach(dev));
}

static device_method_t imx6_ahci_ata_methods[] = {
	/* device probe, attach and detach methods */
	DEVMETHOD(device_probe,  imx6_ahci_probe),
	DEVMETHOD(device_attach, imx6_ahci_attach),
	DEVMETHOD(device_detach, imx6_ahci_detach),

	/* ahci bus methods */
	DEVMETHOD(bus_print_child,        ahci_print_child),
	DEVMETHOD(bus_alloc_resource,     ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,   ahci_release_resource),
	DEVMETHOD(bus_setup_intr,         ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,      ahci_teardown_intr),
	DEVMETHOD(bus_child_location_str, ahci_child_location_str),

	DEVMETHOD_END
};

static driver_t ahci_ata_driver = {
	"ahci",
	imx6_ahci_ata_methods,
	sizeof(struct ahci_controller)
};

DRIVER_MODULE(imx6_ahci, simplebus, ahci_ata_driver, ahci_devclass, 0, 0);
MODULE_DEPEND(imx6_ahci, ahci, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data)
