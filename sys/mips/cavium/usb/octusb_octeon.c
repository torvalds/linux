#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Hans Petter Selasky. All rights reserved.
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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>
#include <contrib/octeon-sdk/cvmx-usb.h>

#include <mips/cavium/usb/octusb.h>

#define	MEM_RID	0

static device_identify_t octusb_octeon_identify;
static device_probe_t octusb_octeon_probe;
static device_attach_t octusb_octeon_attach;
static device_detach_t octusb_octeon_detach;

struct octusb_octeon_softc {
	struct octusb_softc sc_dci;	/* must be first */
};

static void
octusb_octeon_identify(driver_t *drv, device_t parent)
{
	if (octeon_has_feature(OCTEON_FEATURE_USB))
		BUS_ADD_CHILD(parent, 0, "octusb", 0);
}

static int
octusb_octeon_probe(device_t dev)
{
	device_set_desc(dev, "Cavium Octeon USB controller");
	return (0);
}

static int
octusb_octeon_attach(device_t dev)
{
	struct octusb_octeon_softc *sc = device_get_softc(dev);
	int err;
	int rid;
	int nports;
	int i;

	/* setup controller interface softc */

	/* initialise some bus fields */
	sc->sc_dci.sc_bus.parent = dev;
	sc->sc_dci.sc_bus.devices = sc->sc_dci.sc_devices;
	sc->sc_dci.sc_bus.devices_max = OCTUSB_MAX_DEVICES;
	sc->sc_dci.sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_dci.sc_bus,
	    USB_GET_DMA_TAG(dev), NULL)) {
		return (ENOMEM);
	}
	nports = cvmx_usb_get_num_ports();
	if (nports > OCTUSB_MAX_PORTS)
		panic("octusb: too many USB ports %d", nports);
	for (i = 0; i < nports; i++) {
		rid = 0;
		sc->sc_dci.sc_irq_res[i] =
		    bus_alloc_resource(dev, SYS_RES_IRQ, &rid,
			       OCTEON_IRQ_USB0 + i, OCTEON_IRQ_USB0 + i, 1, RF_ACTIVE);
		if (!(sc->sc_dci.sc_irq_res[i])) {
			goto error;
		}

#if (__FreeBSD_version >= 700031)
		err = bus_setup_intr(dev, sc->sc_dci.sc_irq_res[i], INTR_TYPE_BIO | INTR_MPSAFE,
		    NULL, (driver_intr_t *)octusb_interrupt, sc, &sc->sc_dci.sc_intr_hdl[i]);
#else
		err = bus_setup_intr(dev, sc->sc_dci.sc_irq_res[i], INTR_TYPE_BIO | INTR_MPSAFE,
		    (driver_intr_t *)octusb_interrupt, sc, &sc->sc_dci.sc_intr_hdl[i]);
#endif
		if (err) {
			sc->sc_dci.sc_intr_hdl[i] = NULL;
			goto error;
		}
	}

	sc->sc_dci.sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!(sc->sc_dci.sc_bus.bdev)) {
		goto error;
	}
	device_set_ivars(sc->sc_dci.sc_bus.bdev, &sc->sc_dci.sc_bus);


	err = octusb_init(&sc->sc_dci);
	if (!err) {
		err = device_probe_and_attach(sc->sc_dci.sc_bus.bdev);
	}
	if (err) {
		goto error;
	}
	return (0);

error:
	octusb_octeon_detach(dev);
	return (ENXIO);
}

static int
octusb_octeon_detach(device_t dev)
{
	struct octusb_octeon_softc *sc = device_get_softc(dev);
	int err;
	int nports;
	int i;

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (sc->sc_dci.sc_irq_res[0] && sc->sc_dci.sc_intr_hdl[0])
		/*
	 	 * only call octusb_octeon_uninit() after octusb_octeon_init()
	 	 */
		octusb_uninit(&sc->sc_dci);

	nports = cvmx_usb_get_num_ports();
	if (nports > OCTUSB_MAX_PORTS)
		panic("octusb: too many USB ports %d", nports);
	for (i = 0; i < nports; i++) {
		if (sc->sc_dci.sc_irq_res[0] && sc->sc_dci.sc_intr_hdl[0]) {
			err = bus_teardown_intr(dev, sc->sc_dci.sc_irq_res[i],
			    sc->sc_dci.sc_intr_hdl[i]);
			sc->sc_dci.sc_intr_hdl[i] = NULL;
		}
		if (sc->sc_dci.sc_irq_res) {
			bus_release_resource(dev, SYS_RES_IRQ, 0,
			    sc->sc_dci.sc_irq_res[i]);
			sc->sc_dci.sc_irq_res[i] = NULL;
		}
	}
	usb_bus_mem_free_all(&sc->sc_dci.sc_bus, NULL);

	return (0);
}

static device_method_t octusb_octeon_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify, octusb_octeon_identify),
	DEVMETHOD(device_probe, octusb_octeon_probe),
	DEVMETHOD(device_attach, octusb_octeon_attach),
	DEVMETHOD(device_detach, octusb_octeon_detach),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t octusb_octeon_driver = {
	.name = "octusb",
	.methods = octusb_octeon_methods,
	.size = sizeof(struct octusb_octeon_softc),
};

static devclass_t octusb_octeon_devclass;

DRIVER_MODULE(octusb, ciu, octusb_octeon_driver, octusb_octeon_devclass, 0, 0);
