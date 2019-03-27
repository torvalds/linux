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

#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-usb",	1},
	{"brcm,bcm2835-usb",		1},
	{"brcm,bcm2708-usb",		1},
	{NULL,				0}
};

static device_probe_t bcm283x_dwc_otg_probe;
static device_attach_t bcm283x_dwc_otg_attach;

static int
bcm283x_dwc_otg_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "DWC OTG 2.0 integrated USB controller (bcm283x)");

	return (BUS_PROBE_VENDOR);
}

static int
bcm283x_dwc_otg_attach(device_t dev)
{
	int err;

	err = bcm2835_mbox_set_power_state(BCM2835_MBOX_POWER_ID_USB_HCD, TRUE);
	if (err)
		device_printf(dev, "failed to set power state, err=%d\n", err);

	return (dwc_otg_attach(dev));
}

static device_method_t bcm283x_dwc_otg_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, bcm283x_dwc_otg_probe),
	DEVMETHOD(device_attach, bcm283x_dwc_otg_attach),

	DEVMETHOD_END
};

static devclass_t bcm283x_dwc_otg_devclass;

DEFINE_CLASS_1(bcm283x_dwcotg, bcm283x_dwc_otg_driver, bcm283x_dwc_otg_methods,
    sizeof(struct dwc_otg_fdt_softc), dwc_otg_driver);
DRIVER_MODULE(bcm283x_dwcotg, simplebus, bcm283x_dwc_otg_driver,
    bcm283x_dwc_otg_devclass, 0, 0);
MODULE_DEPEND(bcm283x_dwcotg, usb, 1, 1, 1);
