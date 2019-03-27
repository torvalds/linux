/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014,2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * the sponsorship of the FreeBSD Foundation.
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

/* Generic ECAM PCIe driver FDT attachment */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#if defined(INTRNG)
#include <machine/intr.h>
#endif

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>
#include <dev/pci/pci_host_generic.h>
#include <dev/pci/pci_host_generic_fdt.h>

#include <machine/intr.h>

#include "pcib_if.h"

#define	PCI_IO_WINDOW_OFFSET	0x1000

#define	SPACE_CODE_SHIFT	24
#define	SPACE_CODE_MASK		0x3
#define	SPACE_CODE_IO_SPACE	0x1
#define	PROPS_CELL_SIZE		1
#define	PCI_ADDR_CELL_SIZE	2

/* OFW bus interface */
struct generic_pcie_ofw_devinfo {
	struct ofw_bus_devinfo	di_dinfo;
	struct resource_list	di_rl;
};

/* Forward prototypes */

static int generic_pcie_fdt_probe(device_t dev);
static int parse_pci_mem_ranges(device_t, struct generic_pcie_core_softc *);
static int generic_pcie_fdt_release_resource(device_t dev, device_t child,
    int type, int rid, struct resource *res);
static int generic_pcie_ofw_bus_attach(device_t);
static const struct ofw_bus_devinfo *generic_pcie_ofw_get_devinfo(device_t,
    device_t);

static __inline void
get_addr_size_cells(phandle_t node, pcell_t *addr_cells, pcell_t *size_cells)
{

	*addr_cells = 2;
	/* Find address cells if present */
	OF_getencprop(node, "#address-cells", addr_cells, sizeof(*addr_cells));

	*size_cells = 2;
	/* Find size cells if present */
	OF_getencprop(node, "#size-cells", size_cells, sizeof(*size_cells));
}

static int
generic_pcie_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "pci-host-ecam-generic")) {
		device_set_desc(dev, "Generic PCI host controller");
		return (BUS_PROBE_GENERIC);
	}
	if (ofw_bus_is_compatible(dev, "arm,gem5_pcie")) {
		device_set_desc(dev, "GEM5 PCIe host controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

int
pci_host_generic_attach(device_t dev)
{
	struct generic_pcie_fdt_softc *sc;
	uint64_t phys_base;
	uint64_t pci_base;
	uint64_t size;
	phandle_t node;
	int error;
	int tuple;

	sc = device_get_softc(dev);

	/* Retrieve 'ranges' property from FDT */
	if (bootverbose)
		device_printf(dev, "parsing FDT for ECAM%d:\n", sc->base.ecam);
	if (parse_pci_mem_ranges(dev, &sc->base))
		return (ENXIO);

	/* Attach OFW bus */
	if (generic_pcie_ofw_bus_attach(dev) != 0)
		return (ENXIO);

	node = ofw_bus_get_node(dev);
	if (sc->base.coherent == 0) {
		sc->base.coherent = OF_hasprop(node, "dma-coherent");
	}
	if (bootverbose)
		device_printf(dev, "Bus is%s cache-coherent\n",
		    sc->base.coherent ? "" : " not");

	/* TODO parse FDT bus ranges */
	sc->base.bus_start = 0;
	sc->base.bus_end = 0xFF;
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

	ofw_bus_setup_iinfo(node, &sc->pci_iinfo, sizeof(cell_t));

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
parse_pci_mem_ranges(device_t dev, struct generic_pcie_core_softc *sc)
{
	pcell_t pci_addr_cells, parent_addr_cells;
	pcell_t attributes, size_cells;
	cell_t *base_ranges;
	int nbase_ranges;
	phandle_t node;
	int i, j, k;
	int tuple;

	node = ofw_bus_get_node(dev);

	OF_getencprop(node, "#address-cells", &pci_addr_cells,
					sizeof(pci_addr_cells));
	OF_getencprop(node, "#size-cells", &size_cells,
					sizeof(size_cells));
	OF_getencprop(OF_parent(node), "#address-cells", &parent_addr_cells,
					sizeof(parent_addr_cells));

	if (parent_addr_cells > 2 || pci_addr_cells != 3 || size_cells > 2) {
		device_printf(dev,
		    "Unexpected number of address or size cells in FDT\n");
		return (ENXIO);
	}

	nbase_ranges = OF_getproplen(node, "ranges");
	sc->nranges = nbase_ranges / sizeof(cell_t) /
	    (parent_addr_cells + pci_addr_cells + size_cells);
	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->nranges; i++) {
		attributes = (base_ranges[j++] >> SPACE_CODE_SHIFT) & \
							SPACE_CODE_MASK;
		if (attributes == SPACE_CODE_IO_SPACE) {
			sc->ranges[i].flags |= FLAG_IO;
		} else {
			sc->ranges[i].flags |= FLAG_MEM;
		}

		sc->ranges[i].pci_base = 0;
		for (k = 0; k < (pci_addr_cells - 1); k++) {
			sc->ranges[i].pci_base <<= 32;
			sc->ranges[i].pci_base |= base_ranges[j++];
		}
		sc->ranges[i].phys_base = 0;
		for (k = 0; k < parent_addr_cells; k++) {
			sc->ranges[i].phys_base <<= 32;
			sc->ranges[i].phys_base |= base_ranges[j++];
		}
		sc->ranges[i].size = 0;
		for (k = 0; k < size_cells; k++) {
			sc->ranges[i].size <<= 32;
			sc->ranges[i].size |= base_ranges[j++];
		}
	}

	for (; i < MAX_RANGES_TUPLES; i++) {
		/* zero-fill remaining tuples to mark empty elements in array */
		sc->ranges[i].pci_base = 0;
		sc->ranges[i].phys_base = 0;
		sc->ranges[i].size = 0;
	}

	if (bootverbose) {
		for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
			device_printf(dev,
			    "\tPCI addr: 0x%jx, CPU addr: 0x%jx, Size: 0x%jx\n",
			    sc->ranges[tuple].pci_base,
			    sc->ranges[tuple].phys_base,
			    sc->ranges[tuple].size);
		}
	}

	free(base_ranges, M_DEVBUF);
	return (0);
}

static int
generic_pcie_fdt_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct generic_pcie_fdt_softc *sc;
	struct ofw_pci_register reg;
	uint32_t pintr, mintr[4];
	phandle_t iparent;
	int intrcells;

	sc = device_get_softc(bus);
	pintr = pin;

	bzero(&reg, sizeof(reg));
	reg.phys_hi = (pci_get_bus(dev) << OFW_PCI_PHYS_HI_BUSSHIFT) |
	    (pci_get_slot(dev) << OFW_PCI_PHYS_HI_DEVICESHIFT) |
	    (pci_get_function(dev) << OFW_PCI_PHYS_HI_FUNCTIONSHIFT);

	intrcells = ofw_bus_lookup_imap(ofw_bus_get_node(dev),
	    &sc->pci_iinfo, &reg, sizeof(reg), &pintr, sizeof(pintr),
	    mintr, sizeof(mintr), &iparent);
	if (intrcells) {
		pintr = ofw_bus_map_intr(dev, iparent, intrcells, mintr);
		return (pintr);
	}

	device_printf(bus, "could not route pin %d for device %d.%d\n",
	    pin, pci_get_slot(dev), pci_get_function(dev));
	return (PCI_INVALID_IRQ);
}

