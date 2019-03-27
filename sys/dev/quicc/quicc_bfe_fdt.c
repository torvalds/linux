/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006 Juniper Networks.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/tty.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/quicc/quicc_bfe.h>

static int quicc_fdt_probe(device_t dev);

static device_method_t quicc_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		quicc_fdt_probe),
	DEVMETHOD(device_attach,	quicc_bfe_attach),
	DEVMETHOD(device_detach,	quicc_bfe_detach),

	DEVMETHOD(bus_alloc_resource,	quicc_bus_alloc_resource),
	DEVMETHOD(bus_release_resource,	quicc_bus_release_resource),
	DEVMETHOD(bus_get_resource,	quicc_bus_get_resource),
	DEVMETHOD(bus_read_ivar,	quicc_bus_read_ivar),
	DEVMETHOD(bus_setup_intr,	quicc_bus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	quicc_bus_teardown_intr),

	DEVMETHOD_END
};

static driver_t quicc_fdt_driver = {
	quicc_driver_name,
	quicc_fdt_methods,
	sizeof(struct quicc_softc),
};

static int
quicc_fdt_probe(device_t dev)
{
	phandle_t par;
	pcell_t clock;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,cpm2"))
		return (ENXIO);

	par = OF_parent(ofw_bus_get_node(dev));
	if (OF_getprop(par, "bus-frequency", &clock, sizeof(clock)) <= 0)
		clock = 0;

	return (quicc_bfe_probe(dev, (uintptr_t)clock));
}

DRIVER_MODULE(quicc, simplebus, quicc_fdt_driver, quicc_devclass, 0, 0);
