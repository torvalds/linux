/*
 * Copyright 2015 Andrew Turner.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <dev/usb/controller/dwc_otg.h>
#include <dev/usb/controller/dwc_otg_fdt.h>

static device_probe_t hisi_dwc_otg_probe;
static device_attach_t hisi_dwc_otg_attach;

static int
hisi_dwc_otg_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "huawei,hisi-usb"))
		return (ENXIO);

	device_set_desc(dev, "DWC OTG 2.0 integrated USB controller (hisilicon)");

	return (BUS_PROBE_VENDOR);
}

static int
hisi_dwc_otg_attach(device_t dev)
{
	struct dwc_otg_fdt_softc *sc;

	/* Set the default to host mode. */
	/* TODO: Use vbus to detect this. */
	sc = device_get_softc(dev);
	sc->sc_otg.sc_mode = DWC_MODE_HOST;

	return (dwc_otg_attach(dev));
}

static device_method_t hisi_dwc_otg_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, hisi_dwc_otg_probe),
	DEVMETHOD(device_attach, hisi_dwc_otg_attach),

	DEVMETHOD_END
};

static devclass_t hisi_dwc_otg_devclass;

DEFINE_CLASS_1(hisi_dwcotg, hisi_dwc_otg_driver, hisi_dwc_otg_methods,
    sizeof(struct dwc_otg_fdt_softc), dwc_otg_driver);
DRIVER_MODULE(hisi_dwcotg, simplebus, hisi_dwc_otg_driver,
    hisi_dwc_otg_devclass, 0, 0);
MODULE_DEPEND(hisi_dwcotg, usb, 1, 1, 1);
