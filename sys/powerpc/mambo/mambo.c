/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2008 by Nathan Whitehorn. All rights reserved.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/rman.h>

/*
 * Mambo interface
 */
static int	mambobus_probe(device_t);
static int	mambobus_attach(device_t);

static device_method_t  mambobus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mambobus_probe),
	DEVMETHOD(device_attach,	mambobus_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_read_ivar,	bus_generic_read_ivar),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,bus_generic_activate_resource),

	DEVMETHOD_END
};

static driver_t mambobus_driver = {
	"mambo",
	mambobus_methods,
	0
};

static devclass_t mambobus_devclass;

DRIVER_MODULE(mambo, ofwbus, mambobus_driver, mambobus_devclass, 0, 0);

static int
mambobus_probe(device_t dev)
{
	const char *name = ofw_bus_get_name(dev);

	if (name && !strcmp(name, "mambo")) {
		device_set_desc(dev, "Mambo Simulator");
		return (0);
	}

	return (ENXIO);
}

static int
mambobus_attach(device_t dev)
{
	bus_generic_probe(dev);
	return (bus_generic_attach(dev));
}

