/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Sam Leffler.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * AR71XX attachment driver for the USB Enhanced Host Controller.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

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

#include <mips/atheros/ar71xx_setup.h>
#include <mips/atheros/ar71xxreg.h> /* for stuff in ar71xx_cpudef.h */
#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_bus_space_reversed.h>

#define EHCI_HC_DEVSTR		"AR71XX Integrated USB 2.0 controller"

#define	EHCI_USBMODE		0x68	/* USB Device mode register */
#define	EHCI_UM_CM		0x00000003	/* R/WO Controller Mode */
#define	EHCI_UM_CM_HOST		0x3	/* Host Controller */

struct ar71xx_ehci_softc {
	ehci_softc_t		base;	/* storage for EHCI code */
};

static device_attach_t ar71xx_ehci_attach;
static device_detach_t ar71xx_ehci_detach;

bs_r_1_proto(reversed);
bs_w_1_proto(reversed);

static void
ar71xx_ehci_post_reset(struct ehci_softc *ehci_softc)
{
	uint32_t usbmode;

	/* Force HOST mode */
	usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_NOLPM);
	usbmode &= ~EHCI_UM_CM;
	usbmode |= EHCI_UM_CM_HOST;
	EOWRITE4(ehci_softc, EHCI_USBMODE_NOLPM, usbmode);
}

static int
ar71xx_ehci_probe(device_t self)
{

	device_set_desc(self, EHCI_HC_DEVSTR);

	return (BUS_PROBE_NOWILDCARD);
}

static void
ar71xx_ehci_intr(void *arg)
{

	/* XXX TODO: should really see if this was our interrupt.. */
	ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_USB);
	ehci_interrupt(arg);
}

static int
ar71xx_ehci_attach(device_t self)
{
	struct ar71xx_ehci_softc *isc = device_get_softc(self);
	ehci_softc_t *sc = &isc->base;
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

	sc->sc_bus.usbrev = USB_REV_2_0;

	/* NB: hints fix the memory location and irq */

	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(self, "Could not map memory\n");
		goto error;
	}

	/*
	 * Craft special resource for bus space ops that handle
	 * byte-alignment of non-word addresses.  
	 */
	sc->sc_io_tag = ar71xx_bus_space_reversed;
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
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

	sprintf(sc->sc_vendor, "Atheros");

	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, ar71xx_ehci_intr, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}

	/*
	 * Arrange to force Host mode, select big-endian byte alignment,
	 * and arrange to not terminate reset operations (the adapter
	 * will ignore it if we do but might as well save a reg write).
	 * Also, the controller has an embedded Transaction Translator
	 * which means port speed must be read from the Port Status
	 * register following a port enable.
	 */
	sc->sc_flags = 0;
	sc->sc_vendor_post_reset = ar71xx_ehci_post_reset;

	switch (ar71xx_soc) {
		case AR71XX_SOC_AR7241:
		case AR71XX_SOC_AR7242:
		case AR71XX_SOC_AR9130:
		case AR71XX_SOC_AR9132:
		case AR71XX_SOC_AR9330:
		case AR71XX_SOC_AR9331:
		case AR71XX_SOC_AR9341:
		case AR71XX_SOC_AR9342:
		case AR71XX_SOC_AR9344:
		case AR71XX_SOC_QCA9533:
		case AR71XX_SOC_QCA9533_V2:
		case AR71XX_SOC_QCA9556:
		case AR71XX_SOC_QCA9558:
			sc->sc_flags |= EHCI_SCFLG_TT | EHCI_SCFLG_NORESTERM;
			sc->sc_vendor_get_port_speed =
			    ehci_get_port_speed_portsc;
			break;
		default:
			/* fallthrough */
			break;
	}

	/*
	 * ehci_reset() needs the correct offset to access the host controller
	 * registers. The AR724x/AR913x offsets aren't 0.
	*/
	sc->sc_offs = EHCI_CAPLENGTH(EREAD4(sc, EHCI_CAPLEN_HCIVERSION));

	(void) ehci_reset(sc);

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
	ar71xx_ehci_detach(self);
	return (ENXIO);
}

static int
ar71xx_ehci_detach(device_t self)
{
	struct ar71xx_ehci_softc *isc = device_get_softc(self);
	ehci_softc_t *sc = &isc->base;
	int err;

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

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

 	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0, sc->sc_irq_res);
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

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ar71xx_ehci_probe),
	DEVMETHOD(device_attach, ar71xx_ehci_attach),
	DEVMETHOD(device_detach, ar71xx_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t ehci_driver = {
	.name = "ehci",
	.methods = ehci_methods,
	.size = sizeof(struct ar71xx_ehci_softc),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci, nexus, ehci_driver, ehci_devclass, 0, 0);
DRIVER_MODULE(ehci, apb, ehci_driver, ehci_devclass, 0, 0);

MODULE_DEPEND(ehci, usb, 1, 1, 1);
