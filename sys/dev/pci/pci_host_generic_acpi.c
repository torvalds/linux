/*-
 * Copyright (C) 2018 Cavium Inc.
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
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

/* Generic ECAM PCIe driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/cpuset.h>
#include <sys/rwlock.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_pcibvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>
#include <dev/pci/pci_host_generic.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include "pcib_if.h"
#include "acpi_bus_if.h"

/* Assembling ECAM Configuration Address */
#define	PCIE_BUS_SHIFT		20
#define	PCIE_SLOT_SHIFT		15
#define	PCIE_FUNC_SHIFT		12
#define	PCIE_BUS_MASK		0xFF
#define	PCIE_SLOT_MASK		0x1F
#define	PCIE_FUNC_MASK		0x07
#define	PCIE_REG_MASK		0xFFF

#define	PCIE_ADDR_OFFSET(bus, slot, func, reg)			\
	((((bus) & PCIE_BUS_MASK) << PCIE_BUS_SHIFT)	|	\
	(((slot) & PCIE_SLOT_MASK) << PCIE_SLOT_SHIFT)	|	\
	(((func) & PCIE_FUNC_MASK) << PCIE_FUNC_SHIFT)	|	\
	((reg) & PCIE_REG_MASK))

#define	PCI_IO_WINDOW_OFFSET	0x1000

#define	SPACE_CODE_SHIFT	24
#define	SPACE_CODE_MASK		0x3
#define	SPACE_CODE_IO_SPACE	0x1
#define	PROPS_CELL_SIZE		1
#define	PCI_ADDR_CELL_SIZE	2

struct generic_pcie_acpi_softc {
	struct generic_pcie_core_softc base;
	ACPI_BUFFER		ap_prt;		/* interrupt routing table */
};

/* Forward prototypes */

static int generic_pcie_acpi_probe(device_t dev);
static ACPI_STATUS pci_host_generic_acpi_parse_resource(ACPI_RESOURCE *, void *);
static int generic_pcie_acpi_read_ivar(device_t, device_t, int, uintptr_t *);

/*
 * generic_pcie_acpi_probe - look for root bridge flag
 */
static int
generic_pcie_acpi_probe(device_t dev)
{
	ACPI_DEVICE_INFO *devinfo;
	ACPI_HANDLE h;
	int root;

	if (acpi_disabled("pcib") || (h = acpi_get_handle(dev)) == NULL ||
	    ACPI_FAILURE(AcpiGetObjectInfo(h, &devinfo)))
		return (ENXIO);
	root = (devinfo->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;
	AcpiOsFree(devinfo);
	if (!root)
		return (ENXIO);

	device_set_desc(dev, "Generic PCI host controller");
	return (BUS_PROBE_GENERIC);
}

/*
 * pci_host_generic_acpi_parse_resource - parse PCI memory, IO and bus spaces
 * 'produced' by this bridge
 */
static ACPI_STATUS
pci_host_generic_acpi_parse_resource(ACPI_RESOURCE *res, void *arg)
{
	device_t dev = (device_t)arg;
	struct generic_pcie_acpi_softc *sc;
	struct rman *rm;
	rman_res_t min, max, off;
	int r;

	rm = NULL;
	sc = device_get_softc(dev);
	r = sc->base.nranges;
	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_ADDRESS16:
		min = res->Data.Address16.Address.Minimum;
		max = res->Data.Address16.Address.Maximum;
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		min = res->Data.Address32.Address.Minimum;
		max = res->Data.Address32.Address.Maximum;
		off = res->Data.Address32.Address.TranslationOffset;
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		if (res->Data.Address.ResourceType != ACPI_MEMORY_RANGE)
			break;
		min = res->Data.Address64.Address.Minimum;
		max = res->Data.Address64.Address.Maximum;
		off = res->Data.Address64.Address.TranslationOffset;
		break;
	default:
		return (AE_OK);
	}

	/* Save detected ranges */
	if (res->Data.Address.ResourceType == ACPI_MEMORY_RANGE ||
	    res->Data.Address.ResourceType == ACPI_IO_RANGE) {
		sc->base.ranges[r].pci_base = min;
		sc->base.ranges[r].phys_base = min + off;
		sc->base.ranges[r].size = max - min + 1;
		if (res->Data.Address.ResourceType == ACPI_MEMORY_RANGE)
			sc->base.ranges[r].flags |= FLAG_MEM;
		else if (res->Data.Address.ResourceType == ACPI_IO_RANGE)
			sc->base.ranges[r].flags |= FLAG_IO;
		sc->base.nranges++;
	} else if (res->Data.Address.ResourceType == ACPI_BUS_NUMBER_RANGE) {
		sc->base.bus_start = min;
		sc->base.bus_end = max;
	}
	return (AE_OK);
}

