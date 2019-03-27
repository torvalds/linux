/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2011 Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/module.h>

#include <dev/fdt/simplebus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/usb/omap_usb.h>

/*
 * USB Host Module
 */

/* UHH */
#define	OMAP_USBHOST_UHH_REVISION                   0x0000
#define	OMAP_USBHOST_UHH_SYSCONFIG                  0x0010
#define	OMAP_USBHOST_UHH_SYSSTATUS                  0x0014
#define	OMAP_USBHOST_UHH_HOSTCONFIG                 0x0040
#define	OMAP_USBHOST_UHH_DEBUG_CSR                  0x0044

/* UHH Register Set */
#define UHH_SYSCONFIG_MIDLEMODE_MASK            (3UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_SMARTSTANDBY    (2UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_NOSTANDBY       (1UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_FORCESTANDBY    (0UL << 12)
#define UHH_SYSCONFIG_CLOCKACTIVITY             (1UL << 8)
#define UHH_SYSCONFIG_SIDLEMODE_MASK            (3UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_SMARTIDLE       (2UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_NOIDLE          (1UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_FORCEIDLE       (0UL << 3)
#define UHH_SYSCONFIG_ENAWAKEUP                 (1UL << 2)
#define UHH_SYSCONFIG_SOFTRESET                 (1UL << 1)
#define UHH_SYSCONFIG_AUTOIDLE                  (1UL << 0)

#define UHH_HOSTCONFIG_APP_START_CLK            (1UL << 31)
#define UHH_HOSTCONFIG_P3_CONNECT_STATUS        (1UL << 10)
#define UHH_HOSTCONFIG_P2_CONNECT_STATUS        (1UL << 9)
#define UHH_HOSTCONFIG_P1_CONNECT_STATUS        (1UL << 8)
#define UHH_HOSTCONFIG_ENA_INCR_ALIGN           (1UL << 5)
#define UHH_HOSTCONFIG_ENA_INCR16               (1UL << 4)
#define UHH_HOSTCONFIG_ENA_INCR8                (1UL << 3)
#define UHH_HOSTCONFIG_ENA_INCR4                (1UL << 2)
#define UHH_HOSTCONFIG_AUTOPPD_ON_OVERCUR_EN    (1UL << 1)
#define UHH_HOSTCONFIG_P1_ULPI_BYPASS           (1UL << 0)

/* The following are on rev2 (OMAP44xx) of the EHCI only */
#define UHH_SYSCONFIG_IDLEMODE_MASK             (3UL << 2)
#define UHH_SYSCONFIG_IDLEMODE_NOIDLE           (1UL << 2)
#define UHH_SYSCONFIG_STANDBYMODE_MASK          (3UL << 4)
#define UHH_SYSCONFIG_STANDBYMODE_NOSTDBY       (1UL << 4)

#define UHH_HOSTCONFIG_P1_MODE_MASK             (3UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_ULPI_PHY         (0UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_UTMI_PHY         (1UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_HSIC             (3UL << 16)
#define UHH_HOSTCONFIG_P2_MODE_MASK             (3UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_ULPI_PHY         (0UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_UTMI_PHY         (1UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_HSIC             (3UL << 18)

/*
 * Values of UHH_REVISION - Note: these are not given in the TRM but taken
 * from the linux OMAP EHCI driver (thanks guys).  It has been verified on
 * a Panda and Beagle board.
 */
#define OMAP_UHH_REV1  0x00000010      /* OMAP3 */
#define OMAP_UHH_REV2  0x50700100      /* OMAP4 */

struct omap_uhh_softc {
	struct simplebus_softc simplebus_sc;
	device_t            sc_dev;

	/* UHH register set */
	struct resource*    uhh_mem_res;

	/* The revision of the HS USB HOST read from UHH_REVISION */
	uint32_t            uhh_rev;

	/* The following details are provided by conf hints */
	int                 port_mode[3];
};

static device_attach_t omap_uhh_attach;
static device_detach_t omap_uhh_detach;

static inline uint32_t
omap_uhh_read_4(struct omap_uhh_softc *sc, bus_size_t off)
{
	return bus_read_4(sc->uhh_mem_res, off);
}

static inline void
omap_uhh_write_4(struct omap_uhh_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->uhh_mem_res, off, val);
}

