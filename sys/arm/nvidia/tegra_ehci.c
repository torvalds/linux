/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * EHCI driver for Tegra SoCs.
 */
#include "opt_bus.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#include "usbdevs.h"

#define	TEGRA_EHCI_REG_OFF	0x100
#define	TEGRA_EHCI_REG_SIZE	0x100

/* Compatible devices. */
#define	TEGRA124_EHCI		1
static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-ehci",	(uintptr_t)TEGRA124_EHCI},
	{NULL,		 	0},
};

struct tegra_ehci_softc {
	ehci_softc_t	ehci_softc;
	device_t	dev;
	struct resource	*ehci_mem_res;	/* EHCI core regs. */
	struct resource	*ehci_irq_res;	/* EHCI core IRQ. */
	int		usb_alloc_called;
	clk_t		clk;
	phy_t 		phy;
	hwreset_t 	reset;
};

static void
tegra_ehci_post_reset(struct ehci_softc *ehci_softc)
{
	uint32_t usbmode;

	/* Force HOST mode. */
	usbmode = EOREAD4(ehci_softc, EHCI_USBMODE_LPM);
	usbmode &= ~EHCI_UM_CM;
	usbmode |= EHCI_UM_CM_HOST;
	device_printf(ehci_softc->sc_bus.bdev, "set host controller mode\n");
	EOWRITE4(ehci_softc, EHCI_USBMODE_LPM, usbmode);
}

static int
tegra_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Nvidia Tegra EHCI controller");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
tegra_ehci_detach(device_t dev)
{
	struct tegra_ehci_softc *sc;
	ehci_softc_t *esc;

	sc = device_get_softc(dev);

	esc = &sc->ehci_softc;
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (esc->sc_bus.bdev != NULL)
		device_delete_child(dev, esc->sc_bus.bdev);
	if (esc->sc_flags & EHCI_SCFLG_DONEINIT)
		ehci_detach(esc);
	if (esc->sc_intr_hdl != NULL)
		bus_teardown_intr(dev, esc->sc_irq_res,
		    esc->sc_intr_hdl);
	if (sc->ehci_irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0,
		    sc->ehci_irq_res);
	if (sc->ehci_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0,
		    sc->ehci_mem_res);
	if (sc->usb_alloc_called)
		usb_bus_mem_free_all(&esc->sc_bus, &ehci_iterate_hw_softc);

	/* During module unload there are lots of children leftover. */
	device_delete_children(dev);

	return (0);
}

static int
tegra_ehci_attach(device_t dev)
{
	struct tegra_ehci_softc *sc;
	ehci_softc_t *esc;
	int rv, rid;
	uint64_t freq;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);
	esc = &sc->ehci_softc;

	/* Allocate resources. */
	rid = 0;
	sc->ehci_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->ehci_mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		rv = ENXIO;
		goto out;
	}

	rid = 0;
	sc->ehci_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->ehci_irq_res == NULL) {
		device_printf(dev, "Cannot allocate IRQ resources\n");
		rv = ENXIO;
		goto out;
	}

	rv = hwreset_get_by_ofw_name(dev, 0, "usb", &sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot get reset\n");
		rv = ENXIO;
		goto out;
	}

	rv = phy_get_by_ofw_property(sc->dev, 0, "nvidia,phy", &sc->phy);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'nvidia,phy' phy\n");
		rv = ENXIO;
		goto out;
	}

	rv = clk_get_by_ofw_index(sc->dev, 0, 0, &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get clock\n");
		goto out;
	}

	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock\n");
		goto out;
	}

	freq = 0;
	rv = clk_get_freq(sc->clk, &freq);
	if (rv != 0) {
		device_printf(dev, "Cannot get clock frequency\n");
		goto out;
	}

	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot clear reset: %d\n", rv);
		rv = ENXIO;
		goto out;
	}

	rv = phy_enable(sc->phy);
	if (rv != 0) {
		device_printf(dev, "Cannot enable phy: %d\n", rv);
		goto out;
	}

	/* Fill data for EHCI driver. */
	esc->sc_vendor_get_port_speed = ehci_get_port_speed_hostc;
	esc->sc_vendor_post_reset = tegra_ehci_post_reset;
	esc->sc_io_tag = rman_get_bustag(sc->ehci_mem_res);
	esc->sc_bus.parent = dev;
	esc->sc_bus.devices = esc->sc_devices;
	esc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	esc->sc_bus.dma_bits = 32;

	/* Allocate all DMA memory. */
	rv = usb_bus_mem_alloc_all(&esc->sc_bus, USB_GET_DMA_TAG(dev),
	    &ehci_iterate_hw_softc);
	sc->usb_alloc_called = 1;
	if (rv != 0) {
		device_printf(dev, "usb_bus_mem_alloc_all() failed\n");
		rv = ENOMEM;
		goto out;
	}

	/*
	 * Set handle to USB related registers subregion used by
	 * generic EHCI driver.
	 */
	rv = bus_space_subregion(esc->sc_io_tag,
	    rman_get_bushandle(sc->ehci_mem_res),
	    TEGRA_EHCI_REG_OFF, TEGRA_EHCI_REG_SIZE, &esc->sc_io_hdl);
	if (rv != 0) {
		device_printf(dev, "Could not create USB memory subregion\n");
		rv = ENXIO;
		goto out;
	}

	/* Setup interrupt handler. */
	rv = bus_setup_intr(dev, sc->ehci_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, esc, &esc->sc_intr_hdl);
	if (rv != 0) {
		device_printf(dev, "Could not setup IRQ\n");
		goto out;
	}

	/* Add USB bus device. */
	esc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (esc->sc_bus.bdev == NULL) {
		device_printf(dev, "Could not add USB device\n");
		goto out;
	}
	device_set_ivars(esc->sc_bus.bdev, &esc->sc_bus);

	esc->sc_id_vendor = USB_VENDOR_FREESCALE;
	strlcpy(esc->sc_vendor, "Nvidia", sizeof(esc->sc_vendor));

	/* Set flags that affect ehci_init() behavior. */
	esc->sc_flags |= EHCI_SCFLG_TT;
	esc->sc_flags |= EHCI_SCFLG_NORESTERM;
	rv = ehci_init(esc);
	if (rv != 0) {
		device_printf(dev, "USB init failed: %d\n",
		    rv);
		goto out;
	}
	esc->sc_flags |= EHCI_SCFLG_DONEINIT;

	/* Probe the bus. */
	rv = device_probe_and_attach(esc->sc_bus.bdev);
	if (rv != 0) {
		device_printf(dev,
		    "device_probe_and_attach() failed\n");
		goto out;
	}
	return (0);

out:
	tegra_ehci_detach(dev);
	return (rv);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, tegra_ehci_probe),
	DEVMETHOD(device_attach, tegra_ehci_attach),
	DEVMETHOD(device_detach, tegra_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	DEVMETHOD_END
};

static devclass_t ehci_devclass;
static DEFINE_CLASS_0(ehci, ehci_driver, ehci_methods,
    sizeof(struct tegra_ehci_softc));
DRIVER_MODULE(tegra_ehci, simplebus, ehci_driver, ehci_devclass, NULL, NULL);
MODULE_DEPEND(tegra_ehci, usb, 1, 1, 1);
