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
 * USB Open Host Controller driver.
 *
 * OHCI spec: http://www.intel.com/design/usb/ohci11d.pdf
 */

/* The low level controller code for OHCI has been split into
 * PCI probes and OHCI specific code. This was done to facilitate the
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
#include <dev/usb/controller/ohci.h>
#include <dev/usb/controller/ohcireg.h>
#include "usb_if.h"

#define	PCI_OHCI_VENDORID_ACERLABS	0x10b9
#define	PCI_OHCI_VENDORID_AMD		0x1022
#define	PCI_OHCI_VENDORID_APPLE		0x106b
#define	PCI_OHCI_VENDORID_ATI		0x1002
#define	PCI_OHCI_VENDORID_CMDTECH	0x1095
#define	PCI_OHCI_VENDORID_NEC		0x1033
#define	PCI_OHCI_VENDORID_NVIDIA	0x12D2
#define	PCI_OHCI_VENDORID_NVIDIA2	0x10DE
#define	PCI_OHCI_VENDORID_OPTI		0x1045
#define	PCI_OHCI_VENDORID_SIS		0x1039
#define	PCI_OHCI_VENDORID_SUN		0x108e

#define	PCI_OHCI_BASE_REG	0x10

static device_probe_t ohci_pci_probe;
static device_attach_t ohci_pci_attach;
static device_detach_t ohci_pci_detach;
static usb_take_controller_t ohci_pci_take_controller;

static int
ohci_pci_take_controller(device_t self)
{
	uint32_t reg;
	uint32_t int_line;

	if (pci_get_powerstate(self) != PCI_POWERSTATE_D0) {
		device_printf(self, "chip is in D%d mode "
		    "-- setting to D0\n", pci_get_powerstate(self));
		reg = pci_read_config(self, PCI_CBMEM, 4);
		int_line = pci_read_config(self, PCIR_INTLINE, 4);
		pci_set_powerstate(self, PCI_POWERSTATE_D0);
		pci_write_config(self, PCI_CBMEM, reg, 4);
		pci_write_config(self, PCIR_INTLINE, int_line, 4);
	}
	return (0);
}

static const char *
ohci_pci_match(device_t self)
{
	uint32_t device_id = pci_get_devid(self);

	switch (device_id) {
	case 0x523710b9:
		return ("AcerLabs M5237 (Aladdin-V) USB controller");

	case 0x740c1022:
		return ("AMD-756 USB Controller");
	case 0x74141022:
		return ("AMD-766 USB Controller");
	case 0x78071022:
		return ("AMD FCH USB Controller");

	case 0x43741002:
		return "ATI SB400 USB Controller";
	case 0x43751002:
		return "ATI SB400 USB Controller";
	case 0x43971002:
		return ("AMD SB7x0/SB8x0/SB9x0 USB controller");
	case 0x43981002:
		return ("AMD SB7x0/SB8x0/SB9x0 USB controller");
	case 0x43991002:
		return ("AMD SB7x0/SB8x0/SB9x0 USB controller");

	case 0x06701095:
		return ("CMD Tech 670 (USB0670) USB controller");

	case 0x06731095:
		return ("CMD Tech 673 (USB0673) USB controller");

	case 0xc8611045:
		return ("OPTi 82C861 (FireLink) USB controller");

	case 0x00351033:
		return ("NEC uPD 9210 USB controller");

	case 0x00d710de:
		return ("nVidia nForce3 USB Controller");

	case 0x005a10de:
		return ("nVidia nForce CK804 USB Controller");
	case 0x036c10de:
		return ("nVidia nForce MCP55 USB Controller");
	case 0x03f110de:
		return ("nVidia nForce MCP61 USB Controller");
	case 0x0aa510de:
		return ("nVidia nForce MCP79 USB Controller");
	case 0x0aa710de:
		return ("nVidia nForce MCP79 USB Controller");
	case 0x0aa810de:
		return ("nVidia nForce MCP79 USB Controller");

	case 0x70011039:
		return ("SiS 5571 USB controller");

	case 0x1103108e:
		return "Sun PCIO-2 USB controller";

	case 0x0019106b:
		return ("Apple KeyLargo USB controller");
	case 0x003f106b:
		return ("Apple KeyLargo/Intrepid USB controller");

	default:
		break;
	}
	if ((pci_get_class(self) == PCIC_SERIALBUS) &&
	    (pci_get_subclass(self) == PCIS_SERIALBUS_USB) &&
	    (pci_get_progif(self) == PCI_INTERFACE_OHCI)) {
		return ("OHCI (generic) USB controller");
	}
	return (NULL);
}

static int
ohci_pci_probe(device_t self)
{
	const char *desc = ohci_pci_match(self);

	if (desc) {
		device_set_desc(self, desc);
		return (0);
	} else {
		return (ENXIO);
	}
}

static int
ohci_pci_attach(device_t self)
{
	ohci_softc_t *sc = device_get_softc(self);
	int rid;
	int err;

	/* initialise some bus fields */
	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = OHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(self),
	    &ohci_iterate_hw_softc)) {
		return (ENOMEM);
	}
	sc->sc_dev = self;

	pci_enable_busmaster(self);

	/*
	 * Some Sun PCIO-2 USB controllers have their intpin register
	 * bogusly set to 0, although it should be 4.  Correct that.
	 */
	if (pci_get_devid(self) == 0x1103108e && pci_get_intpin(self) == 0)
		pci_set_intpin(self, 4);

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
	 * ohci_pci_match will never return NULL if ohci_pci_probe
	 * succeeded
	 */
	device_set_desc(sc->sc_bus.bdev, ohci_pci_match(self));
	switch (pci_get_vendor(self)) {
	case PCI_OHCI_VENDORID_ACERLABS:
		sprintf(sc->sc_vendor, "AcerLabs");
		break;
	case PCI_OHCI_VENDORID_AMD:
		sprintf(sc->sc_vendor, "AMD");
		break;
	case PCI_OHCI_VENDORID_APPLE:
		sprintf(sc->sc_vendor, "Apple");
		break;
	case PCI_OHCI_VENDORID_ATI:
		sprintf(sc->sc_vendor, "ATI");
		break;
	case PCI_OHCI_VENDORID_CMDTECH:
		sprintf(sc->sc_vendor, "CMDTECH");
		break;
	case PCI_OHCI_VENDORID_NEC:
		sprintf(sc->sc_vendor, "NEC");
		break;
	case PCI_OHCI_VENDORID_NVIDIA:
	case PCI_OHCI_VENDORID_NVIDIA2:
		sprintf(sc->sc_vendor, "nVidia");
		break;
	case PCI_OHCI_VENDORID_OPTI:
		sprintf(sc->sc_vendor, "OPTi");
		break;
	case PCI_OHCI_VENDORID_SIS:
		sprintf(sc->sc_vendor, "SiS");
		break;
	case PCI_OHCI_VENDORID_SUN:
		sprintf(sc->sc_vendor, "SUN");
		break;
	default:
		if (bootverbose) {
			device_printf(self, "(New OHCI DeviceId=0x%08x)\n",
			    pci_get_devid(self));
		}
		sprintf(sc->sc_vendor, "(0x%04x)", pci_get_vendor(self));
	}

	/* sc->sc_bus.usbrev; set by ohci_init() */

