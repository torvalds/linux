/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@carlstedt.se) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * USB Enhanced Host Controller Driver, a.k.a. USB 2.0 controller.
 *
 * The EHCI 1.0 spec can be found at
 * http://developer.intel.com/technology/usb/download/ehci-r10.pdf
 * and the USB 2.0 spec at
 * http://www.usb.org/developers/docs/usb_20.zip
 */

/* The low level controller code for EHCI has been split into
 * PCI probes and EHCI specific code. This was done to facilitate the
 * sharing of code between *BSD's
 */

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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/usb_pci.h>
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>
#include "usb_if.h"

#define	PCI_EHCI_VENDORID_ACERLABS	0x10b9
#define	PCI_EHCI_VENDORID_AMD		0x1022
#define	PCI_EHCI_VENDORID_APPLE		0x106b
#define	PCI_EHCI_VENDORID_ATI		0x1002
#define	PCI_EHCI_VENDORID_CMDTECH	0x1095
#define	PCI_EHCI_VENDORID_INTEL		0x8086
#define	PCI_EHCI_VENDORID_NEC		0x1033
#define	PCI_EHCI_VENDORID_OPTI		0x1045
#define	PCI_EHCI_VENDORID_PHILIPS	0x1131
#define	PCI_EHCI_VENDORID_SIS		0x1039
#define	PCI_EHCI_VENDORID_NVIDIA	0x12D2
#define	PCI_EHCI_VENDORID_NVIDIA2	0x10DE
#define	PCI_EHCI_VENDORID_VIA		0x1106

static device_probe_t ehci_pci_probe;
static device_attach_t ehci_pci_attach;
static device_detach_t ehci_pci_detach;
static usb_take_controller_t ehci_pci_take_controller;

