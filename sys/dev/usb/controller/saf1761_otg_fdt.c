/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/rman.h>

#include <dev/fdt/fdt_common.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#endif					/* USB_GLOBAL_INCLUDE_FILE */

#include <dev/usb/controller/saf1761_otg.h>
#include <dev/usb/controller/saf1761_otg_reg.h>

static device_probe_t saf1761_otg_fdt_probe;
static device_attach_t saf1761_otg_fdt_attach;
static device_detach_t saf1761_otg_fdt_detach;

static device_method_t saf1761_otg_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, saf1761_otg_fdt_probe),
	DEVMETHOD(device_attach, saf1761_otg_fdt_attach),
	DEVMETHOD(device_detach, saf1761_otg_fdt_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t saf1761_otg_driver = {
	.name = "saf1761otg",
	.methods = saf1761_otg_methods,
	.size = sizeof(struct saf1761_otg_softc),
};

static devclass_t saf1761_otg_devclass;

DRIVER_MODULE(saf1761otg, simplebus, saf1761_otg_driver, saf1761_otg_devclass, 0, 0);
MODULE_DEPEND(saf1761otg, usb, 1, 1, 1);

static int
saf1761_otg_fdt_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "nxp,usb-isp1761"))
		return (ENXIO);

	device_set_desc(dev, "ISP1761/SAF1761 DCI USB 2.0 Device Controller");

	return (0);
}

static int
saf1761_otg_fdt_attach(device_t dev)
{
	struct saf1761_otg_softc *sc = device_get_softc(dev);
	char param[24];
	int err;
	int rid;

	/* get configuration from FDT */

	/* get bus-width, if any */
	if (OF_getprop(ofw_bus_get_node(dev), "bus-width",
	    &param, sizeof(param)) > 0) {
		param[sizeof(param) - 1] = 0;
		if (strcmp(param, "32") == 0)
			sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_DATA_BUS_WIDTH;
	} else {
		/* assume 32-bit data bus */
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_DATA_BUS_WIDTH;
	}

	/* get analog over-current setting */
	if (OF_getprop(ofw_bus_get_node(dev), "analog-oc",
	    &param, sizeof(param)) > 0) {
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_ANA_DIGI_OC;
	}

	/* get DACK polarity */
	if (OF_getprop(ofw_bus_get_node(dev), "dack-polarity",
	    &param, sizeof(param)) > 0) {
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_DACK_POL;
	}

	/* get DREQ polarity */
	if (OF_getprop(ofw_bus_get_node(dev), "dreq-polarity",
	    &param, sizeof(param)) > 0) {
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_DREQ_POL;
	}

	/* get IRQ polarity */
	if (OF_getprop(ofw_bus_get_node(dev), "int-polarity",
	    &param, sizeof(param)) > 0) {
		sc->sc_interrupt_cfg |= SOTG_INTERRUPT_CFG_INTPOL;
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_INTR_POL;
	}

	/* get IRQ level triggering */
	if (OF_getprop(ofw_bus_get_node(dev), "int-level",
	    &param, sizeof(param)) > 0) {
		sc->sc_interrupt_cfg |= SOTG_INTERRUPT_CFG_INTLVL;
		sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_INTR_LEVEL;
	}

	/* initialise some bus fields */
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = SOTG_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus,
	    USB_GET_DMA_TAG(dev), NULL)) {
		return (ENOMEM);
	}
	rid = 0;
	sc->sc_io_res =
	    bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);

	if (sc->sc_io_res == NULL) 
		goto error;

	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	/* try to allocate the HC interrupt first */
	rid = 1;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		/* try to allocate a common IRQ second */
		rid = 0;
		sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		    RF_SHAREABLE | RF_ACTIVE);
		if (sc->sc_irq_res == NULL)
			goto error;
	}

	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (sc->sc_bus.bdev == NULL)
		goto error;

	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);

	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_TTY | INTR_MPSAFE,
	    &saf1761_otg_filter_interrupt, &saf1761_otg_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		sc->sc_intr_hdl = NULL;
		goto error;
	}
	err = saf1761_otg_init(sc);
	if (err) {
		device_printf(dev, "Init failed\n");
		goto error;
	}
	err = device_probe_and_attach(sc->sc_bus.bdev);
	if (err) {
		device_printf(dev, "USB probe and attach failed\n");
		goto error;
	}
	return (0);

error:
	saf1761_otg_fdt_detach(dev);
	return (ENXIO);
}

static int
saf1761_otg_fdt_detach(device_t dev)
{
	struct saf1761_otg_softc *sc = device_get_softc(dev);
	int err;

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * Only call uninit() after init()
		 */
		saf1761_otg_uninit(sc);

		err = bus_teardown_intr(dev, sc->sc_irq_res,
		    sc->sc_intr_hdl);
		sc->sc_intr_hdl = NULL;
	}
	if (sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, 0,
		    sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}
	usb_bus_mem_free_all(&sc->sc_bus, NULL);

	return (0);
}
