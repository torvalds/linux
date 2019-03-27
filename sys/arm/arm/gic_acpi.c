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

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <machine/intr.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include <arm/arm/gic.h>
#include <arm/arm/gic_common.h>

struct gic_acpi_devinfo {
	struct resource_list	rl;
};

static device_identify_t gic_acpi_identify;
static device_probe_t gic_acpi_probe;
static device_attach_t gic_acpi_attach;
static bus_get_resource_list_t gic_acpi_get_resource_list;
static bool arm_gic_add_children(device_t);

static device_method_t gic_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	gic_acpi_identify),
	DEVMETHOD(device_probe,		gic_acpi_probe),
	DEVMETHOD(device_attach,	gic_acpi_attach),

	/* Bus interface */
	DEVMETHOD(bus_get_resource_list, gic_acpi_get_resource_list),

	DEVMETHOD_END,
};

DEFINE_CLASS_1(gic, gic_acpi_driver, gic_acpi_methods,
    sizeof(struct arm_gic_softc), arm_gic_driver);

static devclass_t gic_acpi_devclass;

EARLY_DRIVER_MODULE(gic, acpi, gic_acpi_driver, gic_acpi_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);

struct madt_table_data {
	device_t parent;
	ACPI_MADT_GENERIC_DISTRIBUTOR *dist;
	ACPI_MADT_GENERIC_INTERRUPT *intr[MAXCPU];
};

static void
madt_handler(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	struct madt_table_data *madt_data;
	ACPI_MADT_GENERIC_INTERRUPT *intr;

	madt_data = (struct madt_table_data *)arg;

	switch(entry->Type) {
	case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
		if (madt_data->dist != NULL) {
			if (bootverbose)
				device_printf(madt_data->parent,
				    "gic: Already have a distributor table");
		} else
			madt_data->dist =
			    (ACPI_MADT_GENERIC_DISTRIBUTOR *)entry;
		break;
	case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
		intr = (ACPI_MADT_GENERIC_INTERRUPT *)entry;
		if (intr->CpuInterfaceNumber < MAXCPU)
			madt_data->intr[intr->CpuInterfaceNumber] = intr;
		break;
	}
}

static void
gic_acpi_identify(driver_t *driver, device_t parent)
{
	struct madt_table_data madt_data;
	ACPI_MADT_GENERIC_INTERRUPT *intr;
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;
	device_t dev;
	int i;

	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return;

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		device_printf(parent, "gic: Unable to map the MADT\n");
		return;
	}

	bzero(&madt_data, sizeof(madt_data));
	madt_data.parent = parent;
	madt_data.dist = NULL;

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    madt_handler, &madt_data);

	/* Check the version of the GIC we have */
	switch (madt_data.dist->Version) {
	case ACPI_MADT_GIC_VERSION_NONE:
	case ACPI_MADT_GIC_VERSION_V1:
	case ACPI_MADT_GIC_VERSION_V2:
		break;
	default:
		goto out;
	}

	intr = NULL;
	for (i = 0; i < MAXCPU; i++) {
		if (madt_data.intr[i] != NULL) {
			if (intr == NULL) {
				intr = madt_data.intr[i];
			} else if (intr->BaseAddress !=
			    madt_data.intr[i]->BaseAddress) {
				device_printf(parent,
"gic: Not all CPU interfaces at the same address, this may fail\n");
			}
		}
	}
	if (intr == NULL) {
		device_printf(parent, "gic: No CPU interfaces found\n");
		goto out;
	}

	dev = BUS_ADD_CHILD(parent, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE,
	    "gic", -1);
	if (dev == NULL) {
		device_printf(parent, "add gic child failed\n");
		goto out;
	}

	BUS_SET_RESOURCE(parent, dev, SYS_RES_MEMORY, 0,
	    madt_data.dist->BaseAddress, 4 * 1024);
	BUS_SET_RESOURCE(parent, dev, SYS_RES_MEMORY, 1,
	    intr->BaseAddress, 4 * 1024);

	acpi_set_private(dev, (void *)(uintptr_t)madt_data.dist->Version);