static const char *
ehci_pci_match(device_t self)
{
	uint32_t device_id = pci_get_devid(self);

	switch (device_id) {
	case 0x523910b9:
		return "ALi M5239 USB 2.0 controller";

	case 0x10227463:
		return "AMD 8111 USB 2.0 controller";

	case 0x20951022:
		return ("AMD CS5536 (Geode) USB 2.0 controller");
	case 0x78081022:
		return ("AMD FCH USB 2.0 controller");

	case 0x43451002:
		return "ATI SB200 USB 2.0 controller";
	case 0x43731002:
		return "ATI SB400 USB 2.0 controller";
	case 0x43961002:
		return ("AMD SB7x0/SB8x0/SB9x0 USB 2.0 controller");

	case 0x0f348086:
		return ("Intel BayTrail USB 2.0 controller");
	case 0x1c268086:
		return ("Intel Cougar Point USB 2.0 controller");
	case 0x1c2d8086:
		return ("Intel Cougar Point USB 2.0 controller");
	case 0x1d268086:
		return ("Intel Patsburg USB 2.0 controller");
	case 0x1d2d8086:
		return ("Intel Patsburg USB 2.0 controller");
	case 0x1e268086:
		return ("Intel Panther Point USB 2.0 controller");
	case 0x1e2d8086:
		return ("Intel Panther Point USB 2.0 controller");
	case 0x1f2c8086:
		return ("Intel Avoton USB 2.0 controller");
	case 0x25ad8086:
		return "Intel 6300ESB USB 2.0 controller";
	case 0x24cd8086:
		return "Intel 82801DB/L/M (ICH4) USB 2.0 controller";
	case 0x24dd8086:
		return "Intel 82801EB/R (ICH5) USB 2.0 controller";
	case 0x265c8086:
		return "Intel 82801FB (ICH6) USB 2.0 controller";
	case 0x268c8086:
		return ("Intel 63XXESB USB 2.0 controller");
	case 0x27cc8086:
		return "Intel 82801GB/R (ICH7) USB 2.0 controller";
	case 0x28368086:
		return "Intel 82801H (ICH8) USB 2.0 controller USB2-A";
	case 0x283a8086:
		return "Intel 82801H (ICH8) USB 2.0 controller USB2-B";
	case 0x293a8086:
		return "Intel 82801I (ICH9) USB 2.0 controller";
	case 0x293c8086:
		return "Intel 82801I (ICH9) USB 2.0 controller";
	case 0x3a3a8086:
		return "Intel 82801JI (ICH10) USB 2.0 controller USB-A";
	case 0x3a3c8086:
		return "Intel 82801JI (ICH10) USB 2.0 controller USB-B";
	case 0x3b348086:
		return ("Intel PCH USB 2.0 controller USB-A");
	case 0x3b3c8086:
		return ("Intel PCH USB 2.0 controller USB-B");
	case 0x8c268086:
		return ("Intel Lynx Point USB 2.0 controller USB-A");
	case 0x8c2d8086:
		return ("Intel Lynx Point USB 2.0 controller USB-B");
	case 0x8ca68086:
		return ("Intel Wildcat Point USB 2.0 controller USB-A");
	case 0x8cad8086:
		return ("Intel Wildcat Point USB 2.0 controller USB-B");
	case 0x8d268086:
		return ("Intel Wellsburg USB 2.0 controller");
	case 0x8d2d8086:
		return ("Intel Wellsburg USB 2.0 controller");
	case 0x9c268086:
		return ("Intel Lynx Point LP USB 2.0 controller USB");

	case 0x00e01033:
		return ("NEC uPD 72010x USB 2.0 controller");

	case 0x006810de:
		return "NVIDIA nForce2 USB 2.0 controller";
	case 0x008810de:
		return "NVIDIA nForce2 Ultra 400 USB 2.0 controller";
	case 0x00d810de:
		return "NVIDIA nForce3 USB 2.0 controller";
	case 0x00e810de:
		return "NVIDIA nForce3 250 USB 2.0 controller";
	case 0x005b10de:
		return "NVIDIA nForce CK804 USB 2.0 controller";
	case 0x036d10de:
		return "NVIDIA nForce MCP55 USB 2.0 controller";
	case 0x03f210de:
		return "NVIDIA nForce MCP61 USB 2.0 controller";
	case 0x0aa610de:
		return "NVIDIA nForce MCP79 USB 2.0 controller";
	case 0x0aa910de:
		return "NVIDIA nForce MCP79 USB 2.0 controller";
	case 0x0aaa10de:
		return "NVIDIA nForce MCP79 USB 2.0 controller";

	case 0x15621131:
		return "Philips ISP156x USB 2.0 controller";

	case 0x70021039:
		return "SiS 968 USB 2.0 controller";

	case 0x31041106:
		return ("VIA VT6202 USB 2.0 controller");

	default:
		break;
	}

	if ((pci_get_class(self) == PCIC_SERIALBUS)
	    && (pci_get_subclass(self) == PCIS_SERIALBUS_USB)
	    && (pci_get_progif(self) == PCI_INTERFACE_EHCI)) {
		return ("EHCI (generic) USB 2.0 controller");
	}
	return (NULL);			/* dunno */
}

static int
ehci_pci_probe(device_t self)
{
	const char *desc = ehci_pci_match(self);

	if (desc) {
		device_set_desc(self, desc);
		return (BUS_PROBE_DEFAULT);
	} else {
		return (ENXIO);
	}
}

static void
ehci_pci_ati_quirk(device_t self, uint8_t is_sb700)
{
	device_t smbdev;
	uint32_t val;

	if (is_sb700) {
		/* Lookup SMBUS PCI device */
		smbdev = pci_find_device(PCI_EHCI_VENDORID_ATI, 0x4385);
		if (smbdev == NULL)
			return;
		val = pci_get_revid(smbdev);
		if (val != 0x3a && val != 0x3b)
			return;
	}

	/*
	 * Note: this bit is described as reserved in SB700
	 * Register Reference Guide.
	 */
	val = pci_read_config(self, 0x53, 1);
	if (!(val & 0x8)) {
		val |= 0x8;
		pci_write_config(self, 0x53, val, 1);
		device_printf(self, "AMD SB600/700 quirk applied\n");
	}
}

