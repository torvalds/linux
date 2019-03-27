/*-
 * Copyright (c) 2009-2012 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ahci/ahci.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#endif

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

static struct ofw_compat_data compat_data[] = {
	{"generic-ahci", 		1},
	{"snps,dwc-ahci",		1},
	{"marvell,armada-3700-ahci",	1},
	{NULL,				0}
};

static int
ahci_fdt_probe(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	phandle_t node;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc_copy(dev, "AHCI SATA controller");
	node = ofw_bus_get_node(dev);
	ctlr->dma_coherent = OF_hasprop(node, "dma-coherent");
	return (BUS_PROBE_DEFAULT);
}
#endif

#ifdef DEV_ACPI
static int
ahci_acpi_probe(device_t dev)
{
	ACPI_HANDLE h;

	if ((h = acpi_get_handle(dev)) == NULL)
		return (ENXIO);

	if (pci_get_class(dev) == PCIC_STORAGE &&
	    pci_get_subclass(dev) == PCIS_STORAGE_SATA &&
	    pci_get_progif(dev) == PCIP_STORAGE_SATA_AHCI_1_0) {
		device_set_desc_copy(dev, "AHCI SATA controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}
#endif

static int
ahci_gen_ctlr_reset(device_t dev)
{

	return ahci_ctlr_reset(dev);
}

static int
ahci_gen_attach(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int	error;

	ctlr->r_rid = 0;
	ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &ctlr->r_rid,
	    RF_ACTIVE);
	if (ctlr->r_mem == NULL)
		return (ENXIO);

	/* Setup controller defaults. */
	ctlr->numirqs = 1;

	/* Reset controller */
	if ((error = ahci_gen_ctlr_reset(dev)) == 0)
		error = ahci_attach(dev);

	if (error != 0) {
		if (ctlr->r_mem != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid,
			    ctlr->r_mem);
	}
	return error;
}

static int
ahci_gen_detach(device_t dev)
{

	ahci_detach(dev);
	return (0);
}

#ifdef FDT
static devclass_t ahci_gen_fdt_devclass;
static device_method_t ahci_fdt_methods[] = {
	DEVMETHOD(device_probe,     ahci_fdt_probe),
	DEVMETHOD(device_attach,    ahci_gen_attach),
	DEVMETHOD(device_detach,    ahci_gen_detach),
	DEVMETHOD(bus_print_child,  ahci_print_child),
	DEVMETHOD(bus_alloc_resource,       ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,     ahci_release_resource),
	DEVMETHOD(bus_setup_intr,   ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,ahci_teardown_intr),
	DEVMETHOD(bus_child_location_str, ahci_child_location_str),
	DEVMETHOD(bus_get_dma_tag,  ahci_get_dma_tag),
	DEVMETHOD_END
};
static driver_t ahci_fdt_driver = {
	"ahci",
	ahci_fdt_methods,
	sizeof(struct ahci_controller)
};
DRIVER_MODULE(ahci_fdt, simplebus, ahci_fdt_driver, ahci_gen_fdt_devclass,
    NULL, NULL);
#endif

#ifdef DEV_ACPI
static devclass_t ahci_gen_acpi_devclass;
static device_method_t ahci_acpi_methods[] = {
	DEVMETHOD(device_probe,     ahci_acpi_probe),
	DEVMETHOD(device_attach,    ahci_gen_attach),
	DEVMETHOD(device_detach,    ahci_gen_detach),
	DEVMETHOD(bus_print_child,  ahci_print_child),
	DEVMETHOD(bus_alloc_resource,       ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,     ahci_release_resource),
	DEVMETHOD(bus_setup_intr,   ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,ahci_teardown_intr),
	DEVMETHOD(bus_child_location_str, ahci_child_location_str),
	DEVMETHOD(bus_get_dma_tag,  ahci_get_dma_tag),
	DEVMETHOD_END
};
static driver_t ahci_acpi_driver = {
	"ahci",
	ahci_acpi_methods,
	sizeof(struct ahci_controller)
};
DRIVER_MODULE(ahci_acpi, acpi, ahci_acpi_driver, ahci_gen_acpi_devclass,
    NULL, NULL);
#endif
