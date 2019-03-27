/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marius Strobl, Joerg Wunsch
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
 * Device specific driver for the EBus i2c devices found on some sun4u
 * systems. On systems not having a boot-bus controller the i2c devices
 * are PCF8584.
 *
 * Known onboard slave devices on the primary bus are:
 *
 * AXe:
 *	0x40	PCF8574 I/O	fan status (CPU fans 1+2)
 *	0x9e	PCF8591 A/D	temperature (CPU + hotspot)
 *
 * AXmp:
 *	0x70	PCF8574 I/O	fan status (fans 1-4)
 *	0x78	PCF8574 I/O	fan fail interrupt
 *	0x9a	PCF8591 A/D	voltage (CPU core)
 *	0x9c	PCF8591 A/D	temperature (hotspots 1+2, aux. analog 1+2)
 *	0x9e	PCF8591 A/D	temperature (CPUs 1-4)
 *
 * CP1400:
 *	0x70	PCF8574 I/O	reserved for factory use
 *	0x9e	PCF8591 A/D	temperature (CPU)
 *
 * CP1500:
 *	0x70	PCF8574 I/O	reserved for factory use
 *	0x72	PCF8574 I/O	geographic address + power supply status lines
 *	0x9e	PCF8591 A/D	temperature (CPU)
 *	0xa0	AT24C01A	hostid
 *
 * For AXmp, CP1400 and CP1500 these are described in more detail in:
 * http://www.sun.com/oem/products/manuals/805-7581-04.pdf
 *
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/systm.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <dev/iicbus/iiconf.h>
#include <dev/pcf/pcfvar.h>
#include "iicbus_if.h"

#define	PCF_NAME	"pcf"

static int pcf_ebus_probe(device_t);
static int pcf_ebus_attach(device_t);
static int pcf_ebus_detach(device_t);

static device_method_t pcf_ebus_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		pcf_ebus_probe),
	DEVMETHOD(device_attach,	pcf_ebus_attach),
	DEVMETHOD(device_detach,	pcf_ebus_detach),

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

static devclass_t pcf_ebus_devclass;

static driver_t pcf_ebus_driver = {
	PCF_NAME,
	pcf_ebus_methods,
	sizeof(struct pcf_softc),
};

static int
pcf_ebus_probe(device_t dev)
{
	const char *compat;

	/*
	 * We must not attach to this i2c device if this is a system with
	 * a boot-bus controller. Additionally testing the compatibility
	 * property will hopefully take care of this.
	 */
	if (strcmp("i2c", ofw_bus_get_name(dev)) == 0) {
		compat = ofw_bus_get_compat(dev);
		if (compat != NULL && strcmp("i2cpcf,8584", compat) == 0) {
			device_set_desc(dev, "PCF8584 I2C bus controller");
			return (0);
		}
	}
	return (ENXIO);
}

static int
pcf_ebus_attach(device_t dev)
{
	struct pcf_softc *sc;
	int rv = ENXIO;
	phandle_t node;
	uint64_t own_addr;

	sc = DEVTOSOFTC(dev);
	mtx_init(&sc->pcf_lock, device_get_nameunit(dev), "pcf", MTX_DEF);

	/* get OFW node of the pcf */
	if ((node = ofw_bus_get_node(dev)) == -1) {
		device_printf(dev, "cannot get OFW node\n");
		goto error;
	}

	/* IO port is mandatory */
	sc->res_ioport = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->rid_ioport, RF_ACTIVE);
	if (sc->res_ioport == 0) {
		device_printf(dev, "cannot reserve I/O port range\n");
		goto error;
	}

	sc->pcf_flags = device_get_flags(dev);

	/*
	 * XXX use poll-mode property?
	 */
	if (!(sc->pcf_flags & IIC_POLLED)) {
		sc->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &sc->rid_irq, RF_ACTIVE);
		if (sc->res_irq == 0) {
			device_printf(dev, "can't reserve irq, polled mode.\n");
			sc->pcf_flags |= IIC_POLLED;
		}
	}

	/*
	 * XXX on AXmp there's probably a second IRQ which is the fan fail
	 *     interrupt genererated by the PCF8574 at 0x78.
	 */

	/* get address of the pcf */
	if (OF_getprop(node, "own-address", &own_addr, sizeof(own_addr)) ==
	    -1) {
		device_printf(dev, "cannot get own address\n");
		goto error;
	}
	if (bootverbose)
		device_printf(dev, "PCF8584 address: 0x%08llx\n", (unsigned
		    long long)own_addr);

	/* reset the chip */
	pcf_rst_card(dev, IIC_FASTEST, own_addr, NULL);

	if (sc->res_irq) {
		rv = bus_setup_intr(dev, sc->res_irq,
		    INTR_TYPE_NET /* | INTR_ENTROPY */, NULL, pcf_intr, sc,
		    &sc->intr_cookie);
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
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid_ioport,
		    sc->res_ioport);
	}
	mtx_destroy(&sc->pcf_lock);
	return (rv);
}

static int
pcf_ebus_detach(device_t dev)
{
	struct pcf_softc *sc;
	int rv;

	sc = DEVTOSOFTC(dev);

	if ((rv = bus_generic_detach(dev)) != 0)
		return (rv);

	if ((rv = device_delete_child(dev, sc->iicbus)) != 0)
		return (rv);

	if (sc->res_irq != 0) {
		bus_teardown_intr(dev, sc->res_irq,
		    sc->intr_cookie);
		bus_release_resource(dev, SYS_RES_IRQ, sc->rid_irq,
		    sc->res_irq);
	}

	bus_release_resource(dev, SYS_RES_MEMORY, sc->rid_ioport,
	    sc->res_ioport);
	mtx_destroy(&sc->pcf_lock);

	return (0);
}

DRIVER_MODULE(pcf_ebus, ebus, pcf_ebus_driver, pcf_ebus_devclass, 0, 0);
