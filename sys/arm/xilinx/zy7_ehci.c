/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 Thomas Skibo
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
 *
 * $FreeBSD$
 */

/*
 * A host-controller driver for Zynq-7000's USB OTG controller.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.  Ch. 15 covers the USB
 * controller and register definitions are in appendix B.34.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/ofw/ofw_bus.h>
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


/* Register definitions. */
#define ZY7_USB_ID				0x0000
#define ZY7_USB_HWGENERAL			0x0004
#define ZY7_USB_HWHOST				0x0008
#define ZY7_USB_HWDEVICE			0x000c
#define ZY7_USB_HWTXBUF				0x0010
#define ZY7_USB_HWRXBUF				0x0014
#define ZY7_USB_GPTIMER0LD			0x0080
#define ZY7_USB_GPTIMER0CTRL			0x0084
#define ZY7_USB_GPTIMER1LD			0x0088
#define ZY7_USB_GPTIMER1CTRL			0x008c
#define ZY7_USB_SBUSCFG				0x0090
#define ZY7_USB_CAPLENGTH_HCIVERSION		0x0100
#define ZY7_USB_HCSPARAMS			0x0104
#define ZY7_USB_HCCPARAMS			0x0108
#define ZY7_USB_DCIVERSION			0x0120
#define ZY7_USB_DCCPARAMS			0x0124
#define ZY7_USB_USBCMD				0x0140
#define ZY7_USB_USBSTS				0x0144
#define ZY7_USB_USBINTR				0x0148
#define ZY7_USB_FRINDEX				0x014c
#define ZY7_USB_PERIODICLISTBASE_DEICEADDR 	0x0154
#define ZY7_USB_ASYNCLISTADDR_ENDPOINTLISTADDR 	0x0158
#define ZY7_USB_TTCTRL				0x015c
#define ZY7_USB_BURSTSIZE			0x0160
#define ZY7_USB_TXFILLTUNING			0x0164
#define   ZY7_USB_TXFILLTUNING_TXFIFOTHRES_SHFT		16
#define   ZY7_USB_TXFILLTUNING_TXFIFOTHRES_MASK		(0x3f<<16)
#define ZY7_USB_TXTFILLTUNING			0x0168
#define ZY7_USB_IC_USB				0x016c
#define ZY7_USB_ULPI_VIEWPORT			0x0170
#define   ZY7_USB_ULPI_VIEWPORT_WU			(1<<31)
#define   ZY7_USB_ULPI_VIEWPORT_RUN			(1<<30)
#define   ZY7_USB_ULPI_VIEWPORT_RW			(1<<29)
#define   ZY7_USB_ULPI_VIEWPORT_SS			(1<<27)
#define   ZY7_USB_ULPI_VIEWPORT_PORT_MASK		(7<<24)
#define   ZY7_USB_ULPI_VIEWPORT_PORT_SHIFT		24
#define   ZY7_USB_ULPI_VIEWPORT_ADDR_MASK		(0xff<<16)
#define   ZY7_USB_ULPI_VIEWPORT_ADDR_SHIFT		16
#define   ZY7_USB_ULPI_VIEWPORT_DATARD_MASK		(0xff<<8)
#define   ZY7_USB_ULPI_VIEWPORT_DATARD_SHIFT		8
#define   ZY7_USB_ULPI_VIEWPORT_DATAWR_MASK		(0xff<<0)
#define   ZY7_USB_ULPI_VIEWPORT_DATAWR_SHIFT		0
#define ZY7_USB_ENDPTNAK			0x0178
#define ZY7_USB_ENDPTNAKEN			0x017c
#define ZY7_USB_CONFIGFLAG			0x0180
#define ZY7_USB_PORTSC(n)			(0x0180+4*(n))
#define   ZY7_USB_PORTSC_PTS_MASK			(3<<30)
#define   ZY7_USB_PORTSC_PTS_SHIFT			30
#define   ZY7_USB_PORTSC_PTS_UTMI			(0<<30)
#define   ZY7_USB_PORTSC_PTS_ULPI			(2<<30)
#define   ZY7_USB_PORTSC_PTS_SERIAL			(3<<30)
#define   ZY7_USB_PORTSC_PTW				(1<<28)
#define   ZY7_USB_PORTSC_PTS2				(1<<25)
#define ZY7_USB_OTGSC				0x01a4
#define ZY7_USB_USBMODE				0x01a8
#define ZY7_USB_ENDPTSETUPSTAT			0x01ac
#define ZY7_USB_ENDPTPRIME			0x01b0
#define ZY7_USB_ENDPTFLUSH			0x01b4
#define ZY7_USB_ENDPTSTAT			0x01b8
#define ZY7_USB_ENDPTCOMPLETE			0x01bc
#define ZY7_USB_ENDPTCTRL(n)			(0x01c0+4*(n))

