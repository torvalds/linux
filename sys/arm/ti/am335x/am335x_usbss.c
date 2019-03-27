/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <dev/fdt/simplebus.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/musb_otg.h>
#include <dev/usb/usb_debug.h>

#include <sys/rman.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_scm.h>
#include <arm/ti/am335x/am335x_scm.h>

#define	AM335X_USB_PORTS	2

#define	USBSS_REVREG		0x00
#define	USBSS_SYSCONFIG		0x10
#define		USBSS_SYSCONFIG_SRESET		1

#define USBCTRL_REV		0x00
#define USBCTRL_CTRL		0x14
#define USBCTRL_STAT		0x18
#define USBCTRL_IRQ_STAT0	0x30
#define		IRQ_STAT0_RXSHIFT	16
#define		IRQ_STAT0_TXSHIFT	0
#define USBCTRL_IRQ_STAT1	0x34
#define 	IRQ_STAT1_DRVVBUS	(1 << 8)
#define USBCTRL_INTEN_SET0	0x38
#define USBCTRL_INTEN_SET1	0x3C
#define 	USBCTRL_INTEN_USB_ALL	0x1ff
#define 	USBCTRL_INTEN_USB_SOF	(1 << 3)
#define USBCTRL_INTEN_CLR0	0x40
#define USBCTRL_INTEN_CLR1	0x44
#define USBCTRL_UTMI		0xE0
#define		USBCTRL_UTMI_FSDATAEXT		(1 << 1)
#define USBCTRL_MODE		0xE8
#define 	USBCTRL_MODE_IDDIG		(1 << 8)
#define 	USBCTRL_MODE_IDDIGMUX		(1 << 7)

#define	USBSS_WRITE4(sc, reg, val)		\
    bus_write_4((sc)->sc_mem_res, (reg), (val))
#define	USBSS_READ4(sc, reg)			\
    bus_read_4((sc)->sc_mem_res, (reg))

static device_probe_t usbss_probe;
static device_attach_t usbss_attach;
static device_detach_t usbss_detach;

struct usbss_softc {
	struct simplebus_softc	simplebus_sc;
	struct resource		*sc_mem_res;
	int			sc_mem_rid;
};

static int
usbss_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,am33xx-usb"))
		return (ENXIO);

	device_set_desc(dev, "TI AM33xx integrated USB OTG controller");
	
	return (BUS_PROBE_DEFAULT);
}

static int
usbss_attach(device_t dev)
{
	struct usbss_softc *sc = device_get_softc(dev);
	int i;
	uint32_t rev;
	phandle_t node;

	/* Request the memory resources */
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev,
		    "Error: could not allocate mem resources\n");
		return (ENXIO);
	}

	/* Enable device clocks. */
	ti_prcm_clk_enable(MUSB0_CLK);

	/*
	 * Reset USBSS, USB0 and USB1.
	 * The registers of USB subsystem must not be accessed while the
	 * reset pulse is active (200ns).
	 */
	USBSS_WRITE4(sc, USBSS_SYSCONFIG, USBSS_SYSCONFIG_SRESET);
	DELAY(100);
	i = 10;
	while (USBSS_READ4(sc, USBSS_SYSCONFIG) & USBSS_SYSCONFIG_SRESET) {
		DELAY(100);
		if (i-- == 0) {
			device_printf(dev, "reset timeout.\n");
			return (ENXIO);
		}
	}

	/* Read the module revision. */
	rev = USBSS_READ4(sc, USBSS_REVREG);
	device_printf(dev, "TI AM335X USBSS v%d.%d.%d\n",
	    (rev >> 8) & 7, (rev >> 6) & 3, rev & 63);

	node = ofw_bus_get_node(dev);

	if (node == -1) {
		usbss_detach(dev);
		return (ENXIO);
	}

	simplebus_init(dev, node);

	/*
	 * Allow devices to identify.
	 */
	bus_generic_probe(dev);

	/*
	 * Now walk the OFW tree and attach top-level devices.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node))
		simplebus_add_device(dev, node, 0, NULL, -1, NULL);

	return (bus_generic_attach(dev));
}

static int
usbss_detach(device_t dev)
{
	struct usbss_softc *sc = device_get_softc(dev);

	/* Free resources if any */
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid,
		    sc->sc_mem_res);

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	return (0);
}

static device_method_t usbss_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, usbss_probe),
	DEVMETHOD(device_attach, usbss_attach),
	DEVMETHOD(device_detach, usbss_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

DEFINE_CLASS_1(usbss, usbss_driver, usbss_methods,
    sizeof(struct usbss_softc), simplebus_driver);
static devclass_t usbss_devclass;
DRIVER_MODULE(usbss, simplebus, usbss_driver, usbss_devclass, 0, 0);
MODULE_DEPEND(usbss, usb, 1, 1, 1);
