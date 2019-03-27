/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <geom/geom_disk.h>

#include <dev/altera/jtag_uart/altera_jtag_uart.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

/*
 * FDT bus attachment for Altera JTAG UARTs.
 */
static int
altera_jtag_uart_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "altera,jtag_uart-11_0")) {
		device_set_desc(dev, "Altera JTAG UART");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
altera_jtag_uart_fdt_attach(device_t dev)
{
	struct altera_jtag_uart_softc *sc;
	int error;

	error = 0;
	sc = device_get_softc(dev);
	sc->ajus_dev = dev;
	sc->ajus_unit = device_get_unit(dev);
	sc->ajus_mem_rid = 0;
	sc->ajus_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->ajus_mem_rid, RF_ACTIVE);
	if (sc->ajus_mem_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto out;
	}

	/*
	 * Interrupt support is optional -- if we can't allocate an IRQ, then
	 * we fall back on polling.
	 */
	sc->ajus_irq_rid = 0;
	sc->ajus_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->ajus_irq_rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->ajus_irq_res == NULL)
		device_printf(dev,
		    "IRQ unavailable; selecting polled operation\n");
	error = altera_jtag_uart_attach(sc);
out:
	if (error) {
		if (sc->ajus_irq_res != NULL)
			bus_release_resource(dev, SYS_RES_IRQ,
			    sc->ajus_irq_rid, sc->ajus_irq_res);
		if (sc->ajus_mem_res != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    sc->ajus_mem_rid, sc->ajus_mem_res);
	}
	return (error);
}

static int
altera_jtag_uart_fdt_detach(device_t dev)
{
	struct altera_jtag_uart_softc *sc;

	sc = device_get_softc(dev);
	KASSERT(sc->ajus_mem_res != NULL, ("%s: resources not allocated",
	    __func__));

	altera_jtag_uart_detach(sc);
	bus_release_resource(dev, SYS_RES_IRQ, sc->ajus_irq_rid,
	    sc->ajus_irq_res);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->ajus_mem_rid,
	    sc->ajus_mem_res);
	return (0);
}

static device_method_t altera_jtag_uart_fdt_methods[] = {
	DEVMETHOD(device_probe,		altera_jtag_uart_fdt_probe),
	DEVMETHOD(device_attach,	altera_jtag_uart_fdt_attach),
	DEVMETHOD(device_detach,	altera_jtag_uart_fdt_detach),
	{ 0, 0 }
};

static driver_t altera_jtag_uart_fdt_driver = {
	"altera_jtag_uart",
	altera_jtag_uart_fdt_methods,
	sizeof(struct altera_jtag_uart_softc),
};

DRIVER_MODULE(altera_jtag_uart, simplebus, altera_jtag_uart_fdt_driver,
    altera_jtag_uart_devclass, 0, 0);
