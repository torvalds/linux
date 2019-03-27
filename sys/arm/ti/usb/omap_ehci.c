/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
#include <sys/proc.h>
#include <sys/condvar.h>

#include <dev/fdt/simplebus.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#include <machine/bus.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/usb/omap_usb.h>

#include <arm/ti/omap4/pandaboard/pandaboard.h>

/* EHCI */
#define	OMAP_USBHOST_HCCAPBASE                      0x0000
#define	OMAP_USBHOST_HCSPARAMS                      0x0004
#define	OMAP_USBHOST_HCCPARAMS                      0x0008
#define	OMAP_USBHOST_USBCMD                         0x0010
#define	OMAP_USBHOST_USBSTS                         0x0014
#define	OMAP_USBHOST_USBINTR                        0x0018
#define	OMAP_USBHOST_FRINDEX                        0x001C
#define	OMAP_USBHOST_CTRLDSSEGMENT                  0x0020
#define	OMAP_USBHOST_PERIODICLISTBASE               0x0024
#define	OMAP_USBHOST_ASYNCLISTADDR                  0x0028
#define	OMAP_USBHOST_CONFIGFLAG                     0x0050
#define	OMAP_USBHOST_PORTSC(i)                      (0x0054 + (0x04 * (i)))
#define	OMAP_USBHOST_INSNREG00                      0x0090
#define	OMAP_USBHOST_INSNREG01                      0x0094
#define	OMAP_USBHOST_INSNREG02                      0x0098
#define	OMAP_USBHOST_INSNREG03                      0x009C
#define	OMAP_USBHOST_INSNREG04                      0x00A0
#define	OMAP_USBHOST_INSNREG05_UTMI                 0x00A4
#define	OMAP_USBHOST_INSNREG05_ULPI                 0x00A4
#define	OMAP_USBHOST_INSNREG06                      0x00A8
#define	OMAP_USBHOST_INSNREG07                      0x00AC
#define	OMAP_USBHOST_INSNREG08                      0x00B0

#define OMAP_USBHOST_INSNREG04_DISABLE_UNSUSPEND   (1 << 5)

#define OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT   31
#define OMAP_USBHOST_INSNREG05_ULPI_PORTSEL_SHIFT   24
#define OMAP_USBHOST_INSNREG05_ULPI_OPSEL_SHIFT     22
#define OMAP_USBHOST_INSNREG05_ULPI_REGADD_SHIFT    16
#define OMAP_USBHOST_INSNREG05_ULPI_EXTREGADD_SHIFT 8
#define OMAP_USBHOST_INSNREG05_ULPI_WRDATA_SHIFT    0

#define ULPI_FUNC_CTRL_RESET                    (1 << 5)

/*-------------------------------------------------------------------------*/

/*
 * Macros for Set and Clear
 * See ULPI 1.1 specification to find the registers with Set and Clear offsets
 */
#define ULPI_SET(a)                             (a + 1)
#define ULPI_CLR(a)                             (a + 2)

/*-------------------------------------------------------------------------*/

/*
 * Register Map
 */
#define ULPI_VENDOR_ID_LOW                      0x00
#define ULPI_VENDOR_ID_HIGH                     0x01
#define ULPI_PRODUCT_ID_LOW                     0x02
#define ULPI_PRODUCT_ID_HIGH                    0x03
#define ULPI_FUNC_CTRL                          0x04
#define ULPI_IFC_CTRL                           0x07
#define ULPI_OTG_CTRL                           0x0a
#define ULPI_USB_INT_EN_RISE                    0x0d
#define ULPI_USB_INT_EN_FALL                    0x10
#define ULPI_USB_INT_STS                        0x13
#define ULPI_USB_INT_LATCH                      0x14
#define ULPI_DEBUG                              0x15
#define ULPI_SCRATCH                            0x16

#define OMAP_EHCI_HC_DEVSTR    "TI OMAP USB 2.0 controller"

struct omap_ehci_softc {
	ehci_softc_t        base;	/* storage for EHCI code */
	device_t            sc_dev;
};

