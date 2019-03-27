/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>.
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
 * JZ4780 attachment driver for the USB Enhanced Host Controller.
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
#include <sys/gpio.h>

#include <dev/extres/clk/clk.h>

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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>

#include <mips/ingenic/jz4780_clock.h>
#include <mips/ingenic/jz4780_regs.h>

#define EHCI_HC_DEVSTR		"Ingenic JZ4780 EHCI"

struct jz4780_ehci_softc {
	ehci_softc_t		base;	/* storage for EHCI code */
	clk_t			clk;
	struct gpiobus_pin *gpio_vbus;
};

static device_probe_t jz4780_ehci_probe;
static device_attach_t jz4780_ehci_attach;
static device_detach_t jz4780_ehci_detach;

static int
jz4780_ehci_vbus_gpio_enable(device_t dev)
{
	struct gpiobus_pin *gpio_vbus;
	struct jz4780_ehci_softc *sc;
	int err;

	sc = device_get_softc(dev);

	err = ofw_gpiobus_parse_gpios(dev, "ingenic,vbus-gpio", &gpio_vbus);
	/*
	 * The pin can ne already mapped by other device. Optimistically
	 * surge ahead.
	 */
	if (err <= 0)
		return (0);

	sc->gpio_vbus = gpio_vbus;
	if (err > 1) {
		device_printf(dev, "too many vbus gpios\n");
		return (ENXIO);
	}

	if (sc->gpio_vbus != NULL) {
		err = GPIO_PIN_SETFLAGS(sc->gpio_vbus->dev, sc->gpio_vbus->pin,
		    GPIO_PIN_OUTPUT);
		if (err != 0) {
			device_printf(dev, "Cannot configure GPIO pin %d on %s\n",
			    sc->gpio_vbus->pin, device_get_nameunit(sc->gpio_vbus->dev));
			return (err);
		}

		err = GPIO_PIN_SET(sc->gpio_vbus->dev, sc->gpio_vbus->pin, 1);
		if (err != 0) {
			device_printf(dev, "Cannot configure GPIO pin %d on %s\n",
			    sc->gpio_vbus->pin, device_get_nameunit(sc->gpio_vbus->dev));
			return (err);
		}
	}
	return (0);
}

static int
jz4780_ehci_clk_enable(device_t dev)
{
	struct jz4780_ehci_softc *sc;
	int err;

	sc = device_get_softc(dev);

	err = clk_get_by_ofw_index(dev, 0, 0, &sc->clk);
	if (err != 0) {
		device_printf(dev, "unable to lookup device clock\n");
		return (err);
	}
	err = clk_enable(sc->clk);
	if (err != 0) {
		device_printf(dev, "unable to enable device clock\n");
		return (err);
	}
	err = clk_set_freq(sc->clk, 48000000, 0);
	if (err != 0) {
		device_printf(dev, "unable to set device clock to 48 kHZ\n");
		return (err);
	}
	return (0);
}

static void
jz4780_ehci_intr(void *arg)
{

	ehci_interrupt(arg);
}

static int
jz4780_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-ehci"))
		return (ENXIO);

	device_set_desc(dev, EHCI_HC_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_ehci_attach(device_t dev)
{
	struct jz4780_ehci_softc *isc;
	ehci_softc_t *sc;
	int err;
	int rid;
	uint32_t reg;

	isc = device_get_softc(dev);
	sc = &isc->base;

	/* initialise some bus fields */
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(dev), &ehci_iterate_hw_softc)) {
		return (ENOMEM);
	}

	sc->sc_bus.usbrev = USB_REV_2_0;

	err = jz4780_ehci_vbus_gpio_enable(dev);
	if (err)
		goto error;

	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(dev, "Could not map memory\n");
		goto error;
	}

	/*
	 * Craft special resource for bus space ops that handle
	 * byte-alignment of non-word addresses.
	 */
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	err = jz4780_ehci_clk_enable(dev);
	if (err)
		goto error;

	if (jz4780_ehci_enable() != 0) {
		device_printf(dev, "CGU failed to enable EHCI\n");
		err = ENXIO;
		goto error;
	}

	EWRITE4(sc, EHCI_USBINTR, 0);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "Could not allocate irq\n");
		goto error;
	}
	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(dev, "Could not add USB device\n");
		goto error;
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, EHCI_HC_DEVSTR);

	sprintf(sc->sc_vendor, "Ingenic");

	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, jz4780_ehci_intr, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(dev, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}

	err = ehci_init(sc);
	if (!err) {
		/* Voodoo: set utmi data bus width on controller to 16 bit */
		reg = EREAD4(sc, JZ_EHCI_REG_UTMI_BUS);
		reg |= UTMI_BUS_WIDTH;
		EWRITE4(sc, JZ_EHCI_REG_UTMI_BUS, reg);

		err = device_probe_and_attach(sc->sc_bus.bdev);
	}
	if (err) {
		device_printf(dev, "USB init failed err=%d\n", err);
		goto error;
	}
	return (0);

error:
	jz4780_ehci_detach(dev);
	return (ENXIO);
}

static int
jz4780_ehci_detach(device_t dev)
{
	struct jz4780_ehci_softc *isc;
	ehci_softc_t *sc;
	device_t bdev;
	int err;

	isc = device_get_softc(dev);
	sc = &isc->base;

	if (sc->sc_bus.bdev) {
		bdev = sc->sc_bus.bdev;
		device_detach(bdev);
		device_delete_child(dev, bdev);
	}
	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call ehci_detach() after ehci_init()
		 */
		ehci_detach(sc);

		err = bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intr_hdl);

		if (err)
			/* XXX or should we panic? */
			device_printf(dev, "Could not tear down irq, %d\n",
			    err);
		sc->sc_intr_hdl = NULL;
	}

	if (sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}

	if (isc->clk)
		clk_release(isc->clk);

	usb_bus_mem_free_all(&sc->sc_bus, &ehci_iterate_hw_softc);
	free(isc->gpio_vbus, M_DEVBUF);
	return (0);
}

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, jz4780_ehci_probe),
	DEVMETHOD(device_attach, jz4780_ehci_attach),
	DEVMETHOD(device_detach, jz4780_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t ehci_driver = {
	.name = "ehci",
	.methods = ehci_methods,
	.size = sizeof(struct jz4780_ehci_softc),
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci, simplebus, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(ehci, usb, 1, 1, 1);
MODULE_DEPEND(ehci, gpio, 1, 1, 1);