static int
generic_pcie_fdt_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *res)
{

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS) {
		return (pci_host_generic_core_release_resource(dev, child, type,
		    rid, res));
	}
#endif

	/* For PCIe devices that do not have FDT nodes, use PCIB method */
	if ((int)ofw_bus_get_node(child) <= 0) {
		return (pci_host_generic_core_release_resource(dev, child, type,
		    rid, res));
	}

	/* For other devices use OFW method */
	return (bus_generic_release_resource(dev, child, type, rid, res));
}

struct resource *
pci_host_generic_alloc_resource(device_t dev, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct generic_pcie_fdt_softc *sc;
	struct generic_pcie_ofw_devinfo *di;
	struct resource_list_entry *rle;
	int i;

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS) {
		return (pci_host_generic_core_alloc_resource(dev, child, type, rid,
		    start, end, count, flags));
	}
#endif

	/* For PCIe devices that do not have FDT nodes, use PCIB method */
	if ((int)ofw_bus_get_node(child) <= 0)
		return (pci_host_generic_core_alloc_resource(dev, child, type,
		    rid, start, end, count, flags));

	/* For other devices use OFW method */
	sc = device_get_softc(dev);

	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		if ((di = device_get_ivars(child)) == NULL)
			return (NULL);
		if (type == SYS_RES_IOPORT)
		    type = SYS_RES_MEMORY;

		/* Find defaults for this rid */
		rle = resource_list_find(&di->di_rl, type, *rid);
		if (rle == NULL)
			return (NULL);

		start = rle->start;
		end = rle->end;
		count = rle->count;
	}

	if (type == SYS_RES_MEMORY) {
		/* Remap through ranges property */
		for (i = 0; i < MAX_RANGES_TUPLES; i++) {
			if (start >= sc->base.ranges[i].phys_base &&
			    end < (sc->base.ranges[i].pci_base +
			    sc->base.ranges[i].size)) {
				start -= sc->base.ranges[i].phys_base;
				start += sc->base.ranges[i].pci_base;
				end -= sc->base.ranges[i].phys_base;
				end += sc->base.ranges[i].pci_base;
				break;
			}
		}

		if (i == MAX_RANGES_TUPLES) {
			device_printf(dev, "Could not map resource "
			    "%#jx-%#jx\n", start, end);
			return (NULL);
		}
	}

	return (bus_generic_alloc_resource(dev, child, type, rid, start,
	    end, count, flags));
}

