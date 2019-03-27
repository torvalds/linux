/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

/*
 * Cavium Octeon Ethernet pseudo-bus attachment.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include "ethernet-common.h"

#include "octebusvar.h"

static void		octebus_identify(driver_t *drv, device_t parent);
static int		octebus_probe(device_t dev);
static int		octebus_attach(device_t dev);
static int		octebus_detach(device_t dev);
static int		octebus_shutdown(device_t dev);

static device_method_t octebus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	octebus_identify),
	DEVMETHOD(device_probe,		octebus_probe),
	DEVMETHOD(device_attach,	octebus_attach),
	DEVMETHOD(device_detach,	octebus_detach),
	DEVMETHOD(device_shutdown,	octebus_shutdown),

	/* Bus interface.  */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),

	{ 0, 0 }
};

static driver_t octebus_driver = {
	"octebus",
	octebus_methods,
	sizeof (struct octebus_softc),
};

static devclass_t octebus_devclass;

DRIVER_MODULE(octebus, ciu, octebus_driver, octebus_devclass, 0, 0);

static void
octebus_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "octebus", 0);
}

static int
octebus_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);
	device_set_desc(dev, "Cavium Octeon Ethernet pseudo-bus");
	return (0);
}

static int
octebus_attach(device_t dev)
{
	struct octebus_softc *sc;
	int rv;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	rv = cvm_oct_init_module(dev);
	if (rv != 0)
		return (ENXIO);

	return (0);
}

static int
octebus_detach(device_t dev)
{
	cvm_oct_cleanup_module(dev);
	return (0);
}

static int
octebus_shutdown(device_t dev)
{
	return (octebus_detach(dev));
}