out:
	acpi_unmap_table(madt);
}

static int
gic_acpi_probe(device_t dev)
{

	switch((uintptr_t)acpi_get_private(dev)) {
	case ACPI_MADT_GIC_VERSION_NONE:
	case ACPI_MADT_GIC_VERSION_V1:
	case ACPI_MADT_GIC_VERSION_V2:
		break;
	default:
		return (ENXIO);
	}

	device_set_desc(dev, "ARM Generic Interrupt Controller");
	return (BUS_PROBE_NOWILDCARD);
}

static int
gic_acpi_attach(device_t dev)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	intptr_t xref;
	int err;

	sc->gic_bus = GIC_BUS_ACPI;

	err = arm_gic_attach(dev);
	if (err != 0)
		return (err);

	xref = ACPI_INTR_XREF;

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto cleanup;
	}

	/*
	 * Controller is root:
	 */
	if (intr_pic_claim_root(dev, xref, arm_gic_intr, sc,
	    GIC_LAST_SGI - GIC_FIRST_SGI + 1) != 0) {
		device_printf(dev, "could not set PIC as a root\n");
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}
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
gic_acpi_get_resource_list(device_t bus, device_t child)
{
	struct gic_acpi_devinfo *di;

	di = device_get_ivars(child);
	KASSERT(di != NULL, ("gic_acpi_get_resource_list: No devinfo"));

	return (&di->rl);
}

static void
madt_gicv2m_handler(ACPI_SUBTABLE_HEADER *entry, void *arg)
{
	struct arm_gic_softc *sc;
	ACPI_MADT_GENERIC_MSI_FRAME *msi;
	struct gic_acpi_devinfo *dinfo;
	device_t dev, cdev;

	if (entry->Type == ACPI_MADT_TYPE_GENERIC_MSI_FRAME) {
		sc = arg;
		dev = sc->gic_dev;
		msi = (ACPI_MADT_GENERIC_MSI_FRAME *)entry;

		device_printf(dev, "frame: %x %lx %x %u %u\n", msi->MsiFrameId,
		    msi->BaseAddress, msi->Flags, msi->SpiCount, msi->SpiBase);

		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL)
			return;

		dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);
		resource_list_init(&dinfo->rl);
		resource_list_add(&dinfo->rl, SYS_RES_MEMORY, 0,
		    msi->BaseAddress, msi->BaseAddress + PAGE_SIZE - 1,
		    PAGE_SIZE);
		device_set_ivars(cdev, dinfo);
	}
}

static bool
arm_gic_add_children(device_t dev)
{
	struct arm_gic_softc *sc = device_get_softc(dev);
	ACPI_TABLE_MADT *madt;
	vm_paddr_t physaddr;

	/* This should return a valid address as it did in gic_acpi_identify */
	physaddr = acpi_find_table(ACPI_SIG_MADT);
	if (physaddr == 0)
		return (false);

	madt = acpi_map_table(physaddr, ACPI_SIG_MADT);
	if (madt == NULL) {
		device_printf(dev, "gic: Unable to map the MADT\n");
		return (false);
	}

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    madt_gicv2m_handler, sc);

	acpi_unmap_table(madt);

	return (true);
}

static int
arm_gicv2m_acpi_probe(device_t dev)
{

	if (gic_get_bus(dev) != GIC_BUS_ACPI)
		return (EINVAL);

	if (gic_get_hw_rev(dev) > 2)
		return (EINVAL);

	device_set_desc(dev, "ARM Generic Interrupt Controller MSI/MSIX");
	return (BUS_PROBE_DEFAULT);
}

static device_method_t arm_gicv2m_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		arm_gicv2m_acpi_probe),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(gicv2m, arm_gicv2m_acpi_driver, arm_gicv2m_acpi_methods,
    sizeof(struct arm_gicv2m_softc), arm_gicv2m_driver);

static devclass_t arm_gicv2m_acpi_devclass;

EARLY_DRIVER_MODULE(gicv2m_acpi, gic, arm_gicv2m_acpi_driver,
    arm_gicv2m_acpi_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