static int
generic_pcie_fdt_alloc_msi(device_t pci, device_t child, int count,
    int maxcount, int *irqs)
{
#if defined(INTRNG)
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_alloc_msi(pci, child, msi_parent, count, maxcount,
	    irqs));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_fdt_release_msi(device_t pci, device_t child, int count, int *irqs)
{
#if defined(INTRNG)
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_release_msi(pci, child, msi_parent, count, irqs));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_fdt_map_msi(device_t pci, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
#if defined(INTRNG)
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_map_msi(pci, child, msi_parent, irq, addr, data));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_fdt_alloc_msix(device_t pci, device_t child, int *irq)
{
#if defined(INTRNG)
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_alloc_msix(pci, child, msi_parent, irq));
#else
	return (ENXIO);
#endif
}

static int
generic_pcie_fdt_release_msix(device_t pci, device_t child, int irq)
{
#if defined(INTRNG)
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_release_msix(pci, child, msi_parent, irq));
#else
	return (ENXIO);
#endif
}

int
generic_pcie_get_id(device_t pci, device_t child, enum pci_id_type type,
    uintptr_t *id)
{
	phandle_t node;
	int err;
	uint32_t rid;
	uint16_t pci_rid;

	if (type != PCI_ID_MSI)
		return (pcib_get_id(pci, child, type, id));

	node = ofw_bus_get_node(pci);
	pci_rid = pci_get_rid(child);

	err = ofw_bus_msimap(node, pci_rid, NULL, &rid);
	if (err != 0)
		return (err);
	*id = rid;

	return (0);
}

static const struct ofw_bus_devinfo *
generic_pcie_ofw_get_devinfo(device_t bus __unused, device_t child)
{
	struct generic_pcie_ofw_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_dinfo);
}

/* Helper functions */

static int
generic_pcie_ofw_bus_attach(device_t dev)
{
	struct generic_pcie_ofw_devinfo *di;
	device_t child;
	phandle_t parent, node;
	pcell_t addr_cells, size_cells;

	parent = ofw_bus_get_node(dev);
	if (parent > 0) {
		get_addr_size_cells(parent, &addr_cells, &size_cells);
		/* Iterate through all bus subordinates */
		for (node = OF_child(parent); node > 0; node = OF_peer(node)) {

			/* Allocate and populate devinfo. */
			di = malloc(sizeof(*di), M_DEVBUF, M_WAITOK | M_ZERO);
			if (ofw_bus_gen_setup_devinfo(&di->di_dinfo, node) != 0) {
				free(di, M_DEVBUF);
				continue;
			}

			/* Initialize and populate resource list. */
			resource_list_init(&di->di_rl);
			ofw_bus_reg_to_rl(dev, node, addr_cells, size_cells,
			    &di->di_rl);
			ofw_bus_intr_to_rl(dev, node, &di->di_rl, NULL);

			/* Add newbus device for this FDT node */
			child = device_add_child(dev, NULL, -1);
			if (child == NULL) {
				resource_list_free(&di->di_rl);
				ofw_bus_gen_destroy_devinfo(&di->di_dinfo);
				free(di, M_DEVBUF);
				continue;
			}

			device_set_ivars(child, di);
		}
	}

	return (0);
}

static device_method_t generic_pcie_fdt_methods[] = {
	DEVMETHOD(device_probe,		generic_pcie_fdt_probe),
	DEVMETHOD(device_attach,	pci_host_generic_attach),
	DEVMETHOD(bus_alloc_resource,	pci_host_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	generic_pcie_fdt_release_resource),

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt,	generic_pcie_fdt_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	generic_pcie_fdt_alloc_msi),
	DEVMETHOD(pcib_release_msi,	generic_pcie_fdt_release_msi),
	DEVMETHOD(pcib_alloc_msix,	generic_pcie_fdt_alloc_msix),
	DEVMETHOD(pcib_release_msix,	generic_pcie_fdt_release_msix),
	DEVMETHOD(pcib_map_msi,		generic_pcie_fdt_map_msi),
	DEVMETHOD(pcib_get_id,		generic_pcie_get_id),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	generic_pcie_ofw_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

DEFINE_CLASS_1(pcib, generic_pcie_fdt_driver, generic_pcie_fdt_methods,
    sizeof(struct generic_pcie_fdt_softc), generic_pcie_core_driver);

static devclass_t generic_pcie_fdt_devclass;

DRIVER_MODULE(pcib, simplebus, generic_pcie_fdt_driver,
    generic_pcie_fdt_devclass, 0, 0);
DRIVER_MODULE(pcib, ofwbus, generic_pcie_fdt_driver, generic_pcie_fdt_devclass,
    0, 0);
