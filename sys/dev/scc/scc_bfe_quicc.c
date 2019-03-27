/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/quicc/quicc_bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/scc/scc_bfe.h>

static int
scc_quicc_probe(device_t dev)
{
	device_t parent;
	struct scc_softc *sc;
	uintptr_t devtype, rclk;
	int error;

	parent = device_get_parent(dev);

	error = BUS_READ_IVAR(parent, dev, QUICC_IVAR_DEVTYPE, &devtype);
	if (error)
		return (error);
	if (devtype != QUICC_DEVTYPE_SCC)
		return (ENXIO);

	device_set_desc(dev, "QUICC quad channel SCC");

	sc = device_get_softc(dev);
	sc->sc_class = &scc_quicc_class;
	if (BUS_READ_IVAR(parent, dev, QUICC_IVAR_BRGCLK, &rclk))
		rclk = 0;
	return (scc_bfe_probe(dev, 0, rclk, 0));
}

static int
scc_quicc_attach(device_t dev)
{

	return (scc_bfe_attach(dev, 0));
}

static device_method_t scc_quicc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		scc_quicc_probe),
	DEVMETHOD(device_attach,	scc_quicc_attach),
	DEVMETHOD(device_detach,	scc_bfe_detach),

	DEVMETHOD(bus_alloc_resource,	scc_bus_alloc_resource),
	DEVMETHOD(bus_release_resource,	scc_bus_release_resource),
	DEVMETHOD(bus_get_resource,	scc_bus_get_resource),
	DEVMETHOD(bus_read_ivar,	scc_bus_read_ivar),
	DEVMETHOD(bus_setup_intr,	scc_bus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	scc_bus_teardown_intr),

	DEVMETHOD_END
};

static driver_t scc_quicc_driver = {
	scc_driver_name,
	scc_quicc_methods,
	sizeof(struct scc_softc),
};

DRIVER_MODULE(scc, quicc, scc_quicc_driver, scc_devclass, NULL, NULL);
