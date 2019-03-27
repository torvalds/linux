/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
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

#include "opt_acpi.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/intr.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include "gic_v3_reg.h"
#include "gic_v3_var.h"

struct gic_v3_acpi_devinfo {
	struct gic_v3_devinfo	di_gic_dinfo;
	struct resource_list	di_rl;
};

static device_identify_t gic_v3_acpi_identify;
static device_probe_t gic_v3_acpi_probe;
static device_attach_t gic_v3_acpi_attach;
static bus_alloc_resource_t gic_v3_acpi_bus_alloc_res;

static void gic_v3_acpi_bus_attach(device_t);

static device_method_t gic_v3_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,		gic_v3_acpi_identify),
	DEVMETHOD(device_probe,			gic_v3_acpi_probe),
	DEVMETHOD(device_attach,		gic_v3_acpi_attach),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,		gic_v3_acpi_bus_alloc_res),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(gic, gic_v3_acpi_driver, gic_v3_acpi_methods,
    sizeof(struct gic_v3_softc), gic_v3_driver);

static devclass_t gic_v3_acpi_devclass;

EARLY_DRIVER_MODULE(gic_v3, acpi, gic_v3_acpi_driver, gic_v3_acpi_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);


struct madt_table_data {
	device_t parent;
	device_t dev;
	ACPI_MADT_GENERIC_DISTRIBUTOR *dist;
	int count;
};

static void
madt_handler(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	struct madt_table_data *madt_data;

	madt_data = (struct madt_table_data *)arg;

	switch(entry->Type) {
	case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
		if (madt_data->dist != NULL) {
			if (bootverbose)
				device_printf(madt_data->parent,
				    "gic: Already have a distributor table");
			break;
		}
		madt_data->dist = (ACPI_MADT_GENERIC_DISTRIBUTOR *)entry;
		break;

	case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
		break;

	default:
		break;
	}
}

static void
rdist_map(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	ACPI_MADT_GENERIC_REDISTRIBUTOR *redist;
	struct madt_table_data *madt_data;

	madt_data = (struct madt_table_data *)arg;

	switch(entry->Type) {
	case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
		redist = (ACPI_MADT_GENERIC_REDISTRIBUTOR *)entry;

		madt_data->count++;
		BUS_SET_RESOURCE(madt_data->parent, madt_data->dev,
		    SYS_RES_MEMORY, madt_data->count, redist->BaseAddress,
		    redist->Length);
		break;

	default:
		break;
	}
}

static void
gic_v3_acpi_identify(driver_t *driver, device_t parent)
{
	struct madt_table_data madt_data;
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;
	device_t dev;

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return;

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		device_printf(parent, "gic: Unable to map the MADT\n");
		return;
	}

	madt_data.parent = parent;
	madt_data.dist = NULL;
	madt_data.count = 0;

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    madt_handler, &madt_data);
	if (madt_data.dist == NULL) {
		device_printf(parent,
		    "No gic interrupt or distributor table\n");
		goto out;
	}
	/* This is for the wrong GIC version */
	if (madt_data.dist->Version != ACPI_MADT_GIC_VERSION_V3)
		goto out;

	dev = BUS_ADD_CHILD(parent, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE,
	    "gic", -1);
	if (dev == NULL) {
		device_printf(parent, "add gic child failed\n");
		goto out;
	}

	/* Add the MADT data */
	BUS_SET_RESOURCE(parent, dev, SYS_RES_MEMORY, 0,
	    madt_data.dist->BaseAddress, 128 * 1024);

	madt_data.dev = dev;
	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    rdist_map, &madt_data);

	acpi_set_private(dev, (void *)(uintptr_t)madt_data.dist->Version);

out:
	acpi_unmap_table(madt);
}

static int
gic_v3_acpi_probe(device_t dev)
{

	switch((uintptr_t)acpi_get_private(dev)) {
	case ACPI_MADT_GIC_VERSION_V3:
		break;
	default:
		return (ENXIO);
	}

	device_set_desc(dev, GIC_V3_DEVSTR);
	return (BUS_PROBE_NOWILDCARD);
}

static void
madt_count_redistrib(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	struct gic_v3_softc *sc = arg;

	if (entry->Type == ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR)
		sc->gic_redists.nregions++;
}

