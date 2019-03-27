/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Universal Serial Bus (USB) Controller
 * Chapter 44-45, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/gpio.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "gpio_if.h"
#include "opt_platform.h"

#define	ENUTMILEVEL3	(1 << 15)
#define	ENUTMILEVEL2	(1 << 14)

#define	GPIO_USB_PWR	134

#define	USB_ID		0x000	/* Identification register */
#define	USB_HWGENERAL	0x004	/* Hardware General */
#define	USB_HWHOST	0x008	/* Host Hardware Parameters */
#define	USB_HWDEVICE	0x00C	/* Device Hardware Parameters */
#define	USB_HWTXBUF	0x010	/* TX Buffer Hardware Parameters */
#define	USB_HWRXBUF	0x014	/* RX Buffer Hardware Parameters */
#define	USB_HCSPARAMS	0x104	/* Host Controller Structural Parameters */

#define	USBPHY_PWD		0x00	/* PHY Power-Down Register */
#define	USBPHY_PWD_SET		0x04	/* PHY Power-Down Register */
#define	USBPHY_PWD_CLR		0x08	/* PHY Power-Down Register */
#define	USBPHY_PWD_TOG		0x0C	/* PHY Power-Down Register */
#define	USBPHY_TX		0x10	/* PHY Transmitter Control Register */
#define	USBPHY_RX		0x20	/* PHY Receiver Control Register */
#define	USBPHY_RX_SET		0x24	/* PHY Receiver Control Register */
#define	USBPHY_RX_CLR		0x28	/* PHY Receiver Control Register */
#define	USBPHY_RX_TOG		0x2C	/* PHY Receiver Control Register */
#define	USBPHY_CTRL		0x30	/* PHY General Control Register */
#define	USBPHY_CTRL_SET		0x34	/* PHY General Control Register */
#define	USBPHY_CTRL_CLR		0x38	/* PHY General Control Register */
#define	USBPHY_CTRL_TOG		0x3C	/* PHY General Control Register */
#define	USBPHY_STATUS		0x40	/* PHY Status Register */
#define	USBPHY_DEBUG		0x50	/* PHY Debug Register */
#define	USBPHY_DEBUG_SET	0x54	/* PHY Debug Register */
#define	USBPHY_DEBUG_CLR	0x58	/* PHY Debug Register */
#define	USBPHY_DEBUG_TOG	0x5C	/* PHY Debug Register */
#define	USBPHY_DEBUG0_STATUS	0x60	/* UTMI Debug Status Register 0 */
#define	USBPHY_DEBUG1		0x70	/* UTMI Debug Status Register 1 */
#define	USBPHY_DEBUG1_SET	0x74	/* UTMI Debug Status Register 1 */
#define	USBPHY_DEBUG1_CLR	0x78	/* UTMI Debug Status Register 1 */
#define	USBPHY_DEBUG1_TOG	0x7C	/* UTMI Debug Status Register 1 */
#define	USBPHY_VERSION		0x80	/* UTMI RTL Version */
#define	USBPHY_IP		0x90	/* PHY IP Block Register */
#define	USBPHY_IP_SET		0x94	/* PHY IP Block Register */
#define	USBPHY_IP_CLR		0x98	/* PHY IP Block Register */
#define	USBPHY_IP_TOG		0x9C	/* PHY IP Block Register */

#define	USBPHY_CTRL_SFTRST	(1U << 31)
#define	USBPHY_CTRL_CLKGATE	(1 << 30)
#define	USBPHY_DEBUG_CLKGATE	(1 << 30)

#define	PHY_READ4(_sc, _reg)		\
	bus_space_read_4(_sc->bst_phy, _sc->bsh_phy, _reg)
#define	PHY_WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst_phy, _sc->bsh_phy, _reg, _val)

#define	USBC_READ4(_sc, _reg)		\
	bus_space_read_4(_sc->bst_usbc, _sc->bsh_usbc, _reg)
#define	USBC_WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst_usbc, _sc->bsh_usbc, _reg, _val)

/* Forward declarations */
static int	vybrid_ehci_attach(device_t dev);
static int	vybrid_ehci_detach(device_t dev);
static int	vybrid_ehci_probe(device_t dev);