static void
ehci_pci_via_quirk(device_t self)
{
	uint32_t val;

	if ((pci_get_device(self) == 0x3104) && 
	    ((pci_get_revid(self) & 0xf0) == 0x60)) {
		/* Correct schedule sleep time to 10us */
		val = pci_read_config(self, 0x4b, 1);
		if (val & 0x20)
			return;
		val |= 0x20;
		pci_write_config(self, 0x4b, val, 1);
		device_printf(self, "VIA-quirk applied\n");
	}
}

static int
ehci_pci_attach(device_t self)
{
	ehci_softc_t *sc = device_get_softc(self);
	int err;
	int rid;

	/* initialise some bus fields */
	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(self), &ehci_iterate_hw_softc)) {
		return (ENOMEM);
	}

	pci_enable_busmaster(self);

	switch (pci_read_config(self, PCI_USBREV, 1) & PCI_USB_REV_MASK) {
	case PCI_USB_REV_PRE_1_0:
	case PCI_USB_REV_1_0:
	case PCI_USB_REV_1_1:
		/*
		 * NOTE: some EHCI USB controllers have the wrong USB
		 * revision number. It appears those controllers are
		 * fully compliant so we just ignore this value in
		 * some common cases.
		 */
		device_printf(self, "pre-2.0 USB revision (ignored)\n");
		/* fallthrough */
	case PCI_USB_REV_2_0:
		break;
	default:
		/* Quirk for Parallels Desktop 4.0 */
		device_printf(self, "USB revision is unknown. Assuming v2.0.\n");
		break;
	}

	rid = PCI_CBMEM;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(self, "Could not map memory\n");
		goto error;
	}
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	rid = 0;
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

	/*
	 * ehci_pci_match will never return NULL if ehci_pci_probe
	 * succeeded
	 */
	device_set_desc(sc->sc_bus.bdev, ehci_pci_match(self));
	switch (pci_get_vendor(self)) {
	case PCI_EHCI_VENDORID_ACERLABS:
		sprintf(sc->sc_vendor, "AcerLabs");
		break;
	case PCI_EHCI_VENDORID_AMD:
		sprintf(sc->sc_vendor, "AMD");
		break;
	case PCI_EHCI_VENDORID_APPLE:
		sprintf(sc->sc_vendor, "Apple");
		break;
	case PCI_EHCI_VENDORID_ATI:
		sprintf(sc->sc_vendor, "ATI");
		break;
	case PCI_EHCI_VENDORID_CMDTECH:
		sprintf(sc->sc_vendor, "CMDTECH");
		break;
	case PCI_EHCI_VENDORID_INTEL:
		sprintf(sc->sc_vendor, "Intel");
		break;
	case PCI_EHCI_VENDORID_NEC:
		sprintf(sc->sc_vendor, "NEC");
		break;
	case PCI_EHCI_VENDORID_OPTI:
		sprintf(sc->sc_vendor, "OPTi");
		break;
	case PCI_EHCI_VENDORID_PHILIPS:
		sprintf(sc->sc_vendor, "Philips");
		break;
	case PCI_EHCI_VENDORID_SIS:
		sprintf(sc->sc_vendor, "SiS");
		break;
	case PCI_EHCI_VENDORID_NVIDIA:
	case PCI_EHCI_VENDORID_NVIDIA2:
		sprintf(sc->sc_vendor, "nVidia");
		break;
	case PCI_EHCI_VENDORID_VIA:
		sprintf(sc->sc_vendor, "VIA");
		break;
	default:
		if (bootverbose)
			device_printf(self, "(New EHCI DeviceId=0x%08x)\n",
			    pci_get_devid(self));
		sprintf(sc->sc_vendor, "(0x%04x)", pci_get_vendor(self));
	}

#if (__FreeBSD_version >= 700031)
	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
#else
	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