static int
gic_v3_acpi_count_regions(device_t dev)
{
	struct gic_v3_softc *sc;
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;

	sc = device_get_softc(dev);

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return (ENXIO);

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		device_printf(dev, "Unable to map the MADT\n");
		return (ENXIO);
	}

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    madt_count_redistrib, sc);
	acpi_unmap_table(madt);

	return (sc->gic_redists.nregions > 0 ? 0 : ENXIO);
}

static int
gic_v3_acpi_attach(device_t dev)
{
	struct gic_v3_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->gic_bus = GIC_BUS_ACPI;

	err = gic_v3_acpi_count_regions(dev);
	if (err != 0)
		goto error;

	err = gic_v3_attach(dev);
	if (err != 0)
		goto error;

	sc->gic_pic = intr_pic_register(dev, ACPI_INTR_XREF);
	if (sc->gic_pic == NULL) {
		device_printf(dev, "could not register PIC\n");
		err = ENXIO;
		goto error;
	}

	if (intr_pic_claim_root(dev, ACPI_INTR_XREF, arm_gic_v3_intr, sc,
	    GIC_LAST_SGI - GIC_FIRST_SGI + 1) != 0) {
		err = ENXIO;
		goto error;
	}

	/*
	 * Try to register the ITS driver to this GIC. The GIC will act as
	 * a bus in that case. Failure here will not affect the main GIC
	 * functionality.
	 */
	gic_v3_acpi_bus_attach(dev);

	if (device_get_children(dev, &sc->gic_children, &sc->gic_nchildren) !=0)
		sc->gic_nchildren = 0;

	return (0);

error:
	if (bootverbose) {
		device_printf(dev,
		    "Failed to attach. Error %d\n", err);
	}
	/* Failure so free resources */
	gic_v3_detach(dev);

	return (err);
}

static void
gic_v3_add_children(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	ACPI_MADT_GENERIC_TRANSLATOR *gict;
	struct gic_v3_acpi_devinfo *di;
	struct gic_v3_softc *sc;
	device_t child, dev;
	u_int xref;
	int err, pxm;

	if (entry->Type == ACPI_MADT_TYPE_GENERIC_TRANSLATOR) {
		/* We have an ITS, add it as a child */
		gict = (ACPI_MADT_GENERIC_TRANSLATOR *)entry;
		dev = arg;
		sc = device_get_softc(dev);

		child = device_add_child(dev, "its", -1);
		if (child == NULL)
			return;

		di = malloc(sizeof(*di), M_GIC_V3, M_WAITOK | M_ZERO);
		resource_list_init(&di->di_rl);
		resource_list_add(&di->di_rl, SYS_RES_MEMORY, 0,
		    gict->BaseAddress, gict->BaseAddress + 128 * 1024 - 1,
		    128 * 1024);
		err = acpi_iort_its_lookup(gict->TranslationId, &xref, &pxm);
		if (err == 0) {
			di->di_gic_dinfo.gic_domain = pxm;
			di->di_gic_dinfo.msi_xref = xref;
		} else {
			di->di_gic_dinfo.gic_domain = -1;
			di->di_gic_dinfo.msi_xref = ACPI_MSI_XREF;
		}
		sc->gic_nchildren++;
		device_set_ivars(child, di);
	}
}

static void
gic_v3_acpi_bus_attach(device_t dev)
{
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return;

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		device_printf(dev, "Unable to map the MADT to add children\n");
		return;
	}

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    gic_v3_add_children, dev);

	acpi_unmap_table(madt);

	bus_generic_attach(dev);
}

static struct resource *
gic_v3_acpi_bus_alloc_res(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct gic_v3_acpi_devinfo *di;
	struct resource_list_entry *rle;

	/* We only allocate memory */
	if (type != SYS_RES_MEMORY)
		return (NULL);

	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		if ((di = device_get_ivars(child)) == NULL)
			return (NULL);

		/* Find defaults for this rid */
		rle = resource_list_find(&di->di_rl, type, *rid);
		if (rle == NULL)
			return (NULL);

		start = rle->start;
		end = rle->end;
		count = rle->count;
	}

	return (bus_generic_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}