#if (__FreeBSD_version >= 700031)
	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ohci_interrupt, sc, &sc->sc_intr_hdl);
#else
	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    (driver_intr_t *)ohci_interrupt, sc, &sc->sc_intr_hdl);
#endif
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}
	err = ohci_init(sc);
	if (!err) {
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		device_printf(self, "USB init failed\n");
		goto error;
	}
	return (0);

error:
	ohci_pci_detach(self);
	return (ENXIO);
}

static int
ohci_pci_detach(device_t self)
{
	ohci_softc_t *sc = device_get_softc(self);

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	pci_disable_busmaster(self);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ohci_detach() after ohci_init()
		 */
		ohci_detach(sc);

		int err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);

		if (err) {
			/* XXX or should we panic? */
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
		}
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
	usb_bus_mem_free_all(&sc->sc_bus, &ohci_iterate_hw_softc);

	return (0);
}

static device_method_t ohci_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ohci_pci_probe),
	DEVMETHOD(device_attach, ohci_pci_attach),
	DEVMETHOD(device_detach, ohci_pci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	DEVMETHOD(usb_take_controller, ohci_pci_take_controller),

	DEVMETHOD_END
};

static driver_t ohci_driver = {
	.name = "ohci",
	.methods = ohci_pci_methods,
	.size = sizeof(struct ohci_softc),
};

static devclass_t ohci_devclass;

DRIVER_MODULE(ohci, pci, ohci_driver, ohci_devclass, 0, 0);
MODULE_DEPEND(ohci, usb, 1, 1, 1);
