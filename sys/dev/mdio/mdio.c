/*-
 * Copyright (c) 2011-2012 Stefan Bethke.
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>

#include <dev/mdio/mdio.h>

#include "mdio_if.h"

static void
mdio_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, mdio_driver.name, -1) == NULL)
		BUS_ADD_CHILD(parent, 0, mdio_driver.name, -1);
}

static int
mdio_probe(device_t dev)
{

	device_set_desc(dev, "MDIO");

	return (BUS_PROBE_SPECIFIC);
}

static int
mdio_attach(device_t dev)
{

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	return (bus_generic_attach(dev));
}

static int
mdio_detach(device_t dev)
{

	bus_generic_detach(dev);
	return (0);
}

static int
mdio_readreg(device_t dev, int phy, int reg)
{

	return (MDIO_READREG(device_get_parent(dev), phy, reg));
}

static int
mdio_writereg(device_t dev, int phy, int reg, int val)
{

	return (MDIO_WRITEREG(device_get_parent(dev), phy, reg, val));
}

static int
mdio_readextreg(device_t dev, int phy, int devad, int reg)
{

	return (MDIO_READEXTREG(device_get_parent(dev), phy, devad, reg));
}

static int
mdio_writeextreg(device_t dev, int phy, int devad, int reg,
    int val)
{

	return (MDIO_WRITEEXTREG(device_get_parent(dev), phy, devad, reg, val));
}

static void
mdio_hinted_child(device_t dev, const char *name, int unit)
{

	device_add_child(dev, name, unit);
}

static device_method_t mdio_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	mdio_identify),
	DEVMETHOD(device_probe,		mdio_probe),
	DEVMETHOD(device_attach,	mdio_attach),
	DEVMETHOD(device_detach,	mdio_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	DEVMETHOD(bus_hinted_child,	mdio_hinted_child),

	/* MDIO access */
	DEVMETHOD(mdio_readreg,		mdio_readreg),
	DEVMETHOD(mdio_writereg,	mdio_writereg),
	DEVMETHOD(mdio_readextreg,	mdio_readextreg),
	DEVMETHOD(mdio_writeextreg,	mdio_writeextreg),

	DEVMETHOD_END
};

driver_t mdio_driver = {
	"mdio",
	mdio_methods,
	0
};

devclass_t mdio_devclass;

MODULE_VERSION(mdio, 1);
