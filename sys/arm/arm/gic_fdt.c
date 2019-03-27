/*-
 * Copyright (c) 2011,2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
 *
 * Developed by Damjan Marion <damjan.marion@gmail.com>
 *
 * Based on OMAP4 GIC code by Ben Gray
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
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

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/arm/gic.h>
#include <arm/arm/gic_common.h>

struct arm_gic_devinfo {
	struct ofw_bus_devinfo	obdinfo;
	struct resource_list	rl;
};

struct arm_gic_fdt_softc {
	struct arm_gic_softc	base;
	pcell_t			addr_cells;
	pcell_t			size_cells;
};

static device_probe_t gic_fdt_probe;
static device_attach_t gic_fdt_attach;
static ofw_bus_get_devinfo_t gic_ofw_get_devinfo;
static bus_get_resource_list_t gic_fdt_get_resource_list;
static bool arm_gic_add_children(device_t);

static struct ofw_compat_data compat_data[] = {
	{"arm,gic",		true},	/* Non-standard, used in FreeBSD dts. */
	{"arm,gic-400",		true},
	{"arm,cortex-a15-gic",	true},
	{"arm,cortex-a9-gic",	true},
	{"arm,cortex-a7-gic",	true},
	{"arm,arm11mp-gic",	true},
	{"brcm,brahma-b15-gic",	true},
	{"qcom,msm-qgic2",	true},
	{NULL,			false}
};

static device_method_t gic_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gic_fdt_probe),
	DEVMETHOD(device_attach,	gic_fdt_attach),

	/* Bus interface */
	DEVMETHOD(bus_get_resource_list,gic_fdt_get_resource_list),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	gic_ofw_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END,
};

DEFINE_CLASS_1(gic, gic_fdt_driver, gic_fdt_methods,
    sizeof(struct arm_gic_fdt_softc), arm_gic_driver);

static devclass_t gic_fdt_devclass;

EARLY_DRIVER_MODULE(gic, simplebus, gic_fdt_driver, gic_fdt_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(gic, ofwbus, gic_fdt_driver, gic_fdt_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);

static int
gic_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);
	device_set_desc(dev, "ARM Generic Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
gic_fdt_attach(device_t dev)
{
	struct arm_gic_fdt_softc *sc = device_get_softc(dev);
	phandle_t pxref;
	intptr_t xref;
	int err;

	sc->base.gic_bus = GIC_BUS_FDT;

	err = arm_gic_attach(dev);
	if (err != 0)
		return (err);

	xref = OF_xref_from_node(ofw_bus_get_node(dev));

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto cleanup;
	}

	/*
	 * Controller is root if:
	 * - doesn't have interrupt parent
	 * - his interrupt parent is this controller
	 */
	pxref = ofw_bus_find_iparent(ofw_bus_get_node(dev));
	if (pxref == 0 || xref == pxref) {
		if (intr_pic_claim_root(dev, xref, arm_gic_intr, sc,
		    GIC_LAST_SGI - GIC_FIRST_SGI + 1) != 0) {
			device_printf(dev, "could not set PIC as a root\n");
			intr_pic_deregister(dev, xref);
			goto cleanup;
		}
	} else {
		if (sc->base.gic_res[2] == NULL) {
			device_printf(dev,
			    "not root PIC must have defined interrupt\n");
			intr_pic_deregister(dev, xref);
			goto cleanup;
		}
		if (bus_setup_intr(dev, sc->base.gic_res[2], INTR_TYPE_CLK,
		    arm_gic_intr, NULL, sc, &sc->base.gic_intrhand)) {
			device_printf(dev, "could not setup irq handler\n");
			intr_pic_deregister(dev, xref);
			goto cleanup;
		}
	}

	OF_device_register_xref(xref, dev);

	/* If we have children probe and attach them */
	if (arm_gic_add_children(dev)) {
		bus_generic_probe(dev);
		return (bus_generic_attach(dev));
	}

	return (0);

cleanup:
	arm_gic_detach(dev);
	return(ENXIO);
}

static struct resource_list *
gic_fdt_get_resource_list(device_t bus, device_t child)
{
	struct arm_gic_devinfo *di;

	di = device_get_ivars(child);
	KASSERT(di != NULL, ("gic_fdt_get_resource_list: No devinfo"));

	return (&di->rl);
}