static device_attach_t omap_ehci_attach;
static device_detach_t omap_ehci_detach;

/**
 *	omap_ehci_read_4 - read a 32-bit value from the EHCI registers
 *	omap_ehci_write_4 - write a 32-bit value from the EHCI registers
 *	@sc: omap ehci device context
 *	@off: byte offset within the register set to read from
 *	@val: the value to write into the register
 *	
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	nothing in case of write function, if read function returns the value read.
 */
static inline uint32_t
omap_ehci_read_4(struct omap_ehci_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->base.sc_io_res, off));
}

static inline void
omap_ehci_write_4(struct omap_ehci_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->base.sc_io_res, off, val);
}

/**
 *	omap_ehci_soft_phy_reset - resets the phy using the reset command
 *	@isc: omap ehci device context
 *	@port: port to send the reset over
 *	
 *
 *	LOCKING:
 *	none
 *
 *	RETURNS:
 *	nothing
 */
static void 
omap_ehci_soft_phy_reset(struct omap_ehci_softc *isc, unsigned int port)
{
	unsigned long timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);
	uint32_t reg;

	reg = ULPI_FUNC_CTRL_RESET
		/* FUNCTION_CTRL_SET register */
		| (ULPI_SET(ULPI_FUNC_CTRL) << OMAP_USBHOST_INSNREG05_ULPI_REGADD_SHIFT)
		/* Write */
		| (2 << OMAP_USBHOST_INSNREG05_ULPI_OPSEL_SHIFT)
		/* PORTn */
		| ((port + 1) << OMAP_USBHOST_INSNREG05_ULPI_PORTSEL_SHIFT)
		/* start ULPI access*/
		| (1 << OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT);

	omap_ehci_write_4(isc, OMAP_USBHOST_INSNREG05_ULPI, reg);

	/* Wait for ULPI access completion */
	while ((omap_ehci_read_4(isc, OMAP_USBHOST_INSNREG05_ULPI)
	       & (1 << OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT))) {

		/* Sleep for a tick */
		pause("USBPHY_RESET", 1);
		
		if (timeout-- == 0) {
			device_printf(isc->sc_dev, "PHY reset operation timed out\n");
			break;
		}
	}
}

/**
 *	omap_ehci_init - initialises the USB host EHCI controller
 *	@isc: omap ehci device context
 *
 *	This initialisation routine is quite heavily based on the work done by the
 *	OMAP Linux team (for which I thank them very much).  The init sequence is
 *	almost identical, diverging only for the FreeBSD specifics.
 *
 *	LOCKING:
 *	none
 *
 *	RETURNS:
 *	0 on success, a negative error code on failure.
 */
static int
omap_ehci_init(struct omap_ehci_softc *isc)
{
	uint32_t reg = 0;
	int i;
	device_t uhh_dev;
	
	uhh_dev = device_get_parent(isc->sc_dev);
	device_printf(isc->sc_dev, "Starting TI EHCI USB Controller\n");

	/* Set the interrupt threshold control, it controls the maximum rate at
	 * which the host controller issues interrupts.  We set it to 1 microframe
	 * at startup - the default is 8 mircoframes (equates to 1ms).
	 */
	reg = omap_ehci_read_4(isc, OMAP_USBHOST_USBCMD);
	reg &= 0xff00ffff;
	reg |= (1 << 16);
	omap_ehci_write_4(isc, OMAP_USBHOST_USBCMD, reg);

	/* Soft reset the PHY using PHY reset command over ULPI */
	for (i = 0; i < OMAP_HS_USB_PORTS; i++) {
		if (omap_usb_port_mode(uhh_dev, i) == EHCI_HCD_OMAP_MODE_PHY)
			omap_ehci_soft_phy_reset(isc, i);

	}

	return(0);
}

