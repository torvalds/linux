/*-
 * Copyright (c) 2009, Nathan Whitehorn <nwhitehorn@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "iicbus_if.h"

/* Methods */
static device_probe_t ofw_iicbus_probe;
static device_attach_t ofw_iicbus_attach;
static device_t ofw_iicbus_add_child(device_t dev, u_int order,
    const char *name, int unit);
static const struct ofw_bus_devinfo *ofw_iicbus_get_devinfo(device_t bus,
    device_t dev);

static device_method_t ofw_iicbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_iicbus_probe),
	DEVMETHOD(device_attach,	ofw_iicbus_attach),

	/* Bus interface */
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),
	DEVMETHOD(bus_add_child,	ofw_iicbus_add_child),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	ofw_iicbus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

struct ofw_iicbus_devinfo {
	struct iicbus_ivar	opd_dinfo;	/* Must be the first. */
	struct ofw_bus_devinfo	opd_obdinfo;
};

devclass_t ofw_iicbus_devclass;

DEFINE_CLASS_1(iicbus, ofw_iicbus_driver, ofw_iicbus_methods,
    sizeof(struct iicbus_softc), iicbus_driver);
EARLY_DRIVER_MODULE(ofw_iicbus, iicbb, ofw_iicbus_driver, ofw_iicbus_devclass,
    0, 0, BUS_PASS_BUS);
EARLY_DRIVER_MODULE(ofw_iicbus, iichb, ofw_iicbus_driver, ofw_iicbus_devclass,
    0, 0, BUS_PASS_BUS);
EARLY_DRIVER_MODULE(ofw_iicbus, twsi, ofw_iicbus_driver, ofw_iicbus_devclass,
    0, 0, BUS_PASS_BUS);
MODULE_VERSION(ofw_iicbus, 1);
MODULE_DEPEND(ofw_iicbus, iicbus, 1, 1, 1);

static int
ofw_iicbus_probe(device_t dev)
{

	if (ofw_bus_get_node(dev) == -1)
		return (ENXIO);
	device_set_desc(dev, "OFW I2C bus");

	return (0);
}

static int
ofw_iicbus_attach(device_t dev)
{
	struct iicbus_softc *sc = IICBUS_SOFTC(dev);
	struct ofw_iicbus_devinfo *dinfo;
	phandle_t child, node, root;
	pcell_t freq, paddr;
	device_t childdev;
	ssize_t compatlen;
	char compat[255];
	char *curstr;
	u_int iic_addr_8bit = 0;

	sc->dev = dev;
	mtx_init(&sc->lock, "iicbus", NULL, MTX_DEF);

	/*
	 * If there is a clock-frequency property for the device node, use it as
	 * the starting value for the bus frequency.  Then call the common
	 * routine that handles the tunable/sysctl which allows the FDT value to
	 * be overridden by the user.
	 */
	node = ofw_bus_get_node(dev);
	freq = 0;
	OF_getencprop(node, "clock-frequency", &freq, sizeof(freq));
	iicbus_init_frequency(dev, freq);
	
	iicbus_reset(dev, IIC_FASTEST, 0, NULL);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);

	/*
	 * Check if we're running on a PowerMac, needed for the I2C
	 * address below.
	 */
	root = OF_peer(0);
	compatlen = OF_getprop(root, "compatible", compat,
				sizeof(compat));
	if (compatlen != -1) {
	    for (curstr = compat; curstr < compat + compatlen;
		curstr += strlen(curstr) + 1) {
		if (strncmp(curstr, "MacRISC", 7) == 0)
		    iic_addr_8bit = 1;
	    }
	}

	/*
	 * Attach those children represented in the device tree.
	 */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		/*
		 * Try to get the I2C address first from the i2c-address
		 * property, then try the reg property.  It moves around
		 * on different systems.
		 */
		if (OF_getencprop(child, "i2c-address", &paddr,
		    sizeof(paddr)) == -1)
			if (OF_getencprop(child, "reg", &paddr,
			    sizeof(paddr)) == -1)
				continue;

		/*
		 * Now set up the I2C and OFW bus layer devinfo and add it
		 * to the bus.
		 */
		dinfo = malloc(sizeof(struct ofw_iicbus_devinfo), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (dinfo == NULL)
			continue;
		/*
		 * FreeBSD drivers expect I2C addresses to be expressed as
		 * 8-bit values.  Apple OFW data contains 8-bit values, but
		 * Linux FDT data contains 7-bit values, so shift them up to
		 * 8-bit format.
		 */
		if (iic_addr_8bit)
		    dinfo->opd_dinfo.addr = paddr;
		else
		    dinfo->opd_dinfo.addr = paddr << 1;

		if (ofw_bus_gen_setup_devinfo(&dinfo->opd_obdinfo, child) !=
		    0) {
			free(dinfo, M_DEVBUF);
			continue;
		}

		childdev = device_add_child(dev, NULL, -1);
		resource_list_init(&dinfo->opd_dinfo.rl);
		ofw_bus_intr_to_rl(childdev, child,
					&dinfo->opd_dinfo.rl, NULL);
		device_set_ivars(childdev, dinfo);
	}

	/* Register bus */
	OF_device_register_xref(OF_xref_from_node(node), dev);
	return (bus_generic_attach(dev));
}

static device_t
ofw_iicbus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	device_t child;
	struct ofw_iicbus_devinfo *devi;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (child);
	devi = malloc(sizeof(struct ofw_iicbus_devinfo), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (devi == NULL) {
		device_delete_child(dev, child);
		return (0);
	}

	/*
	 * NULL all the OFW-related parts of the ivars for non-OFW
	 * children.
	 */
	devi->opd_obdinfo.obd_node = -1;
	devi->opd_obdinfo.obd_name = NULL;
	devi->opd_obdinfo.obd_compat = NULL;
	devi->opd_obdinfo.obd_type = NULL;
	devi->opd_obdinfo.obd_model = NULL;

	device_set_ivars(child, devi);

	return (child);
}

static const struct ofw_bus_devinfo *
ofw_iicbus_get_devinfo(device_t bus, device_t dev)
{
	struct ofw_iicbus_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	return (&dinfo->opd_obdinfo);
}