struct vybrid_ehci_softc {
	ehci_softc_t		base;
	device_t		dev;
	struct resource		*res[6];
	bus_space_tag_t		bst_phy;
	bus_space_handle_t      bsh_phy;
	bus_space_tag_t		bst_usbc;
	bus_space_handle_t      bsh_usbc;
};

static struct resource_spec vybrid_ehci_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, vybrid_ehci_probe),
	DEVMETHOD(device_attach, vybrid_ehci_attach),
	DEVMETHOD(device_detach, vybrid_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	{ 0, 0 }
};

/* kobj_class definition */
static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(ehci_softc_t)
};

static devclass_t ehci_devclass;

DRIVER_MODULE(vybrid_ehci, simplebus, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(vybrid_ehci, usb, 1, 1, 1);

static void
vybrid_ehci_post_reset(struct ehci_softc *ehci_softc)
{
	uint32_t usbmode;

	/* Force HOST mode */
	usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_NOLPM);
	usbmode &= ~EHCI_UM_CM;
	usbmode |= EHCI_UM_CM_HOST;
	EOWRITE4(ehci_softc, EHCI_USBMODE_NOLPM, usbmode);
}

/*
 * Public methods
 */
static int
vybrid_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "fsl,mvf600-usb-ehci") == 0)
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family integrated USB controller");
	return (BUS_PROBE_DEFAULT);
}

