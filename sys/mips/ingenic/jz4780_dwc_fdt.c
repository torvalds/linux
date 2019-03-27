/*
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>.
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

#include <dev/extres/clk/clk.h>

#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <dev/usb/controller/dwc_otg.h>
#include <dev/usb/controller/dwc_otg_fdt.h>

#include <mips/ingenic/jz4780_clock.h>
#include <mips/ingenic/jz4780_regs.h>

static device_probe_t jz4780_dwc_otg_probe;
static device_attach_t jz4780_dwc_otg_attach;
static device_detach_t jz4780_dwc_otg_detach;

struct jz4780_dwc_otg_softc {
	struct dwc_otg_fdt_softc base;	/* storage for DWC OTG code */
	clk_t			phy_clk;
	clk_t			otg_clk;
};

static int
jz4780_dwc_otg_clk_enable(device_t dev)
{
	struct jz4780_dwc_otg_softc *sc;
	int err;

	sc = device_get_softc(dev);

	/* Configure and enable phy clock */
	err = clk_get_by_ofw_name(dev, 0, "otg_phy", &sc->phy_clk);
	if (err != 0) {
		device_printf(dev, "unable to lookup %s clock\n", "otg_phy");
		return (err);
	}
	err = clk_set_freq(sc->phy_clk, 48000000, 0);
	if (err != 0) {
		device_printf(dev, "unable to set %s clock to 48 kHZ\n",
		    "otg_phy");
		return (err);
	}
	err = clk_enable(sc->phy_clk);
	if (err != 0) {
		device_printf(dev, "unable to enable %s clock\n", "otg_phy");
		return (err);
	}

	/* Configure and enable otg1 clock */
	err = clk_get_by_ofw_name(dev, 0, "otg1", &sc->otg_clk);
	if (err != 0) {
		device_printf(dev, "unable to lookup %s clock\n", "otg1");
		return (err);
	}
	err = clk_enable(sc->phy_clk);
	if (err != 0) {
		device_printf(dev, "unable to enable %s clock\n", "otg1");
		return (err);
	}

	return (0);
}

static int
jz4780_dwc_otg_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-otg"))
		return (ENXIO);

	device_set_desc(dev, "DWC OTG 2.0 integrated USB controller (jz4780)");

	return (BUS_PROBE_VENDOR);
}

static int
jz4780_dwc_otg_attach(device_t dev)
{
	struct jz4780_dwc_otg_softc *sc;
	struct resource *res;
	int err, rid;

	sc = device_get_softc(dev);

	err = jz4780_dwc_otg_clk_enable(dev);
	if (err != 0)
		goto fail;

	err = jz4780_otg_enable();
	if (err != 0) {
		device_printf(dev, "CGU failed to enable OTG\n");
		goto fail;
	}

	/* Voodoo: Switch off VBUS overcurrent detection in OTG PHY */
	res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res != NULL) {
		uint32_t reg;

		reg = bus_read_4(res, JZ_DWC2_GUSBCFG);
		reg |= 0xc;
		bus_write_4(res, JZ_DWC2_GUSBCFG, reg);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
	}

	sc->base.sc_otg.sc_phy_type = DWC_OTG_PHY_UTMI;
	sc->base.sc_otg.sc_phy_bits = 16;

	err = dwc_otg_attach(dev);
	if (err != 0)
		goto fail;

	return (0);
fail:
	if (sc->otg_clk)
		clk_release(sc->otg_clk);
	if (sc->phy_clk)
		clk_release(sc->phy_clk);
	return (err);
}

static int
jz4780_dwc_otg_detach(device_t dev)
{
	struct jz4780_dwc_otg_softc *sc;
	int err;

	err = dwc_otg_detach(dev);
	if (err != 0)
		return (err);

	sc = device_get_softc(dev);
	if (sc->otg_clk)
		clk_release(sc->otg_clk);
	if (sc->phy_clk)
		clk_release(sc->phy_clk);
	return (0);
}

static device_method_t jz4780_dwc_otg_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, jz4780_dwc_otg_probe),
	DEVMETHOD(device_attach, jz4780_dwc_otg_attach),
	DEVMETHOD(device_detach, jz4780_dwc_otg_detach),

	DEVMETHOD_END
};

static devclass_t jz4780_dwc_otg_devclass;

DEFINE_CLASS_1(jzotg, jz4780_dwc_otg_driver, jz4780_dwc_otg_methods,
    sizeof(struct jz4780_dwc_otg_softc), dwc_otg_driver);
DRIVER_MODULE(jzotg, simplebus, jz4780_dwc_otg_driver,
    jz4780_dwc_otg_devclass, 0, 0);
MODULE_DEPEND(jzotg, usb, 1, 1, 1);
