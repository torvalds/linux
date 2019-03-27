/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/simplebus.h>

/*
 * Driver for Mediatek/Ralink Palmbus
 *
 * Currently the only reason for the existence of this driver is so that we can
 * minimize the changes required to the upstream DTS files we use.
 * Otherwise palmbus is a very simple subclass of our simplebus and the only
 * difference between the two is the actual value of the compatible property.
 */

static int
palmbus_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!(ofw_bus_is_compatible(dev, "palmbus") &&
	    ofw_bus_has_prop(dev, "ranges")) &&
	    (ofw_bus_get_type(dev) == NULL || strcmp(ofw_bus_get_type(dev),
	    "soc") != 0))
		return (ENXIO);

	device_set_desc(dev, "MTK Palmbus");

	return (0);
}

static device_method_t palmbus_methods[] = {
	DEVMETHOD(device_probe,		palmbus_probe),

	DEVMETHOD_END
};

DEFINE_CLASS_1(palmbus, palmbus_driver, palmbus_methods,
    sizeof(struct simplebus_softc), simplebus_driver);
static devclass_t palmbus_devclass;
EARLY_DRIVER_MODULE(palmbus, ofwbus, palmbus_driver, palmbus_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(palmbus, 1);