static int
pci_host_acpi_get_ecam_resource(device_t dev)
{
	struct generic_pcie_acpi_softc *sc;
	struct acpi_device *ad;
	struct resource_list *rl;
	ACPI_TABLE_HEADER *hdr;
	ACPI_MCFG_ALLOCATION *mcfg_entry, *mcfg_end;
	ACPI_HANDLE handle;
	ACPI_STATUS status;
	rman_res_t base, start, end;
	int found, val;

	sc = device_get_softc(dev);
	handle = acpi_get_handle(dev);

	/* Try MCFG first */
	status = AcpiGetTable(ACPI_SIG_MCFG, 1, &hdr);
	if (ACPI_SUCCESS(status)) {
		found = FALSE;
		mcfg_end = (ACPI_MCFG_ALLOCATION *)((char *)hdr + hdr->Length);
		mcfg_entry = (ACPI_MCFG_ALLOCATION *)((ACPI_TABLE_MCFG *)hdr + 1);
		while (mcfg_entry < mcfg_end && !found) {
			if (mcfg_entry->PciSegment == sc->base.ecam &&
			    mcfg_entry->StartBusNumber <= sc->base.bus_start &&
			    mcfg_entry->EndBusNumber >= sc->base.bus_start)
				found = TRUE;
			else
				mcfg_entry++;
		}
		if (found) {
			if (mcfg_entry->EndBusNumber < sc->base.bus_end) {
				device_printf(dev, "bus end mismatch! expected %d found %d.\n",
				    sc->base.bus_end, (int)mcfg_entry->EndBusNumber);
				sc->base.bus_end = mcfg_entry->EndBusNumber;
			}
			base = mcfg_entry->Address;
		} else {
			device_printf(dev, "MCFG exists, but does not have bus %d-%d\n",
			    sc->base.bus_start, sc->base.bus_end);
			return (ENXIO);
		}
	} else {
		status = acpi_GetInteger(handle, "_CBA", &val);
		if (ACPI_SUCCESS(status))
			base = val;
		else
			return (ENXIO);
	}

	/* add as MEM rid 0 */
	ad = device_get_ivars(dev);
	rl = &ad->ad_rl;
	start = base + (sc->base.bus_start << PCIE_BUS_SHIFT);
	end = base + ((sc->base.bus_end + 1) << PCIE_BUS_SHIFT) - 1;
	resource_list_add(rl, SYS_RES_MEMORY, 0, start, end, end - start + 1);
	if (bootverbose)
		device_printf(dev, "ECAM for bus %d-%d at mem %jx-%jx\n",
		    sc->base.bus_start, sc->base.bus_end, start, end);
	return (0);
}

static int
pci_host_generic_acpi_attach(device_t dev)
{
	struct generic_pcie_acpi_softc *sc;
	ACPI_HANDLE handle;
	uint64_t phys_base;
	uint64_t pci_base;
	uint64_t size;
	ACPI_STATUS status;
	int error;
	int tuple;

	sc = device_get_softc(dev);
	handle = acpi_get_handle(dev);

	/* Get Start bus number for the PCI host bus is from _BBN method */
	status = acpi_GetInteger(handle, "_BBN", &sc->base.bus_start);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "No _BBN, using start bus 0\n");
		sc->base.bus_start = 0;
	}
	sc->base.bus_end = 255;

	/* Get PCI Segment (domain) needed for MCFG lookup */
	status = acpi_GetInteger(handle, "_SEG", &sc->base.ecam);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "No _SEG for PCI Bus, using segment 0\n");
		sc->base.ecam = 0;
	}

	/* Bus decode ranges */
	status = AcpiWalkResources(handle, "_CRS",
	    pci_host_generic_acpi_parse_resource, (void *)dev);
	if (ACPI_FAILURE(status))
		return (ENXIO);

	/* Coherency attribute */
	if (ACPI_FAILURE(acpi_GetInteger(handle, "_CCA", &sc->base.coherent)))
		sc->base.coherent = 0;
	if (bootverbose)
		device_printf(dev, "Bus is%s cache-coherent\n",
		    sc->base.coherent ? "" : " not");

	/* add config space resource */
	pci_host_acpi_get_ecam_resource(dev);
	acpi_pcib_fetch_prt(dev, &sc->ap_prt);

	error = pci_host_generic_core_attach(dev);
	if (error != 0)
		return (error);

	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		phys_base = sc->base.ranges[tuple].phys_base;
		pci_base = sc->base.ranges[tuple].pci_base;
		size = sc->base.ranges[tuple].size;
		if (phys_base == 0 || size == 0)
			continue; /* empty range element */
		if (sc->base.ranges[tuple].flags & FLAG_MEM) {
			error = rman_manage_region(&sc->base.mem_rman,
			   phys_base, phys_base + size - 1);
		} else if (sc->base.ranges[tuple].flags & FLAG_IO) {
			error = rman_manage_region(&sc->base.io_rman,
			   pci_base + PCI_IO_WINDOW_OFFSET,
			   pci_base + PCI_IO_WINDOW_OFFSET + size - 1);
		} else
			continue;
		if (error) {
			device_printf(dev, "rman_manage_region() failed."
						"error = %d\n", error);
			rman_fini(&sc->base.mem_rman);
			return (error);
		}
	}

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
generic_pcie_acpi_read_ivar(device_t dev, device_t child, int index,
    uintptr_t *result)
{
	struct generic_pcie_acpi_softc *sc;

	sc = device_get_softc(dev);

	if (index == PCIB_IVAR_BUS) {
		*result = sc->base.bus_start;
		return (0);
	}

	if (index == PCIB_IVAR_DOMAIN) {
		*result = sc->base.ecam;
		return (0);
	}

	if (bootverbose)
		device_printf(dev, "ERROR: Unknown index %d.\n", index);
	return (ENOENT);
}

