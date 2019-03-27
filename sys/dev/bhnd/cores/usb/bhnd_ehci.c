/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * Copyright (c) 2010, Aleksandr Rybalko <ray@ddteam.net>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * BHND attachment driver for the USB Enhanced Host Controller.
 * Ported from ZRouter with insignificant adaptations for FreeBSD11.
 */

#include "opt_bus.h"

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/linker_set.h>
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
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#include <dev/bhnd/bhnd.h>

#define	EHCI_HC_DEVSTR		"Broadcom EHCI"

#define	USB_BRIDGE_INTR_CAUSE	0x210
#define	USB_BRIDGE_INTR_MASK	0x214

static device_attach_t	bhnd_ehci_attach;
static device_detach_t	bhnd_ehci_detach;

static int		bhnd_ehci_probe(device_t self);
static void		bhnd_ehci_post_reset(struct ehci_softc *ehci_softc);

static int
bhnd_ehci_probe(device_t self)
{

	device_set_desc(self, EHCI_HC_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static void
bhnd_ehci_post_reset(struct ehci_softc *ehci_softc)
{
        uint32_t	usbmode;

        /* Force HOST mode */
        usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_NOLPM);
        usbmode &= ~EHCI_UM_CM;
        usbmode |= EHCI_UM_CM_HOST;
        EOWRITE4(ehci_softc, EHCI_USBMODE_NOLPM, usbmode);
}

static int
bhnd_ehci_attach(device_t self)
{
	ehci_softc_t	*sc;
	int		 err;
	int		 rid;

	sc = device_get_softc(self);
	/* initialise some bus fields */
	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.usbrev = USB_REV_2_0;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if ((err = usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(self),
	    &ehci_iterate_hw_softc)) != 0) {
		BHND_ERROR_DEV(self, "can't allocate DMA memory: %d", err);
		return (ENOMEM);
	}

	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (!sc->sc_io_res) {
		BHND_ERROR_DEV(self, "Could not map memory");
		goto error;
	}
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->sc_irq_res == NULL) {
		BHND_ERROR_DEV(self, "Could not allocate error irq");
		bhnd_ehci_detach(self);
		return (ENXIO);
	}

	sc->sc_bus.bdev = device_add_child(self, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		BHND_ERROR_DEV(self, "Could not add USB device");
		goto error;
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, EHCI_HC_DEVSTR);

 	sprintf(sc->sc_vendor, "Broadcom");

	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		BHND_ERROR_DEV(self, "Could not setup irq, %d", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}

	sc->sc_flags |= EHCI_SCFLG_LOSTINTRBUG;
	sc->sc_vendor_post_reset = bhnd_ehci_post_reset;

	err = ehci_init(sc);
	if (!err) {
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		BHND_ERROR_DEV(self, "USB init failed err=%d", err);
		goto error;
	}
	return (0);

error:
	bhnd_ehci_detach(self);
	return (ENXIO);
}

static int
bhnd_ehci_detach(device_t self)
{
	ehci_softc_t	*sc;
	int		 err;

	sc = device_get_softc(self);

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	/*
	 * disable interrupts that might have been switched on in ehci_init
	 */
#ifdef notyet
	if (sc->sc_io_res) {
		EWRITE4(sc, EHCI_USBINTR, 0);
		EWRITE4(sc, USB_BRIDGE_INTR_MASK, 0);
	}
#endif
 	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ehci_detach() after ehci_init()
		 */
		ehci_detach(sc);

		err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);

		if (err)
			/* XXX or should we panic? */
			BHND_ERROR_DEV(self, "Could not tear down irq, %d", err);

		sc->sc_intr_hdl = NULL;
	}
 	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, 0, sc->sc_io_res);
		sc->sc_io_res = NULL;
	}
	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);

	return (0);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bhnd_ehci_probe),
	DEVMETHOD(device_attach,	bhnd_ehci_attach),
	DEVMETHOD(device_detach,	bhnd_ehci_detach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),

	{0, 0}
};

static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(ehci_softc_t),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci, bhnd_usb, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(ehci, usb, 1, 1, 1);
