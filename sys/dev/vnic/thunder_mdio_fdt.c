/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/simplebus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "thunder_mdio_var.h"

static int thunder_mdio_fdt_probe(device_t);
static int thunder_mdio_fdt_attach(device_t);

static device_method_t thunder_mdio_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		thunder_mdio_fdt_probe),
	DEVMETHOD(device_attach,	thunder_mdio_fdt_attach),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(thunder_mdio, thunder_mdio_fdt_driver, thunder_mdio_fdt_methods,
    sizeof(struct thunder_mdio_softc), thunder_mdio_driver);

static devclass_t thunder_mdio_fdt_devclass;

EARLY_DRIVER_MODULE(thunder_mdio, ofwbus, thunder_mdio_fdt_driver,
    thunder_mdio_fdt_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(thunder_mdio, mdionexus, thunder_mdio_fdt_driver,
    thunder_mdio_fdt_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);

static struct ofw_compat_data mdio_compat_data[] = {
	{"cavium,octeon-3860-mdio",	true},
	{"cavium,thunder-8890-mdio",	true},
	{NULL,				false}
};

static int
thunder_mdio_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_search_compatible(dev, mdio_compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, THUNDER_MDIO_DEVSTR);
	return (BUS_PROBE_DEFAULT);
}

static int
thunder_mdio_fdt_attach(device_t dev)
{
	phandle_t node;
	int ret;

	/* Call core attach */
	ret = thunder_mdio_attach(dev);
	if (ret != 0)
		return (ret);
	/*
	 * Register device to this node/xref.
	 * Thanks to that we will be able to retrieve device_t structure
	 * while holding only node reference acquired from FDT.
	 */
	node = ofw_bus_get_node(dev);
	OF_device_register_xref(OF_xref_from_node(node), dev);

	return (0);
}

struct mdionexus_softc {
	struct simplebus_softc simplebus_sc;
};

static device_probe_t mdionexus_fdt_probe;
static device_attach_t mdionexus_fdt_attach;

static const struct ofw_bus_devinfo * mdionexus_ofw_get_devinfo(device_t,
    device_t);

static device_method_t mdionexus_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mdionexus_fdt_probe),
	DEVMETHOD(device_attach,	mdionexus_fdt_attach),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,		bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	mdionexus_ofw_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

DEFINE_CLASS_0(mdionexus, mdionexus_fdt_driver, mdionexus_fdt_methods,
    sizeof(struct mdionexus_softc));

static devclass_t mdionexus_fdt_devclass;

EARLY_DRIVER_MODULE(mdionexus, mrmlbus, mdionexus_fdt_driver,
    mdionexus_fdt_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);

static int mdionexus_ofw_fill_ranges(phandle_t, struct simplebus_softc *);
static int mdionexus_ofw_bus_attach(device_t);

static int
mdionexus_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "cavium,thunder-8890-mdio-nexus"))
		return (ENXIO);

	device_set_desc(dev, "Cavium ThunderX MDIO nexus");
	return (BUS_PROBE_SPECIFIC);
}

static int
mdionexus_fdt_attach(device_t dev)
{
	int err;

	err = mdionexus_ofw_bus_attach(dev);
	if (err != 0)
		return (err);

	return (bus_generic_attach(dev));
}

/* OFW bus interface */
struct mdionexus_ofw_devinfo {
	struct ofw_bus_devinfo	di_dinfo;
	struct resource_list	di_rl;
};

static const struct ofw_bus_devinfo *
mdionexus_ofw_get_devinfo(device_t bus __unused, device_t child)
{
	struct mdionexus_ofw_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_dinfo);
}

/* Helper functions */

static int
mdionexus_ofw_fill_ranges(phandle_t node, struct simplebus_softc *sc)
{
	int host_address_cells;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int err;
	int i, j, k;

	err = OF_searchencprop(OF_parent(node), "#address-cells",
	    &host_address_cells, sizeof(host_address_cells));
	if (err <= 0)
		return (-1);

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges < 0)
		return (-1);
	sc->nranges = nbase_ranges / sizeof(cell_t) /
	    (sc->acells + host_address_cells + sc->scells);
	if (sc->nranges == 0)
		return (0);

	sc->ranges = malloc(sc->nranges * sizeof(sc->ranges[0]),
	    M_THUNDER_MDIO, M_WAITOK);
	base_ranges = malloc(nbase_ranges, M_THUNDER_MDIO, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->nranges; i++) {
		sc->ranges[i].bus = 0;
		for (k = 0; k < sc->acells; k++) {
			sc->ranges[i].bus <<= 32;
			sc->ranges[i].bus |= base_ranges[j++];
		}
		sc->ranges[i].host = 0;
		for (k = 0; k < host_address_cells; k++) {
			sc->ranges[i].host <<= 32;
			sc->ranges[i].host |= base_ranges[j++];
		}
		sc->ranges[i].size = 0;
		for (k = 0; k < sc->scells; k++) {
			sc->ranges[i].size <<= 32;
			sc->ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_THUNDER_MDIO);
	return (sc->nranges);
}

static int
mdionexus_ofw_bus_attach(device_t dev)
{
	struct simplebus_softc *sc;
	struct mdionexus_ofw_devinfo *di;
	device_t child;
	phandle_t parent, node;

	parent = ofw_bus_get_node(dev);
	simplebus_init(dev, parent);

	sc = (struct simplebus_softc *)device_get_softc(dev);

	if (mdionexus_ofw_fill_ranges(parent, sc) < 0) {
		device_printf(dev, "could not get ranges\n");
		return (ENXIO);
	}
	/* Iterate through all bus subordinates */
	for (node = OF_child(parent); node > 0; node = OF_peer(node)) {
		/* Allocate and populate devinfo. */
		di = malloc(sizeof(*di), M_THUNDER_MDIO, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&di->di_dinfo, node) != 0) {
			free(di, M_THUNDER_MDIO);
			continue;
		}

		/* Initialize and populate resource list. */
		resource_list_init(&di->di_rl);
		ofw_bus_reg_to_rl(dev, node, sc->acells, sc->scells,
		    &di->di_rl);
		ofw_bus_intr_to_rl(dev, node, &di->di_rl, NULL);

		/* Add newbus device for this FDT node */
		child = device_add_child(dev, NULL, -1);
		if (child == NULL) {
			resource_list_free(&di->di_rl);
			ofw_bus_gen_destroy_devinfo(&di->di_dinfo);
			free(di, M_THUNDER_MDIO);
			continue;
		}

		device_set_ivars(child, di);
	}

	return (0);
}
