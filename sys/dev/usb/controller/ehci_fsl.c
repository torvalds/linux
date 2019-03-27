/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
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

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/lockmgr.h>
#include <sys/condvar.h>
#include <sys/rman.h>

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
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

#include <machine/bus.h>
#include <machine/clock.h>
#include <machine/resource.h>

#include <powerpc/include/tlb.h>

#include "opt_platform.h"

/*
 * Register the driver
 */
/* Forward declarations */
static int	fsl_ehci_attach(device_t self);
static int	fsl_ehci_detach(device_t self);
static int	fsl_ehci_probe(device_t self);

static device_method_t ehci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, fsl_ehci_probe),
	DEVMETHOD(device_attach, fsl_ehci_attach),
	DEVMETHOD(device_detach, fsl_ehci_detach),
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
	sizeof(struct ehci_softc)
};

static devclass_t ehci_devclass;

DRIVER_MODULE(ehci, simplebus, ehci_driver, ehci_devclass, 0, 0);
MODULE_DEPEND(ehci, usb, 1, 1, 1);

/*
 * Private defines
 */
#define FSL_EHCI_REG_OFF	0x100
#define FSL_EHCI_REG_SIZE	0x300

/*
 * Internal interface registers' offsets.
 * Offsets from 0x000 ehci dev space, big-endian access.
 */
enum internal_reg {
	SNOOP1		= 0x400,
	SNOOP2		= 0x404,
	AGE_CNT_THRESH	= 0x408,
	SI_CTRL		= 0x410,
	CONTROL		= 0x500
};

/* CONTROL register bit flags */
enum control_flags {
	USB_EN		= 0x00000004,
	UTMI_PHY_EN	= 0x00000200,
	ULPI_INT_EN	= 0x00000001
};

/* SI_CTRL register bit flags */
enum si_ctrl_flags {
	FETCH_32	= 1,
	FETCH_64	= 0
};

#define SNOOP_RANGE_2GB	0x1E

/*
 * Operational registers' offsets.
 * Offsets from USBCMD register, little-endian access.
 */
enum special_op_reg {
	USBMODE		= 0x0A8,
	PORTSC		= 0x084,
	ULPI_VIEWPORT	= 0x70
};

/* USBMODE register bit flags */
enum usbmode_flags {
	HOST_MODE	= 0x3,
	DEVICE_MODE	= 0x2
};

#define	PORT_POWER_MASK	0x00001000

/*
 * Private methods
 */

static void
set_to_host_mode(ehci_softc_t *sc)
{
	int tmp;

	tmp = bus_space_read_4(sc->sc_io_tag, sc->sc_io_hdl, USBMODE);
	bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl, USBMODE, tmp | HOST_MODE);
}

static void
enable_usb(device_t dev, bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int tmp;
	phandle_t node;
	char *phy_type;

	phy_type = NULL;
	tmp = bus_space_read_4(iot, ioh, CONTROL) | USB_EN;

	node = ofw_bus_get_node(dev);
	if ((node != 0) &&
	    (OF_getprop_alloc(node, "phy_type", (void **)&phy_type) > 0)) {
		if (strncasecmp(phy_type, "utmi", strlen("utmi")) == 0)
			tmp |= UTMI_PHY_EN;
		OF_prop_free(phy_type);
	}
	bus_space_write_4(iot, ioh, CONTROL, tmp);
}

static void
set_32b_prefetch(bus_space_tag_t iot, bus_space_handle_t ioh)
{

	bus_space_write_4(iot, ioh, SI_CTRL, FETCH_32);
}

static void
set_snooping(bus_space_tag_t iot, bus_space_handle_t ioh)
{

	bus_space_write_4(iot, ioh, SNOOP1, SNOOP_RANGE_2GB);
	bus_space_write_4(iot, ioh, SNOOP2, 0x80000000 | SNOOP_RANGE_2GB);
}

static void
clear_port_power(ehci_softc_t *sc)
{
	int tmp;

	tmp = bus_space_read_4(sc->sc_io_tag, sc->sc_io_hdl, PORTSC);
	bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl, PORTSC, tmp & ~PORT_POWER_MASK);
}

/*
 * Public methods
 */