#endif
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}
	ehci_pci_take_controller(self);

	/* Undocumented quirks taken from Linux */

	switch (pci_get_vendor(self)) {
	case PCI_EHCI_VENDORID_ATI:
		/* SB600 and SB700 EHCI quirk */
		switch (pci_get_device(self)) {
		case 0x4386:
			ehci_pci_ati_quirk(self, 0);
			break;
		case 0x4396:
			ehci_pci_ati_quirk(self, 1);
			break;
		default:
			break;
		}
		break;

	case PCI_EHCI_VENDORID_VIA:
		ehci_pci_via_quirk(self);
		break;

	default:
		break;
	}

	/* Dropped interrupts workaround */
	switch (pci_get_vendor(self)) {
	case PCI_EHCI_VENDORID_ATI:
	case PCI_EHCI_VENDORID_VIA:
		sc->sc_flags |= EHCI_SCFLG_LOSTINTRBUG;
		if (bootverbose)
			device_printf(self,
			    "Dropped interrupts workaround enabled\n");
		break;
	default:
		break;
	}

	/* Doorbell feature workaround */
	switch (pci_get_vendor(self)) {
	case PCI_EHCI_VENDORID_NVIDIA:
	case PCI_EHCI_VENDORID_NVIDIA2:
		sc->sc_flags |= EHCI_SCFLG_IAADBUG;
		if (bootverbose)
			device_printf(self,
			    "Doorbell workaround enabled\n");
		break;
	default:
		break;
	}

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
	ehci_pci_detach(self);
	return (ENXIO);
}

static int
ehci_pci_detach(device_t self)
{
	ehci_softc_t *sc = device_get_softc(self);

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	pci_disable_busmaster(self);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ehci_detach() after ehci_init()
		 */
		ehci_detach(sc);

		int err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);

		if (err)
			/* XXX or should we panic? */
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
		sc->sc_intr_hdl = NULL;
	}
	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, PCI_CBMEM,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}
	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);

	return (0);
}

static int
ehci_pci_take_controller(device_t self)
{
	ehci_softc_t *sc = device_get_softc(self);
	uint32_t cparams;
	uint32_t eec;
	uint16_t to;
	uint8_t eecp;
	uint8_t bios_sem;

	cparams = EREAD4(sc, EHCI_HCCPARAMS);

	/* Synchronise with the BIOS if it owns the controller. */
	for (eecp = EHCI_HCC_EECP(cparams); eecp != 0;
	    eecp = EHCI_EECP_NEXT(eec)) {
		eec = pci_read_config(self, eecp, 4);
		if (EHCI_EECP_ID(eec) != EHCI_EC_LEGSUP) {
			continue;
		}
		bios_sem = pci_read_config(self, eecp +
		    EHCI_LEGSUP_BIOS_SEM, 1);
		if (bios_sem == 0) {
			continue;
		}
		device_printf(sc->sc_bus.bdev, "waiting for BIOS "
		    "to give up control\n");
		pci_write_config(self, eecp +
		    EHCI_LEGSUP_OS_SEM, 1, 1);
		to = 500;
		while (1) {
			bios_sem = pci_read_config(self, eecp +
			    EHCI_LEGSUP_BIOS_SEM, 1);
			if (bios_sem == 0)
				break;

			if (--to == 0) {
				device_printf(sc->sc_bus.bdev,
				    "timed out waiting for BIOS\n");
				break;
			}
			usb_pause_mtx(NULL, hz / 100);	/* wait 10ms */
		}
	}
	return (0);
}

static device_method_t ehci_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ehci_pci_probe),
	DEVMETHOD(device_attach, ehci_pci_attach),
	DEVMETHOD(device_detach, ehci_pci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	DEVMETHOD(usb_take_controller, ehci_pci_take_controller),

	DEVMETHOD_END
};

static driver_t ehci_driver = {
	.name = "ehci",
	.methods = ehci_pci_methods,
	.size = sizeof(struct ehci_softc),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci, pci, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(ehci, usb, 1, 1, 1);
