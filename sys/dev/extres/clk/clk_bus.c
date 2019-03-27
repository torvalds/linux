/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus_subr.h>

struct ofw_clkbus_softc {
	struct simplebus_softc simplebus_sc;
};

static int
ofw_clkbus_probe(device_t dev)
{
	const char	*name;

	name = ofw_bus_get_name(dev);

	if (name == NULL || strcmp(name, "clocks") != 0)
		return (ENXIO);

	device_set_desc(dev, "OFW clocks bus");

	return (BUS_PROBE_GENERIC);
}

static int
ofw_clkbus_attach(device_t dev)
{
	phandle_t node, child;
	device_t cdev;

	node  = ofw_bus_get_node(dev);
	simplebus_init(dev, node);

	for (child = OF_child(node); child > 0; child = OF_peer(child)) {
		cdev = simplebus_add_device(dev, child, 0, NULL, -1, NULL);
		if (cdev != NULL)
			device_probe_and_attach(cdev);
	}

	return (bus_generic_attach(dev));
}

static device_method_t ofw_clkbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_clkbus_probe),
	DEVMETHOD(device_attach,	ofw_clkbus_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(ofw_clkbus, ofw_clkbus_driver, ofw_clkbus_methods,
    sizeof(struct ofw_clkbus_softc), simplebus_driver);
static devclass_t ofw_clkbus_devclass;
EARLY_DRIVER_MODULE(ofw_clkbus, simplebus, ofw_clkbus_driver,
    ofw_clkbus_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(ofw_clkbus, 1);