static int
fsl_ehci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (((ofw_bus_is_compatible(dev, "fsl-usb2-dr")) == 0) &&
	    ((ofw_bus_is_compatible(dev, "fsl-usb2-mph")) == 0))
		return (ENXIO);

	device_set_desc(dev, "Freescale integrated EHCI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
fsl_ehci_attach(device_t self)
{
	ehci_softc_t *sc;
	int rid;
	int err;
	bus_space_handle_t ioh;
	bus_space_tag_t iot;

	sc = device_get_softc(self);
	rid = 0;

	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = EHCI_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(self), &ehci_iterate_hw_softc))
		return (ENOMEM);

	/* Allocate io resource for EHCI */
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_io_res == NULL) {
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENXIO);
	}
	iot = rman_get_bustag(sc->sc_io_res);

	/*
	 * Set handle to USB related registers subregion used by generic
	 * EHCI driver
	 */
	ioh = rman_get_bushandle(sc->sc_io_res);

	err = bus_space_subregion(iot, ioh, FSL_EHCI_REG_OFF, FSL_EHCI_REG_SIZE,
	    &sc->sc_io_hdl);
	if (err != 0) {
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENXIO);
	}

	/* Set little-endian tag for use by the generic EHCI driver */
	sc->sc_io_tag = &bs_le_tag;

	/* Allocate irq */
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENXIO);
	}

	/* Setup interrupt handler */
	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)ehci_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENXIO);
	}

	/* Add USB device */
	sc->sc_bus.bdev = device_add_child(self, "usbus", -1);
	if (!sc->sc_bus.bdev) {
		device_printf(self, "Could not add USB device\n");
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENOMEM);
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);

	sc->sc_id_vendor = 0x1234;
	strlcpy(sc->sc_vendor, "Freescale", sizeof(sc->sc_vendor));

	/* Enable USB */
	err = ehci_reset(sc);
	if (err) {
		device_printf(self, "Could not reset the controller\n");
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (ENXIO);
	}

	enable_usb(self, iot, ioh);
	set_snooping(iot, ioh);
	set_to_host_mode(sc);
	set_32b_prefetch(iot, ioh);

	/*
	 * If usb subsystem is enabled in U-Boot, port power has to be turned
	 * off to allow proper discovery of devices during boot up.
	 */
	clear_port_power(sc);

	/* Set flags */
	sc->sc_flags |= EHCI_SCFLG_DONTRESET | EHCI_SCFLG_NORESTERM;

	err = ehci_init(sc);
	if (!err) {
		sc->sc_flags |= EHCI_SCFLG_DONEINIT;
		err = device_probe_and_attach(sc->sc_bus.bdev);
	}

	if (err) {
		device_printf(self, "USB init failed err=%d\n", err);
		err = fsl_ehci_detach(self);
		if (err) {
			device_printf(self,
			    "Detach of the driver failed with error %d\n",
			    err);
		}
		return (EIO);
	}

	return (0);
}

static int
fsl_ehci_detach(device_t self)
{

	int err;
	ehci_softc_t *sc;

	sc = device_get_softc(self);
	/*
	 * only call ehci_detach() after ehci_init()
	 */
	if (sc->sc_flags & EHCI_SCFLG_DONEINIT) {
		ehci_detach(sc);
		sc->sc_flags &= ~EHCI_SCFLG_DONEINIT;
	}

	/* Disable interrupts that might have been switched on in ehci_init */
	if (sc->sc_io_tag && sc->sc_io_hdl)
		bus_space_write_4(sc->sc_io_tag, sc->sc_io_hdl, EHCI_USBINTR, 0);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);
		if (err) {
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
			return (err);
		}
		sc->sc_intr_hdl = NULL;
	}

	if (sc->sc_bus.bdev) {
		device_delete_child(self, sc->sc_bus.bdev);
		sc->sc_bus.bdev = NULL;
	}

	/* During module unload there are lots of children leftover */
	device_delete_children(self);

	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0, sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}

	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, 0, sc->sc_io_res);
		sc->sc_io_res = NULL;
		sc->sc_io_tag = 0;
		sc->sc_io_hdl = 0;
	}

	return (0);
}

