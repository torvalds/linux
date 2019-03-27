/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Joerg Wunsch
 *
 * derived from sys/i386/isa/pcf.c which is:
 *
 * Copyright (c) 1998 Nicolas Souchu, Marc Bouget
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
 * Hardware driver for a Philips PCF8584 I2C bus controller sitting
 * on a generic ISA bus.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <isa/isareg.h>
#include <isa/isavar.h>

#include <dev/iicbus/iiconf.h>
#include <dev/pcf/pcfvar.h>
#include "iicbus_if.h"

#define	PCF_NAME	"pcf"

static void pcf_isa_identify(driver_t *, device_t);
static int pcf_isa_probe(device_t);
static int pcf_isa_attach(device_t);
static int pcf_isa_detach(device_t);

static device_method_t pcf_isa_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	pcf_isa_identify),
	DEVMETHOD(device_probe,		pcf_isa_probe),
	DEVMETHOD(device_attach,	pcf_isa_attach),
	DEVMETHOD(device_detach,	pcf_isa_detach),

	/* iicbus interface */
	DEVMETHOD(iicbus_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_repeated_start, pcf_repeated_start),
	DEVMETHOD(iicbus_start,		pcf_start),
	DEVMETHOD(iicbus_stop,		pcf_stop),
	DEVMETHOD(iicbus_write,		pcf_write),
	DEVMETHOD(iicbus_read,		pcf_read),
	DEVMETHOD(iicbus_reset,		pcf_rst_card),
	{ 0, 0 }
};

static devclass_t pcf_isa_devclass;

static driver_t pcf_isa_driver = {
	PCF_NAME,
	pcf_isa_methods,
	sizeof(struct pcf_softc),
};

static void
pcf_isa_identify(driver_t *driver, device_t parent)
{
	BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, PCF_NAME, 0);

	return;
}

static int
pcf_isa_probe(device_t dev)
{
	rman_res_t	start, count;
	u_int		rid = 0, port, error;

	/* skip PnP probes */
	if (isa_get_logicalid(dev))
		return (ENXIO);

	/* The port address must be explicitly specified */
	bus_get_resource(dev, SYS_RES_IOPORT, rid, &start, &count);
	if ((error = resource_int_value(PCF_NAME, 0, "port", &port)) != 0)
		return (error);

	/* Probe is only successful for the specified base io */
	if (port != (u_int)start)
		return (ENXIO);

	device_set_desc(dev, "PCF8584 I2C bus controller");

	return (0);
}

static int
pcf_isa_attach(device_t dev)
{
	struct pcf_softc *sc;
	int rv = ENXIO;

	sc = DEVTOSOFTC(dev);
	mtx_init(&sc->pcf_lock, device_get_nameunit(dev), "pcf", MTX_DEF);

	/* IO port is mandatory */
	sc->res_ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
						&sc->rid_ioport, RF_ACTIVE);
	if (sc->res_ioport == 0) {
		device_printf(dev, "cannot reserve I/O port range\n");
		goto error;
	}

	sc->pcf_flags = device_get_flags(dev);

	if (!(sc->pcf_flags & IIC_POLLED)) {
		sc->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->rid_irq,
						     RF_ACTIVE);
		if (sc->res_irq == 0) {
			device_printf(dev, "can't reserve irq, polled mode.\n");
			sc->pcf_flags |= IIC_POLLED;
		}
	}

	/* reset the chip */
	pcf_rst_card(dev, IIC_FASTEST, PCF_DEFAULT_ADDR, NULL);

	if (sc->res_irq) {
		rv = bus_setup_intr(dev, sc->res_irq,
				    INTR_TYPE_NET /* | INTR_ENTROPY */,
				    NULL, pcf_intr, sc, &sc->intr_cookie);
		if (rv) {
			device_printf(dev, "could not setup IRQ\n");
			goto error;
		}
	}

	if ((sc->iicbus = device_add_child(dev, "iicbus", -1)) == NULL)
		device_printf(dev, "could not allocate iicbus instance\n");

	/* probe and attach the iicbus */
	bus_generic_attach(dev);

	return (0);

error:
	if (sc->res_irq != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->rid_irq,
				     sc->res_irq);
	}
	if (sc->res_ioport != 0) {
		bus_release_resource(dev, SYS_RES_IOPORT, sc->rid_ioport,
				     sc->res_ioport);
	}
	mtx_destroy(&sc->pcf_lock);
	return (rv);
}

static int
pcf_isa_detach(device_t dev)
{
	struct pcf_softc *sc;
	int rv;

	sc = DEVTOSOFTC(dev);

	if ((rv = bus_generic_detach(dev)) != 0)
		return (rv);

	if ((rv = device_delete_child(dev, sc->iicbus)) != 0)
		return (rv);

	if (sc->res_irq != 0) {
		bus_teardown_intr(dev, sc->res_irq, sc->intr_cookie);
		bus_release_resource(dev, SYS_RES_IRQ, sc->rid_irq, sc->res_irq);
	}

	bus_release_resource(dev, SYS_RES_IOPORT, sc->rid_ioport, sc->res_ioport);
	mtx_destroy(&sc->pcf_lock);

	return (0);
}

DRIVER_MODULE(pcf_isa, isa, pcf_isa_driver, pcf_isa_devclass, 0, 0);