#define EHCI_REG_OFFSET	ZY7_USB_CAPLENGTH_HCIVERSION
#define EHCI_REG_SIZE	0x100

static void
zy7_ehci_post_reset(struct ehci_softc *ehci_softc)
{
	uint32_t usbmode;

	/* Force HOST mode */
	usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_NOLPM);
	usbmode &= ~EHCI_UM_CM;
	usbmode |= EHCI_UM_CM_HOST;
	EOWRITE4(ehci_softc, EHCI_USBMODE_NOLPM, usbmode);
}

static int
zy7_phy_config(device_t dev, bus_space_tag_t io_tag, bus_space_handle_t bsh)
{
	phandle_t node;
	char buf[64];
	uint32_t portsc;
	int tries;

	node = ofw_bus_get_node(dev);

	if (OF_getprop(node, "phy_type", buf, sizeof(buf)) > 0) {
		portsc = bus_space_read_4(io_tag, bsh, ZY7_USB_PORTSC(1));
		portsc &= ~(ZY7_USB_PORTSC_PTS_MASK | ZY7_USB_PORTSC_PTW |
			    ZY7_USB_PORTSC_PTS2);

		if (strcmp(buf,"ulpi") == 0)
			portsc |= ZY7_USB_PORTSC_PTS_ULPI;
		else if (strcmp(buf,"utmi") == 0)
			portsc |= ZY7_USB_PORTSC_PTS_UTMI;
		else if (strcmp(buf,"utmi-wide") == 0)
			portsc |= (ZY7_USB_PORTSC_PTS_UTMI |
				   ZY7_USB_PORTSC_PTW);
		else if (strcmp(buf, "serial") == 0)
			portsc |= ZY7_USB_PORTSC_PTS_SERIAL;

		bus_space_write_4(io_tag, bsh, ZY7_USB_PORTSC(1), portsc);
	}

	if (OF_getprop(node, "phy_vbus_ext", buf, sizeof(buf)) >= 0) {

		/* Tell PHY that VBUS is supplied externally. */
		bus_space_write_4(io_tag, bsh, ZY7_USB_ULPI_VIEWPORT,
				  ZY7_USB_ULPI_VIEWPORT_RUN |
				  ZY7_USB_ULPI_VIEWPORT_RW |
				  (0 << ZY7_USB_ULPI_VIEWPORT_PORT_SHIFT) |
				  (0x0b << ZY7_USB_ULPI_VIEWPORT_ADDR_SHIFT) |
				  (0x60 << ZY7_USB_ULPI_VIEWPORT_DATAWR_SHIFT)
			);

		tries = 100;
		while ((bus_space_read_4(io_tag, bsh, ZY7_USB_ULPI_VIEWPORT) &
			ZY7_USB_ULPI_VIEWPORT_RUN) != 0) {
			if (--tries < 0)
				return (-1);
			DELAY(1);
		}
	}

	return (0);
}

static int
zy7_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "xlnx,zy7_ehci"))
		return (ENXIO);

	device_set_desc(dev, "Zynq-7000 EHCI USB 2.0 controller");
	return (0);
}

static int zy7_ehci_detach(device_t dev);

