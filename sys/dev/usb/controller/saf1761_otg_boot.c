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

#include USB_GLOBAL_INCLUDE_FILE

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

DRIVER_MODULE(saf1761otg, pci, saf1761_otg_driver, saf1761_otg_devclass, 0, 0);
MODULE_DEPEND(saf1761otg, usb, 1, 1, 1);

static int
saf1761_otg_fdt_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "ISP1761/SAF1761 DCI USB 2.0 Device Controller");

	return (0);
}

static int
saf1761_otg_fdt_attach(device_t dev)
{
	struct saf1761_otg_softc *sc = device_get_softc(dev);
	int err;

	/* 32-bit data bus */
	sc->sc_hw_mode |= SOTG_HW_MODE_CTRL_DATA_BUS_WIDTH;

	/* initialise some bus fields */
	sc->sc_bus.parent = dev;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = SOTG_MAX_DEVICES;
	sc->sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_bus, USB_GET_DMA_TAG(dev), NULL))
		return (ENOMEM);

	sc->sc_io_res = (void *)1;
	sc->sc_io_tag = (void *)1;
	sc->sc_io_hdl = (void *)USB_PCI_MEMORY_ADDRESS;
	sc->sc_io_size = USB_PCI_MEMORY_SIZE;

	sc->sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (sc->sc_bus.bdev == NULL)
		goto error;

	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_interrupt(dev, &saf1761_otg_filter_interrupt, &saf1761_otg_interrupt, sc);

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

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (sc->sc_irq_res) {
		/*
		 * Only call uninit() after init()
		 */
		saf1761_otg_uninit(sc);
	}
	usb_bus_mem_free_all(&sc->sc_bus, NULL);

	return (0);
}
