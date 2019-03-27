/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/*
 * FDT attachment driver for the USB Enhanced Host Controller.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

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

#if !defined(__aarch64__)
#include <arm/mv/mvreg.h>
#endif
#include <arm/mv/mvvar.h>

#define	EHCI_VENDORID_MRVL	0x1286
#define	EHCI_HC_DEVSTR		"Marvell Integrated USB 2.0 controller"

static device_attach_t mv_ehci_attach;
static device_detach_t mv_ehci_detach;

static int err_intr(void *arg);

static struct resource *irq_err;
static void *ih_err;

/* EHCI HC regs start at this offset within USB range */
#define	MV_USB_HOST_OFST	0x0100

#define	USB_BRIDGE_INTR_CAUSE	0x210
#define	USB_BRIDGE_INTR_MASK	0x214
#define	USB_BRIDGE_ERR_ADDR	0x21C

#define	MV_USB_ADDR_DECODE_ERR (1 << 0)
#define	MV_USB_HOST_UNDERFLOW  (1 << 1)
#define	MV_USB_HOST_OVERFLOW   (1 << 2)
#define	MV_USB_DEVICE_UNDERFLOW (1 << 3)

enum mv_ehci_hwtype {
	HWTYPE_NONE = 0,
	HWTYPE_MV_EHCI_V1,
	HWTYPE_MV_EHCI_V2,
};

static struct ofw_compat_data compat_data[] = {
	{"mrvl,usb-ehci",		HWTYPE_MV_EHCI_V1},
	{"marvell,orion-ehci",		HWTYPE_MV_EHCI_V2},
	{"marvell,armada-3700-ehci",	HWTYPE_MV_EHCI_V2},
	{NULL,				HWTYPE_NONE}
};

static void
mv_ehci_post_reset(struct ehci_softc *ehci_softc)
{
	uint32_t usbmode;

	/* Force HOST mode */
	usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_NOLPM);
	usbmode &= ~EHCI_UM_CM;
	usbmode |= EHCI_UM_CM_HOST;
	EOWRITE4(ehci_softc, EHCI_USBMODE_NOLPM, usbmode);
}

static int
mv_ehci_probe(device_t self)
{

	if (!ofw_bus_status_okay(self))
		return (ENXIO);

	if (!ofw_bus_search_compatible(self, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(self, EHCI_HC_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static int
mv_ehci_attach(device_t self)
{
	ehci_softc_t *sc = device_get_softc(self);
	enum mv_ehci_hwtype hwtype;
	bus_space_handle_t bsh;
	int err;
	int rid;

	/* initialise some bus fields */
	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	hwtype = ofw_bus_search_compatible(self, compat_data)->ocd_data;
	if (hwtype == HWTYPE_NONE) {
		device_printf(self, "Wrong HW type flag detected\n");
		return (ENXIO);
	}

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(self), &ehci_iterate_hw_softc)) {
		return (ENOMEM);
	}

	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(self, "Could not map memory\n");
		goto error;
	}
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	bsh = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res) - MV_USB_HOST_OFST;

	/*
	 * Marvell EHCI host controller registers start at certain offset
	 * within the whole USB registers range, so create a subregion for the
	 * host mode configuration purposes.
	 */

	if (bus_space_subregion(sc->sc_io_tag, bsh, MV_USB_HOST_OFST,
	    sc->sc_io_size, &sc->sc_io_hdl) != 0)
		panic("%s: unable to subregion USB host registers",
		    device_get_name(self));

	rid = 0;
	if (hwtype == HWTYPE_MV_EHCI_V1) {
		irq_err = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
		    RF_SHAREABLE | RF_ACTIVE);
		if (irq_err == NULL) {
			device_printf(self, "Could not allocate error irq\n");
			mv_ehci_detach(self);
			return (ENXIO);
		}
		rid = 1;
	}

	/*
	 * Notice: Marvell EHCI controller has TWO interrupt lines, so make
	 * sure to use the correct rid for the main one (controller interrupt)
	 * -- refer to DTS for the right resource number to use here.
	 */
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(self, "Could not allocate irq\n");
		goto error;
	}

	sc->sc_bus.bdev = device_add_child(self, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(self, "Could not add USB device\n");
		goto error;
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, EHCI_HC_DEVSTR);

	sprintf(sc->sc_vendor, "Marvell");

	if (hwtype == HWTYPE_MV_EHCI_V1) {
		err = bus_setup_intr(self, irq_err, INTR_TYPE_BIO,
		    err_intr, NULL, sc, &ih_err);
		if (err) {
			device_printf(self, "Could not setup error irq, %d\n", err);
			ih_err = NULL;
			goto error;
		}
	}

	EWRITE4(sc, USB_BRIDGE_INTR_MASK, MV_USB_ADDR_DECODE_ERR |
	    MV_USB_HOST_UNDERFLOW | MV_USB_HOST_OVERFLOW |
	    MV_USB_DEVICE_UNDERFLOW);

	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}

	/*
	 * Workaround for Marvell integrated EHCI controller: reset of
	 * the EHCI core clears the USBMODE register, which sets the core in
	 * an undefined state (neither host nor agent), so it needs to be set
	 * again for proper operation.
	 *
	 * Refer to errata document MV-S500832-00D.pdf (p. 5.24 GL USB-2) for
	 * details.
	 */
	sc->sc_vendor_post_reset = mv_ehci_post_reset;
	if (bootverbose)
		device_printf(self, "5.24 GL USB-2 workaround enabled\n");

	/* XXX all MV chips need it? */
	sc->sc_flags |= EHCI_SCFLG_TT | EHCI_SCFLG_NORESTERM;
	sc->sc_vendor_get_port_speed = ehci_get_port_speed_portsc;
	err = ehci_init(sc);
	if (!err) {
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		device_printf(self, "USB init failed err=%d\n", err);
		goto error;
	}
	return (0);

