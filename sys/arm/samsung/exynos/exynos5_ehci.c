/*-
 * Copyright (c) 2013-2014 Ruslan Bukin <br@bsdpad.com>
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

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/gpio.h>

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

#include <machine/bus.h>
#include <machine/resource.h>

#include <arm/samsung/exynos/exynos5_common.h>
#include <arm/samsung/exynos/exynos5_pmu.h>

#include "gpio_if.h"

#include "opt_platform.h"
#endif

/* GPIO control */
#define	GPIO_OUTPUT	1
#define	GPIO_INPUT	0
#define	PIN_USB		161

/* SYSREG */
#define	EXYNOS5_SYSREG_USB2_PHY	0x0
#define	USB2_MODE_HOST		0x1

/* USB HOST */
#define	HOST_CTRL_CLK_24MHZ	(5 << 16)
#define	HOST_CTRL_CLK_MASK	(7 << 16)
#define	HOST_CTRL_SIDDQ		(1 << 6)
#define	HOST_CTRL_SLEEP		(1 << 5)
#define	HOST_CTRL_SUSPEND	(1 << 4)
#define	HOST_CTRL_RESET_LINK	(1 << 1)
#define	HOST_CTRL_RESET_PHY	(1 << 0)
#define	HOST_CTRL_RESET_PHY_ALL	(1U << 31)

/* Forward declarations */
static int	exynos_ehci_attach(device_t dev);
static int	exynos_ehci_detach(device_t dev);
static int	exynos_ehci_probe(device_t dev);

struct exynos_ehci_softc {
	device_t		dev;
	ehci_softc_t		base;
	struct resource		*res[4];
	bus_space_tag_t		host_bst;
	bus_space_tag_t		sysreg_bst;
	bus_space_handle_t	host_bsh;
	bus_space_handle_t	sysreg_bsh;

};

static struct resource_spec exynos_ehci_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, exynos_ehci_probe),
	DEVMETHOD(device_attach, exynos_ehci_attach),
	DEVMETHOD(device_detach, exynos_ehci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	{ 0, 0 }
};

/* kobj_class definition */
static driver_t ehci_driver = {
	"ehci",
	ehci_methods,
	sizeof(struct exynos_ehci_softc)
};

static devclass_t ehci_devclass;

DRIVER_MODULE(exynos_ehci, simplebus, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(exynos_ehci, usb, 1, 1, 1);

/*
 * Public methods
 */
static int
exynos_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "exynos,usb-ehci") == 0)
		return (ENXIO);

	device_set_desc(dev, "Exynos integrated USB controller");
	return (BUS_PROBE_DEFAULT);
}

static int
gpio_ctrl(struct exynos_ehci_softc *esc, int dir, int power)
{
	device_t gpio_dev;

	/* Get the GPIO device, we need this to give power to USB */
	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		device_printf(esc->dev, "cant find gpio_dev\n");
		return (1);
	}

	if (power)
		GPIO_PIN_SET(gpio_dev, PIN_USB, GPIO_PIN_HIGH);
	else
		GPIO_PIN_SET(gpio_dev, PIN_USB, GPIO_PIN_LOW);

	if (dir)
		GPIO_PIN_SETFLAGS(gpio_dev, PIN_USB, GPIO_PIN_OUTPUT);
	else
		GPIO_PIN_SETFLAGS(gpio_dev, PIN_USB, GPIO_PIN_INPUT);

	return (0);
}

static int
reset_hsic_hub(struct exynos_ehci_softc *esc, phandle_t hub)
{
	device_t gpio_dev;
	pcell_t pin;

	/* TODO: check that hub is compatible with "smsc,usb3503" */
	if (!OF_hasprop(hub, "freebsd,reset-gpio")) {
		return (1);
	}

	if (OF_getencprop(hub, "freebsd,reset-gpio", &pin, sizeof(pin)) < 0) {
		device_printf(esc->dev,
		    "failed to decode reset GPIO pin number for HSIC hub\n");
		return (1);
	}

	/* Get the GPIO device, we need this to give power to USB */
	gpio_dev = devclass_get_device(devclass_find("gpio"), 0);
	if (gpio_dev == NULL) {
		device_printf(esc->dev, "Cant find gpio device\n");
		return (1);
	}

	GPIO_PIN_SET(gpio_dev, pin, GPIO_PIN_LOW);
	DELAY(100);
	GPIO_PIN_SET(gpio_dev, pin, GPIO_PIN_HIGH);

	return (0);
}