static int
generic_pcie_acpi_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct generic_pcie_acpi_softc *sc;

	sc = device_get_softc(bus);
	return (acpi_pcib_route_interrupt(bus, dev, pin, &sc->ap_prt));
}

static u_int
generic_pcie_get_xref(device_t pci, device_t child)
{
	struct generic_pcie_acpi_softc *sc;
	uintptr_t rid;
	u_int xref, devid;
	int err;

	sc = device_get_softc(pci);
	err = pcib_get_id(pci, child, PCI_ID_RID, &rid);
	if (err != 0)
		return (ACPI_MSI_XREF);
	err = acpi_iort_map_pci_msi(sc->base.ecam, rid, &xref, &devid);
	if (err != 0)
		return (ACPI_MSI_XREF);
	return (xref);
}

static u_int
generic_pcie_map_id(device_t pci, device_t child, uintptr_t *id)
{
	struct generic_pcie_acpi_softc *sc;
	uintptr_t rid;
	u_int xref, devid;
	int err;

	sc = device_get_softc(pci);
	err = pcib_get_id(pci, child, PCI_ID_RID, &rid);
	if (err != 0)
		return (err);
        err = acpi_iort_map_pci_msi(sc->base.ecam, rid, &xref, &devid);
	if (err == 0)
		*id = devid;
	else
		*id = rid;	/* RID not in IORT, likely FW bug, ignore */
	return (0);
}

static int
generic_pcie_acpi_alloc_msi(device_t pci, device_t child, int count,
    int maxcount, int *irqs)
{

#if defined(INTRNG)
	return (intr_alloc_msi(pci, child, generic_pcie_get_xref(pci, child),
	    count, maxcount, irqs));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_acpi_release_msi(device_t pci, device_t child, int count,
    int *irqs)
{

#if defined(INTRNG)
	return (intr_release_msi(pci, child, generic_pcie_get_xref(pci, child),
	    count, irqs));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_acpi_map_msi(device_t pci, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{

#if defined(INTRNG)
	return (intr_map_msi(pci, child, generic_pcie_get_xref(pci, child), irq,
	    addr, data));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_acpi_alloc_msix(device_t pci, device_t child, int *irq)
{

#if defined(INTRNG)
	return (intr_alloc_msix(pci, child, generic_pcie_get_xref(pci, child),
	    irq));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_acpi_release_msix(device_t pci, device_t child, int irq)
{

#if defined(INTRNG)
	return (intr_release_msix(pci, child, generic_pcie_get_xref(pci, child),
	    irq));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_acpi_get_id(device_t pci, device_t child, enum pci_id_type type,
    uintptr_t *id)
{

	if (type == PCI_ID_MSI)
		return (generic_pcie_map_id(pci, child, id));
	else
		return (pcib_get_id(pci, child, type, id));
}

static device_method_t generic_pcie_acpi_methods[] = {
	DEVMETHOD(device_probe,		generic_pcie_acpi_probe),
	DEVMETHOD(device_attach,	pci_host_generic_acpi_attach),
	DEVMETHOD(bus_read_ivar,	generic_pcie_acpi_read_ivar),

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt,	generic_pcie_acpi_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	generic_pcie_acpi_alloc_msi),
	DEVMETHOD(pcib_release_msi,	generic_pcie_acpi_release_msi),
	DEVMETHOD(pcib_alloc_msix,	generic_pcie_acpi_alloc_msix),
	DEVMETHOD(pcib_release_msix,	generic_pcie_acpi_release_msix),
	DEVMETHOD(pcib_map_msi,		generic_pcie_acpi_map_msi),
	DEVMETHOD(pcib_get_id,		generic_pcie_acpi_get_id),

	DEVMETHOD_END
};

DEFINE_CLASS_1(pcib, generic_pcie_acpi_driver, generic_pcie_acpi_methods,
    sizeof(struct generic_pcie_acpi_softc), generic_pcie_core_driver);

static devclass_t generic_pcie_acpi_devclass;

DRIVER_MODULE(pcib, acpi, generic_pcie_acpi_driver, generic_pcie_acpi_devclass,
    0, 0);