static int
arm_gic_fill_ranges(phandle_t node, struct arm_gic_fdt_softc *sc)
{
	pcell_t host_cells;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int i, j, k;

	host_cells = 1;
	OF_getencprop(OF_parent(node), "#address-cells", &host_cells,
	    sizeof(host_cells));
	sc->addr_cells = 2;
	OF_getencprop(node, "#address-cells", &sc->addr_cells,
	    sizeof(sc->addr_cells));
	sc->size_cells = 2;
	OF_getencprop(node, "#size-cells", &sc->size_cells,
	    sizeof(sc->size_cells));

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges < 0)
		return (-1);
	sc->base.nranges = nbase_ranges / sizeof(cell_t) /
	    (sc->addr_cells + host_cells + sc->size_cells);
	if (sc->base.nranges == 0)
		return (0);

	sc->base.ranges = malloc(sc->base.nranges * sizeof(sc->base.ranges[0]),
	    M_DEVBUF, M_WAITOK);
	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->base.nranges; i++) {
		sc->base.ranges[i].bus = 0;
		for (k = 0; k < sc->addr_cells; k++) {
			sc->base.ranges[i].bus <<= 32;
			sc->base.ranges[i].bus |= base_ranges[j++];
		}
		sc->base.ranges[i].host = 0;
		for (k = 0; k < host_cells; k++) {
			sc->base.ranges[i].host <<= 32;
			sc->base.ranges[i].host |= base_ranges[j++];
		}
		sc->base.ranges[i].size = 0;
		for (k = 0; k < sc->size_cells; k++) {
			sc->base.ranges[i].size <<= 32;
			sc->base.ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_DEVBUF);
	return (sc->base.nranges);
}

static bool
arm_gic_add_children(device_t dev)
{
	struct arm_gic_fdt_softc *sc;
	struct arm_gic_devinfo *dinfo;
	phandle_t child, node;
	device_t cdev;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	/* If we have no children don't probe for them */
	child = OF_child(node);
	if (child == 0)
		return (false);

	if (arm_gic_fill_ranges(node, sc) < 0) {
		device_printf(dev, "Have a child, but no ranges\n");
		return (false);
	}

	for (; child != 0; child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);

		if (ofw_bus_gen_setup_devinfo(&dinfo->obdinfo, child) != 0) {
			free(dinfo, M_DEVBUF);
			continue;
		}

		resource_list_init(&dinfo->rl);
		ofw_bus_reg_to_rl(dev, child, sc->addr_cells,
		    sc->size_cells, &dinfo->rl);

		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    dinfo->obdinfo.obd_name);
			resource_list_free(&dinfo->rl);
			ofw_bus_gen_destroy_devinfo(&dinfo->obdinfo);
			free(dinfo, M_DEVBUF);
			continue;
		}
		device_set_ivars(cdev, dinfo);
	}

	return (true);
}

static const struct ofw_bus_devinfo *
gic_ofw_get_devinfo(device_t bus __unused, device_t child)
{
	struct arm_gic_devinfo *di;

	di = device_get_ivars(child);

	return (&di->obdinfo);
}

static struct ofw_compat_data gicv2m_compat_data[] = {
	{"arm,gic-v2m-frame",	true},
	{NULL,			false}
};

static int
arm_gicv2m_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, gicv2m_compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "ARM Generic Interrupt Controller MSI/MSIX");
	return (BUS_PROBE_DEFAULT);
}

static int
arm_gicv2m_fdt_attach(device_t dev)
{
	struct arm_gicv2m_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_xref = OF_xref_from_node(ofw_bus_get_node(dev));

	return (arm_gicv2m_attach(dev));
}

static device_method_t arm_gicv2m_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		arm_gicv2m_fdt_probe),
	DEVMETHOD(device_attach,	arm_gicv2m_fdt_attach),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(gicv2m, arm_gicv2m_fdt_driver, arm_gicv2m_fdt_methods,
    sizeof(struct arm_gicv2m_softc), arm_gicv2m_driver);

static devclass_t arm_gicv2m_fdt_devclass;

EARLY_DRIVER_MODULE(gicv2m, gic, arm_gicv2m_fdt_driver,
    arm_gicv2m_fdt_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