static int
phy_init(struct exynos_ehci_softc *esc)
{
	int reg;
	phandle_t hub;

	gpio_ctrl(esc, GPIO_INPUT, 1);

	/* set USB HOST mode */
	bus_space_write_4(esc->sysreg_bst, esc->sysreg_bsh,
	    EXYNOS5_SYSREG_USB2_PHY, USB2_MODE_HOST);

	/* Power ON phy */
	usb2_phy_power_on();

	reg = bus_space_read_4(esc->host_bst, esc->host_bsh, 0x0);
	reg &= ~(HOST_CTRL_CLK_MASK |
	    HOST_CTRL_RESET_PHY |
	    HOST_CTRL_RESET_PHY_ALL |
	    HOST_CTRL_SIDDQ |
	    HOST_CTRL_SUSPEND |
	    HOST_CTRL_SLEEP);

	reg |= (HOST_CTRL_CLK_24MHZ |
	    HOST_CTRL_RESET_LINK);
	bus_space_write_4(esc->host_bst, esc->host_bsh, 0x0, reg);

	DELAY(10);

	reg = bus_space_read_4(esc->host_bst, esc->host_bsh, 0x0);
	reg &= ~(HOST_CTRL_RESET_LINK);
	bus_space_write_4(esc->host_bst, esc->host_bsh, 0x0, reg);

	if ((hub = OF_finddevice("/hsichub")) != -1) {
		reset_hsic_hub(esc, hub);
	}

	gpio_ctrl(esc, GPIO_OUTPUT, 1);

	return (0);
}

static int
exynos_ehci_attach(device_t dev)
{
	struct exynos_ehci_softc *esc;
	ehci_softc_t *sc;
	bus_space_handle_t bsh;
	int err;

	esc = device_get_softc(dev);
	esc->dev = dev;
	sc = &esc->base;
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	if (bus_alloc_resources(dev, exynos_ehci_spec, esc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* EHCI registers */
	sc->sc_io_tag = rman_get_bustag(esc->res[0]);
	bsh = rman_get_bushandle(esc->res[0]);
	sc->sc_io_size = rman_get_size(esc->res[0]);

	/* EHCI HOST ctrl registers */
	esc->host_bst = rman_get_bustag(esc->res[1]);
	esc->host_bsh = rman_get_bushandle(esc->res[1]);

	/* SYSREG */
	esc->sysreg_bst = rman_get_bustag(esc->res[2]);
	esc->sysreg_bsh = rman_get_bushandle(esc->res[2]);

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(dev),
		&ehci_iterate_hw_softc))
		return (ENXIO);

	/*
	 * Set handle to USB related registers subregion used by
	 * generic EHCI driver.
	 */
	err = bus_space_subregion(sc->sc_io_tag, bsh, 0x0,
	    sc->sc_io_size, &sc->sc_io_hdl);
	if (err != 0)
		return (ENXIO);

	phy_init(esc);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, esc->res[3], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc,
	    &sc->sc_intr_hdl);
	if (err) {
		device_printf(dev, "Could not setup irq, "
		    "%d\n", err);
		return (1);
	}

	/* Add USB device */
	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(dev, "Could not add USB device\n");
		err = bus_teardown_intr(dev, esc->res[3],
		    sc->sc_intr_hdl);
		if (err)
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
		return (1);
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);

	strlcpy(sc->sc_vendor, "Samsung", sizeof(sc->sc_vendor));

	err = ehci_init(sc);
	if (!err) {
		sc->sc_flags |= EHCI_SCFLG_DONEINIT;
		err = device_probe_and_attach(sc->sc_bus.bdev);
	} else {
		device_printf(dev, "USB init failed err=%d\n", err);

		device_delete_child(dev, sc->sc_bus.bdev);
		sc->sc_bus.bdev = NULL;

		err = bus_teardown_intr(dev, esc->res[3],
		    sc->sc_intr_hdl);
		if (err)
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
		return (1);
	}
	return (0);
}

static int
exynos_ehci_detach(device_t dev)
{
	struct exynos_ehci_softc *esc;
	ehci_softc_t *sc;
	int err;

	esc = device_get_softc(dev);
	sc = &esc->base;

	if (sc->sc_flags & EHCI_SCFLG_DONEINIT)
		return (0);

	/*
	 * only call ehci_detach() after ehci_init()
	 */
	if (sc->sc_flags & EHCI_SCFLG_DONEINIT) {
		ehci_detach(sc);
		sc->sc_flags &= ~EHCI_SCFLG_DONEINIT;
	}

	/*
	 * Disable interrupts that might have been switched on in
	 * ehci_init.
	 */
	if (sc->sc_io_tag && sc->sc_io_hdl)
		bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl,
		    EHCI_USBINTR, 0);

	if (esc->res[3] && sc->sc_intr_hdl) {
		err = bus_teardown_intr(dev, esc->res[3],
		    sc->sc_intr_hdl);
		if (err) {
			device_printf(dev, "Could not tear down irq,"
			    " %d\n", err);
			return (err);
		}
		sc->sc_intr_hdl = NULL;
	}

	if (sc->sc_bus.bdev) {
		device_delete_child(dev, sc->sc_bus.bdev);
		sc->sc_bus.bdev = NULL;
	}

	/* During module unload there are lots of children leftover */
	device_delete_children(dev);

	bus_release_resources(dev, exynos_ehci_spec, esc->res);

	return (0);
}