static int
omap_uhh_init(struct omap_uhh_softc *isc)
{
	uint8_t tll_ch_mask;
	uint32_t reg;
	int i;

	/* Enable Clocks for high speed USBHOST */
	ti_prcm_clk_enable(USBHSHOST_CLK);

	/* Read the UHH revision */
	isc->uhh_rev = omap_uhh_read_4(isc, OMAP_USBHOST_UHH_REVISION);
	device_printf(isc->sc_dev, "UHH revision 0x%08x\n", isc->uhh_rev);

	if (isc->uhh_rev == OMAP_UHH_REV2) {
		/* For OMAP44xx devices you have to enable the per-port clocks:
		 *  PHY_MODE  - External ULPI clock
		 *  TTL_MODE  - Internal UTMI clock
		 *  HSIC_MODE - Internal 480Mhz and 60Mhz clocks
		 */
		switch(isc->port_mode[0]) {
		case EHCI_HCD_OMAP_MODE_UNKNOWN:
			break;
		case EHCI_HCD_OMAP_MODE_PHY:
			if (ti_prcm_clk_set_source(USBP1_PHY_CLK, EXT_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock source for port 0\n");
			if (ti_prcm_clk_enable(USBP1_PHY_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP1_PHY_CLK source for port 0\n");
			break;
		case EHCI_HCD_OMAP_MODE_TLL:
			if (ti_prcm_clk_enable(USBP1_UTMI_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP1_PHY_CLK source for port 0\n");
			break;
		case EHCI_HCD_OMAP_MODE_HSIC:
			if (ti_prcm_clk_enable(USBP1_HSIC_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP1_PHY_CLK source for port 0\n");
			break;
		default:
			device_printf(isc->sc_dev, "unknown port mode %d for port 0\n", isc->port_mode[0]);
		}
		switch(isc->port_mode[1]) {
		case EHCI_HCD_OMAP_MODE_UNKNOWN:
			break;
		case EHCI_HCD_OMAP_MODE_PHY:
			if (ti_prcm_clk_set_source(USBP2_PHY_CLK, EXT_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock source for port 0\n");
			if (ti_prcm_clk_enable(USBP2_PHY_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP2_PHY_CLK source for port 1\n");
			break;
		case EHCI_HCD_OMAP_MODE_TLL:
			if (ti_prcm_clk_enable(USBP2_UTMI_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP2_UTMI_CLK source for port 1\n");
			break;
		case EHCI_HCD_OMAP_MODE_HSIC:
			if (ti_prcm_clk_enable(USBP2_HSIC_CLK))
				device_printf(isc->sc_dev,
				    "failed to set clock USBP2_HSIC_CLK source for port 1\n");
			break;
		default:
			device_printf(isc->sc_dev, "unknown port mode %d for port 1\n", isc->port_mode[1]);
		}
	}

	/* Put UHH in SmartIdle/SmartStandby mode */
	reg = omap_uhh_read_4(isc, OMAP_USBHOST_UHH_SYSCONFIG);
	if (isc->uhh_rev == OMAP_UHH_REV1) {
		reg &= ~(UHH_SYSCONFIG_SIDLEMODE_MASK |
		    UHH_SYSCONFIG_MIDLEMODE_MASK);
		reg |= (UHH_SYSCONFIG_ENAWAKEUP |
		    UHH_SYSCONFIG_AUTOIDLE |
		    UHH_SYSCONFIG_CLOCKACTIVITY |
		    UHH_SYSCONFIG_SIDLEMODE_SMARTIDLE |
		    UHH_SYSCONFIG_MIDLEMODE_SMARTSTANDBY);
	} else if (isc->uhh_rev == OMAP_UHH_REV2) {
		reg &= ~UHH_SYSCONFIG_IDLEMODE_MASK;
		reg |=  UHH_SYSCONFIG_IDLEMODE_NOIDLE;
		reg &= ~UHH_SYSCONFIG_STANDBYMODE_MASK;
		reg |=  UHH_SYSCONFIG_STANDBYMODE_NOSTDBY;
	}
	omap_uhh_write_4(isc, OMAP_USBHOST_UHH_SYSCONFIG, reg);
	device_printf(isc->sc_dev, "OMAP_UHH_SYSCONFIG: 0x%08x\n", reg);

	reg = omap_uhh_read_4(isc, OMAP_USBHOST_UHH_HOSTCONFIG);

	/* Setup ULPI bypass and burst configurations */
	reg |= (UHH_HOSTCONFIG_ENA_INCR4 |
			UHH_HOSTCONFIG_ENA_INCR8 |
			UHH_HOSTCONFIG_ENA_INCR16);
	reg &= ~UHH_HOSTCONFIG_ENA_INCR_ALIGN;

	if (isc->uhh_rev == OMAP_UHH_REV1) {
		if (isc->port_mode[0] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P1_CONNECT_STATUS;
		if (isc->port_mode[1] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P2_CONNECT_STATUS;
		if (isc->port_mode[2] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~UHH_HOSTCONFIG_P3_CONNECT_STATUS;

		/* Bypass the TLL module for PHY mode operation */
		if ((isc->port_mode[0] == EHCI_HCD_OMAP_MODE_PHY) ||
		    (isc->port_mode[1] == EHCI_HCD_OMAP_MODE_PHY) ||
		    (isc->port_mode[2] == EHCI_HCD_OMAP_MODE_PHY))
			reg &= ~UHH_HOSTCONFIG_P1_ULPI_BYPASS;
		else
			reg |= UHH_HOSTCONFIG_P1_ULPI_BYPASS;

	} else if (isc->uhh_rev == OMAP_UHH_REV2) {
		reg |=  UHH_HOSTCONFIG_APP_START_CLK;

		/* Clear port mode fields for PHY mode*/
		reg &= ~UHH_HOSTCONFIG_P1_MODE_MASK;
		reg &= ~UHH_HOSTCONFIG_P2_MODE_MASK;

		if (isc->port_mode[0] == EHCI_HCD_OMAP_MODE_TLL)
			reg |= UHH_HOSTCONFIG_P1_MODE_UTMI_PHY;
		else if (isc->port_mode[0] == EHCI_HCD_OMAP_MODE_HSIC)
			reg |= UHH_HOSTCONFIG_P1_MODE_HSIC;

		if (isc->port_mode[1] == EHCI_HCD_OMAP_MODE_TLL)
			reg |= UHH_HOSTCONFIG_P2_MODE_UTMI_PHY;
		else if (isc->port_mode[1] == EHCI_HCD_OMAP_MODE_HSIC)
			reg |= UHH_HOSTCONFIG_P2_MODE_HSIC;
	}

	omap_uhh_write_4(isc, OMAP_USBHOST_UHH_HOSTCONFIG, reg);
	device_printf(isc->sc_dev, "UHH setup done, uhh_hostconfig=0x%08x\n", reg);


	/* I found the code and comments in the Linux EHCI driver - thanks guys :)
	 *
	 * "An undocumented "feature" in the OMAP3 EHCI controller, causes suspended
	 * ports to be taken out of suspend when the USBCMD.Run/Stop bit is cleared
	 * (for example when we do omap_uhh_bus_suspend). This breaks suspend-resume if
	 * the root-hub is allowed to suspend. Writing 1 to this undocumented
	 * register bit disables this feature and restores normal behavior."
	 */
#if 0
	omap_uhh_write_4(isc, OMAP_USBHOST_INSNREG04,
	    OMAP_USBHOST_INSNREG04_DISABLE_UNSUSPEND);
#endif
	tll_ch_mask = 0;
	for (i = 0; i < OMAP_HS_USB_PORTS; i++) {
		if (isc->port_mode[i] == EHCI_HCD_OMAP_MODE_TLL)
			tll_ch_mask |= (1 << i);
	}
	if (tll_ch_mask)
		omap_tll_utmi_enable(tll_ch_mask);

	return(0);
}

/**
 *	omap_uhh_fini - shutdown the EHCI controller
 *	@isc: omap ehci device context
 *
 *
 *
 *	LOCKING:
 *	none
 *
 *	RETURNS:
 *	0 on success, a negative error code on failure.
 */
static void
omap_uhh_fini(struct omap_uhh_softc *isc)
{
	unsigned long timeout;

	device_printf(isc->sc_dev, "Stopping TI EHCI USB Controller\n");

	/* Set the timeout */
	if (hz < 10)
		timeout = 1;
	else
		timeout = (100 * hz) / 1000;

	/* Reset the UHH, OHCI and EHCI modules */
	omap_uhh_write_4(isc, OMAP_USBHOST_UHH_SYSCONFIG, 0x0002);
	while ((omap_uhh_read_4(isc, OMAP_USBHOST_UHH_SYSSTATUS) & 0x07) == 0x00) {
		/* Sleep for a tick */
		pause("USBRESET", 1);

		if (timeout-- == 0) {
			device_printf(isc->sc_dev, "operation timed out\n");
			break;
		}
	}

	/* Disable functional and interface clocks for the TLL and HOST modules */
	ti_prcm_clk_disable(USBHSHOST_CLK);

	device_printf(isc->sc_dev, "Clock to USB host has been disabled\n");
}

int
omap_usb_port_mode(device_t dev, int port)
{
	struct omap_uhh_softc *isc;

	isc = device_get_softc(dev);
	if ((port < 0) || (port >= OMAP_HS_USB_PORTS))
		return (-1);

	return isc->port_mode[port];
}

static int
omap_uhh_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,usbhs-host"))
		return (ENXIO);

	device_set_desc(dev, "TI OMAP USB 2.0 Host module");

	return (BUS_PROBE_DEFAULT);
}

static int
omap_uhh_attach(device_t dev)
{
	struct omap_uhh_softc *isc = device_get_softc(dev);
	int err;
	int rid;
	int i;
	phandle_t node;
	char propname[16];
	char *mode;

	/* save the device */
	isc->sc_dev = dev;

	/* Allocate resource for the UHH register set */
	rid = 0;
	isc->uhh_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!isc->uhh_mem_res) {
		device_printf(dev, "Error: Could not map UHH memory\n");
		goto error;
	}

	node = ofw_bus_get_node(dev);

	if (node == -1)
		goto error;

	/* Get port modes from FDT */
	for (i = 0; i < OMAP_HS_USB_PORTS; i++) {
		isc->port_mode[i] = EHCI_HCD_OMAP_MODE_UNKNOWN;
		snprintf(propname, sizeof(propname),
		    "port%d-mode", i+1);

		if (OF_getprop_alloc(node, propname, (void**)&mode) <= 0)
			continue;
		if (strcmp(mode, "ehci-phy") == 0)
			isc->port_mode[i] = EHCI_HCD_OMAP_MODE_PHY;
		else if (strcmp(mode, "ehci-tll") == 0)
			isc->port_mode[i] = EHCI_HCD_OMAP_MODE_TLL;
		else if (strcmp(mode, "ehci-hsic") == 0)
			isc->port_mode[i] = EHCI_HCD_OMAP_MODE_HSIC;
	}

	/* Initialise the ECHI registers */
	err = omap_uhh_init(isc);
	if (err) {
		device_printf(dev, "Error: could not setup OMAP EHCI, %d\n", err);
		goto error;
	}

	simplebus_init(dev, node);

	/*
	 * Allow devices to identify.
	 */
	bus_generic_probe(dev);

	/*
	 * Now walk the OFW tree and attach top-level devices.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node))
		simplebus_add_device(dev, node, 0, NULL, -1, NULL);
	return (bus_generic_attach(dev));

error:
	omap_uhh_detach(dev);
	return (ENXIO);
}

static int
omap_uhh_detach(device_t dev)
{
	struct omap_uhh_softc *isc = device_get_softc(dev);

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (isc->uhh_mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, isc->uhh_mem_res);
		isc->uhh_mem_res = NULL;
	}

	omap_uhh_fini(isc);

	return (0);
}

static device_method_t omap_uhh_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, omap_uhh_probe),
	DEVMETHOD(device_attach, omap_uhh_attach),
	DEVMETHOD(device_detach, omap_uhh_detach),

	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

DEFINE_CLASS_1(omap_uhh, omap_uhh_driver, omap_uhh_methods,
    sizeof(struct omap_uhh_softc), simplebus_driver);
static devclass_t omap_uhh_devclass;
DRIVER_MODULE(omap_uhh, simplebus, omap_uhh_driver, omap_uhh_devclass, 0, 0);