static int
phy_init(struct vybrid_ehci_softc *esc)
{
	device_t sc_gpio_dev;
	int reg;

	/* Reset phy */
	reg = PHY_READ4(esc, USBPHY_CTRL);
	reg |= (USBPHY_CTRL_SFTRST);
	PHY_WRITE4(esc, USBPHY_CTRL, reg);

	/* Minimum reset time */
	DELAY(10000);

	reg &= ~(USBPHY_CTRL_SFTRST | USBPHY_CTRL_CLKGATE);
	PHY_WRITE4(esc, USBPHY_CTRL, reg);

	reg = (ENUTMILEVEL2 | ENUTMILEVEL3);
	PHY_WRITE4(esc, USBPHY_CTRL_SET, reg);

	/* Get the GPIO device, we need this to give power to USB */
	sc_gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (sc_gpio_dev == NULL) {
		device_printf(esc->dev, "Error: failed to get the GPIO dev\n");
		return (1);
	}

	/* Give power to USB */
	GPIO_PIN_SETFLAGS(sc_gpio_dev, GPIO_USB_PWR, GPIO_PIN_OUTPUT);
	GPIO_PIN_SET(sc_gpio_dev, GPIO_USB_PWR, GPIO_PIN_HIGH);

	/* Power up PHY */
	PHY_WRITE4(esc, USBPHY_PWD, 0x00);

	/* Ungate clocks */
	reg = PHY_READ4(esc, USBPHY_DEBUG);
	reg &= ~(USBPHY_DEBUG_CLKGATE);
	PHY_WRITE4(esc, USBPHY_DEBUG, reg);

#if 0
	printf("USBPHY_CTRL == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_CTRL));
	printf("USBPHY_IP == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_IP));
	printf("USBPHY_STATUS == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_STATUS));
	printf("USBPHY_DEBUG == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_DEBUG));
	printf("USBPHY_DEBUG0_STATUS == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_DEBUG0_STATUS));
	printf("USBPHY_DEBUG1 == 0x%08x\n",
	    PHY_READ4(esc, USBPHY_DEBUG1));
#endif

	return (0);
}

static int
vybrid_ehci_attach(device_t dev)
{
	struct vybrid_ehci_softc *esc;
	ehci_softc_t *sc;
	bus_space_handle_t bsh;
	int err;
	int reg;

	esc = device_get_softc(dev);
	esc->dev = dev;

	sc = &esc->base;
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	if (bus_alloc_resources(dev, vybrid_ehci_spec, esc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* EHCI registers */
	sc->sc_io_tag = rman_get_bustag(esc->res[0]);
	bsh = rman_get_bushandle(esc->res[0]);
	sc->sc_io_size = rman_get_size(esc->res[0]);

	esc->bst_usbc = rman_get_bustag(esc->res[1]);
	esc->bsh_usbc = rman_get_bushandle(esc->res[1]);

	esc->bst_phy = rman_get_bustag(esc->res[2]);
	esc->bsh_phy = rman_get_bushandle(esc->res[2]);

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(dev),
		&ehci_iterate_hw_softc))
		return (ENXIO);

#if 0
	printf("USBx_HCSPARAMS is 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HCSPARAMS));
	printf("USB_ID == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_ID));
	printf("USB_HWGENERAL == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HWGENERAL));
	printf("USB_HWHOST == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HWHOST));
	printf("USB_HWDEVICE == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HWDEVICE));
	printf("USB_HWTXBUF == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HWTXBUF));
	printf("USB_HWRXBUF == 0x%08x\n",
	    bus_space_read_4(sc->sc_io_tag, bsh, USB_HWRXBUF));
#endif

	if (phy_init(esc)) {
		device_printf(dev, "Could not setup PHY\n");
		return (1);
	}

	/*
	 * Set handle to USB related registers subregion used by
	 * generic EHCI driver.
	 */
	err = bus_space_subregion(sc->sc_io_tag, bsh, 0x100,
	    sc->sc_io_size, &sc->sc_io_hdl);
	if (err != 0)
		return (ENXIO);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, esc->res[3], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc,
	    &sc->sc_intr_hdl);
	if (err) {
		device_printf(dev, "Could not setup irq, "
		    "%d\n", err);
		return (1);
	}

	/* Add USB device */
	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(dev, "Could not add USB device\n");
		err = bus_teardown_intr(dev, esc->res[5],
		    sc->sc_intr_hdl);
		if (err)
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
		return (1);
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);

	strlcpy(sc->sc_vendor, "Freescale", sizeof(sc->sc_vendor));

	/* Set host mode */
	reg = bus_space_read_4(sc->sc_io_tag, sc->sc_io_hdl, 0xA8);
	reg |= 0x3;
	bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl, 0xA8, reg);

	/* Set flags  and callbacks*/
	sc->sc_flags |= EHCI_SCFLG_TT | EHCI_SCFLG_NORESTERM;
	sc->sc_vendor_post_reset = vybrid_ehci_post_reset;
	sc->sc_vendor_get_port_speed = ehci_get_port_speed_portsc;

	err = ehci_init(sc);
	if (!err) {
		sc->sc_flags |= EHCI_SCFLG_DONEINIT;
		err = device_probe_and_attach(sc->sc_bus.bdev);
	} else {
		device_printf(dev, "USB init failed err=%d\n", err);

		device_delete_child(dev, sc->sc_bus.bdev);
		sc->sc_bus.bdev = NULL;

		err = bus_teardown_intr(dev, esc->res[5],
		    sc->sc_intr_hdl);
		if (err)
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
		return (1);
	}
	return (0);
}

static int
vybrid_ehci_detach(device_t dev)
{
	struct vybrid_ehci_softc *esc;
	ehci_softc_t *sc;
	int err;

	esc = device_get_softc(dev);
	sc = &esc->base;

	/* First detach all children; we can't detach if that fails. */
	if ((err = device_delete_children(dev)) != 0)
		return (err);

	/*
	 * only call ehci_detach() after ehci_init()
	 */
	if (sc->sc_flags & EHCI_SCFLG_DONEINIT) {
		ehci_detach(sc);
		sc->sc_flags &= ~EHCI_SCFLG_DONEINIT;
	}

	/*
	 * Disable interrupts that might have been switched on in
	 * ehci_init.
	 */
	if (sc->sc_io_tag && sc->sc_io_hdl)
		bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl,
		    EHCI_USBINTR, 0);

	if (esc->res[5] && sc->sc_intr_hdl) {
		err = bus_teardown_intr(dev, esc->res[5],
		    sc->sc_intr_hdl);
		if (err) {
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
			return (err);
		}
		sc->sc_intr_hdl = NULL;
	}

	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);

	bus_release_resources(dev, vybrid_ehci_spec, esc->res);

	return (0);
}