/**
 *	omap_ehci_probe - starts the given command
 *	@dev: 
 *	
 *	Effectively boilerplate EHCI resume code.
 *
 *	LOCKING:
 *	Caller should be holding the OMAP3_MMC lock.
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
static int
omap_ehci_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,ehci-omap"))
		return (ENXIO);

	device_set_desc(dev, OMAP_EHCI_HC_DEVSTR);
	
	return (BUS_PROBE_DEFAULT);
}

/**
 *	omap_ehci_attach - driver entry point, sets up the ECHI controller/driver
 *	@dev: the new device handle
 *	
 *	Sets up bus spaces, interrupt handles, etc for the EHCI controller.  It also
 *	parses the resource hints and calls omap_ehci_init() to initialise the
 *	H/W.
 *
 *	LOCKING:
 *	none
 *
 *	RETURNS:
 *	0 on success or a positive error code on failure.
 */
static int
omap_ehci_attach(device_t dev)
{
	struct omap_ehci_softc *isc = device_get_softc(dev);
	ehci_softc_t *sc = &isc->base;
#ifdef SOC_OMAP4
	phandle_t root;
#endif
	int err;
	int rid;

#ifdef SOC_OMAP4
	/* 
	 * If we're running a Pandaboard, run Pandaboard-specific 
	 * init code.
	 */
	root = OF_finddevice("/");
	if (ofw_bus_node_is_compatible(root, "ti,omap4-panda"))
		pandaboard_usb_hub_init();
#endif

	/* initialise some bus fields */
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	sprintf(sc->sc_vendor, "Texas Instruments");

	/* save the device */
	isc->sc_dev = dev;
	
	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(dev),
	                          &ehci_iterate_hw_softc)) {
		return (ENOMEM);
	}
	
	/* Allocate resource for the EHCI register set */
	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(dev, "Error: Could not map EHCI memory\n");
		goto error;
	}
	/* Request an interrupt resource */
	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "Error: could not allocate irq\n");
		goto error;
	}

	/* Add this device as a child of the USBus device */
	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(dev, "Error: could not add USB device\n");
		goto error;
	}

	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, OMAP_EHCI_HC_DEVSTR);
	
	/* Initialise the ECHI registers */
	err = omap_ehci_init(isc);
	if (err) {
		device_printf(dev, "Error: could not setup OMAP EHCI, %d\n", err);
		goto error;
	}
		
	/* Set the tag and size of the register set in the EHCI context */
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	/* Setup the interrupt */
	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
						 NULL, (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(dev, "Error: could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}
	
	/* Finally we are ready to kick off the ECHI host controller */
	err = ehci_init(sc);
	if (err == 0) {
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		device_printf(dev, "Error: USB init failed err=%d\n", err);
		goto error;
	}
	
	return (0);
	
error:
	omap_ehci_detach(dev);
	return (ENXIO);
}

/**
 *	omap_ehci_detach - detach the device and cleanup the driver
 *	@dev: device handle
 *	
 *	Clean-up routine where everything initialised in omap_ehci_attach is
 *	freed and cleaned up.  This function calls omap_ehci_fini() to shutdown
 *	the on-chip module.
 *
 *	LOCKING:
 *	none
 *
 *	RETURNS:
 *	Always returns 0 (success).
 */
static int
omap_ehci_detach(device_t dev)
{
	struct omap_ehci_softc *isc = device_get_softc(dev);
	ehci_softc_t *sc = &isc->base;
	int err;
	
	/* during module unload there are lots of children leftover */
	device_delete_children(dev);
	
	/*
	 * disable interrupts that might have been switched on in ehci_init
	 */
	if (sc->sc_io_res) {
		EWRITE4(sc, EHCI_USBINTR, 0);
	}
	
	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ehci_detach() after ehci_init()
		 */
		ehci_detach(sc);
		
		err = bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intr_hdl);
		if (err)
			device_printf(dev, "Error: could not tear down irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
	}
	
	/* Free the resources stored in the base EHCI handler */
	if (sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_io_res);
		sc->sc_io_res = NULL;
	}

	return (0);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, omap_ehci_probe),
	DEVMETHOD(device_attach, omap_ehci_attach),
	DEVMETHOD(device_detach, omap_ehci_detach),

	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	
	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	
	{0, 0}
};

static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(struct omap_ehci_softc),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(omap_ehci, omap_uhh, ehci_driver, ehci_devclass, 0, 0);