error:
	mv_ehci_detach(self);
	return (ENXIO);
}

static int
mv_ehci_detach(device_t self)
{
	ehci_softc_t *sc = device_get_softc(self);
	int err;

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	/*
	 * disable interrupts that might have been switched on in mv_ehci_attach
	 */
	if (sc->sc_io_res) {
		EWRITE4(sc, USB_BRIDGE_INTR_MASK, 0);
	}
	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ehci_detach() after ehci_init()
		 */
		ehci_detach(sc);

		err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);

		if (err)
			/* XXX or should we panic? */
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
		sc->sc_intr_hdl = NULL;
	}
	if (irq_err && ih_err) {
		err = bus_teardown_intr(self, irq_err, ih_err);

		if (err)
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
		ih_err = NULL;
	}
	if (irq_err) {
		bus_release_resource(self, SYS_RES_IRQ, 0, irq_err);
		irq_err = NULL;
	}
	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 1, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, 0,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}
	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);

	return (0);
}

static int
err_intr(void *arg)
{
	ehci_softc_t *sc = arg;
	unsigned int cause;

	cause = EREAD4(sc, USB_BRIDGE_INTR_CAUSE);
	if (cause) {
		printf("USB error: ");
		if (cause & MV_USB_ADDR_DECODE_ERR) {
			uint32_t addr;

			addr = EREAD4(sc, USB_BRIDGE_ERR_ADDR);
			printf("address decoding error (addr=%#x)\n", addr);
		}
		if (cause & MV_USB_HOST_UNDERFLOW)
			printf("host underflow\n");
		if (cause & MV_USB_HOST_OVERFLOW)
			printf("host overflow\n");
		if (cause & MV_USB_DEVICE_UNDERFLOW)
			printf("device underflow\n");
		if (cause & ~(MV_USB_ADDR_DECODE_ERR | MV_USB_HOST_UNDERFLOW |
		    MV_USB_HOST_OVERFLOW | MV_USB_DEVICE_UNDERFLOW))
			printf("unknown cause (cause=%#x)\n", cause);

		EWRITE4(sc, USB_BRIDGE_INTR_CAUSE, 0);
	}
	return (FILTER_HANDLED);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, mv_ehci_probe),
	DEVMETHOD(device_attach, mv_ehci_attach),
	DEVMETHOD(device_detach, mv_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(ehci_softc_t),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci_mv, simplebus, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(ehci_mv, usb, 1, 1, 1);
