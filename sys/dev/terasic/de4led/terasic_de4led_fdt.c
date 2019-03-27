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

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/terasic/de4led/terasic_de4led.h>

/*
 * Nexus bus attachment for the 8-element LED on the Terasic DE-4 FPGA board,
 * which is hooked up to the processor via a memory-mapped Avalon bus.
 */
static int
terasic_de4led_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "sri-cambridge,de4led")) {
		device_set_desc(dev, "Terasic DE4 8-element LED");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
terasic_de4led_fdt_attach(device_t dev)
{
	struct terasic_de4led_softc *sc;

	sc = device_get_softc(dev);
	sc->tdl_dev = dev;
	sc->tdl_unit = device_get_unit(dev);
	sc->tdl_rid = 0;
	sc->tdl_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->tdl_rid, RF_ACTIVE);
	if (sc->tdl_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		return (ENXIO);
	}
	terasic_de4led_attach(sc);
	return (0);
}

static int
terasic_de4led_fdt_detach(device_t dev)
{
	struct terasic_de4led_softc *sc;

	sc = device_get_softc(dev);
	KASSERT(sc->tdl_res != NULL, ("%s: resources not allocated",
	    __func__));
	terasic_de4led_detach(sc);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->tdl_rid, sc->tdl_res);
	return (0);
}

static device_method_t terasic_de4led_fdt_methods[] = {
	DEVMETHOD(device_probe,		terasic_de4led_fdt_probe),
	DEVMETHOD(device_attach,	terasic_de4led_fdt_attach),
	DEVMETHOD(device_detach,	terasic_de4led_fdt_detach),
	{ 0, 0 }
};

static driver_t terasic_de4led_fdt_driver = {
	"terasic_de4led",
	terasic_de4led_fdt_methods,
	sizeof(struct terasic_de4led_softc),
};

DRIVER_MODULE(terasic_de4led, simplebus, terasic_de4led_fdt_driver,
    terasic_de4led_devclass, 0, 0);