static int
zy7_ehci_attach(device_t dev)
{
	ehci_softc_t *sc = device_get_softc(dev);
	bus_space_handle_t bsh;
	int err, rid;
	
	/* initialize some bus fields */
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(dev), &ehci_iterate_hw_softc))
		return (ENOMEM);

	/* Allocate memory. */
	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					       &rid, RF_ACTIVE);
	if (sc->sc_io_res == NULL) {
		device_printf(dev, "Can't allocate memory");
		zy7_ehci_detach(dev);
		return (ENOMEM);
	}

	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	bsh = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = EHCI_REG_SIZE;

	if (bus_space_subregion(sc->sc_io_tag, bsh, EHCI_REG_OFFSET,
				sc->sc_io_size, &sc->sc_io_hdl) != 0)
		panic("%s: unable to subregion USB host registers",
		      device_get_name(dev));

	/* Allocate IRQ. */
	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
						RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "Can't allocate IRQ\n");
		zy7_ehci_detach(dev);
		return (ENOMEM);
	}

	/* Add USB device */
	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(dev, "Could not add USB device\n");
		zy7_ehci_detach(dev);
		return (ENXIO);
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, "Zynq-7000 ehci USB 2.0 controller");

	strcpy(sc->sc_vendor, "Xilinx"); /* or IP vendor? */

	/* Activate the interrupt */
	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
			     NULL, (driver_intr_t *)ehci_interrupt, sc,
			     &sc->sc_intr_hdl);
	if (err) {
		device_printf(dev, "Cannot setup IRQ\n");
		zy7_ehci_detach(dev);
		return (err);
	}

	/* Customization. */
	sc->sc_flags |= EHCI_SCFLG_TT |	EHCI_SCFLG_NORESTERM;
	sc->sc_vendor_post_reset = zy7_ehci_post_reset;
	sc->sc_vendor_get_port_speed = ehci_get_port_speed_portsc;

	/* Modify FIFO burst threshold from 2 to 8. */
	bus_space_write_4(sc->sc_io_tag, bsh,
			  ZY7_USB_TXFILLTUNING,
			  8 << ZY7_USB_TXFILLTUNING_TXFIFOTHRES_SHFT);

	/* Handle PHY options. */
	if (zy7_phy_config(dev, sc->sc_io_tag, bsh) < 0) {
		device_printf(dev, "Cannot config phy!\n");
		zy7_ehci_detach(dev);
		return (EIO);
	}

	/* Init ehci. */
	err = ehci_init(sc);
	if (!err) {
		sc->sc_flags |= EHCI_SCFLG_DONEINIT;
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		device_printf(dev, "USB init failed err=%d\n", err);
		zy7_ehci_detach(dev);
		return (err);
	}

	return (0);
}

static int
zy7_ehci_detach(device_t dev)
{
	ehci_softc_t *sc = device_get_softc(dev);

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);
	
	if ((sc->sc_flags & EHCI_SCFLG_DONEINIT) != 0) {
		ehci_detach(sc);
		sc->sc_flags &= ~EHCI_SCFLG_DONEINIT;
	}

	if (sc->sc_irq_res) {
		if (sc->sc_intr_hdl != NULL)
			bus_teardown_intr(dev, sc->sc_irq_res,
					  sc->sc_intr_hdl);
		bus_release_resource(dev, SYS_RES_IRQ,
			     rman_get_rid(sc->sc_irq_res), sc->sc_irq_res);
	}

	if (sc->sc_io_res)
		bus_release_resource(dev, SYS_RES_MEMORY,
			     rman_get_rid(sc->sc_io_res), sc->sc_io_res);
	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);

	return (0);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		zy7_ehci_probe),
	DEVMETHOD(device_attach, 	zy7_ehci_attach),
	DEVMETHOD(device_detach, 	zy7_ehci_detach),
	DEVMETHOD(device_suspend, 	bus_generic_suspend),
	DEVMETHOD(device_resume, 	bus_generic_resume),
	DEVMETHOD(device_shutdown, 	bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	DEVMETHOD_END
};

static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(struct ehci_softc),
};
static devclass_t ehci_devclass;

DRIVER_MODULE(zy7_ehci, simplebus, ehci_driver, ehci_devclass, NULL, NULL);
MODULE_DEPEND(zy7_ehci, usb, 1, 1, 1);
