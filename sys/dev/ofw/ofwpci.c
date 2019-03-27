/*-
 * Copyright (c) 2011 Nathan Whitehorn
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
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofwpci.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "pcib_if.h"

/*
 * If it is necessary to set another value of this for
 * some platforms it should be set at fdt.h file
 */
#ifndef PCI_MAP_INTR
#define	PCI_MAP_INTR	4
#endif

#define	PCI_INTR_PINS	4

/*
 * bus interface.
 */
static struct resource * ofw_pci_alloc_resource(device_t, device_t,
    int, int *, rman_res_t, rman_res_t, rman_res_t, u_int);
static int ofw_pci_release_resource(device_t, device_t, int, int,
    struct resource *);
static int ofw_pci_activate_resource(device_t, device_t, int, int,
    struct resource *);
static int ofw_pci_deactivate_resource(device_t, device_t, int, int,
    struct resource *);
static int ofw_pci_adjust_resource(device_t, device_t, int,
    struct resource *, rman_res_t, rman_res_t);

#ifdef __powerpc__
static bus_space_tag_t ofw_pci_bus_get_bus_tag(device_t, device_t);
#endif

/*
 * pcib interface
 */
static int ofw_pci_maxslots(device_t);

/*
 * ofw_bus interface
 */
static phandle_t ofw_pci_get_node(device_t, device_t);

/*
 * local methods
 */
static int ofw_pci_fill_ranges(phandle_t, struct ofw_pci_range *);
static struct rman *ofw_pci_get_rman(struct ofw_pci_softc *, int, u_int);

/*
 * Driver methods.
 */
static device_method_t	ofw_pci_methods[] = {

	/* Device interface */
	DEVMETHOD(device_attach,	ofw_pci_attach),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_read_ivar,	ofw_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	ofw_pci_write_ivar),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	ofw_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	ofw_pci_release_resource),
	DEVMETHOD(bus_activate_resource,	ofw_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	ofw_pci_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	ofw_pci_adjust_resource),
#ifdef __powerpc__
	DEVMETHOD(bus_get_bus_tag,	ofw_pci_bus_get_bus_tag),
#endif

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	ofw_pci_maxslots),
	DEVMETHOD(pcib_route_interrupt,	ofw_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pci_get_node),

	DEVMETHOD_END
};

DEFINE_CLASS_0(ofw_pci, ofw_pci_driver, ofw_pci_methods, 0);

int
ofw_pci_init(device_t dev)
{
	struct ofw_pci_softc *sc;
	phandle_t node;
	u_int32_t busrange[2];
	struct ofw_pci_range *rp;
	int i, error;
	struct ofw_pci_cell_info *cell_info;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	sc->sc_initialized = 1;
	sc->sc_range = NULL;
	sc->sc_pci_domain = device_get_unit(dev);

	cell_info = (struct ofw_pci_cell_info *)malloc(sizeof(*cell_info),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_cell_info = cell_info;

	if (OF_getencprop(node, "bus-range", busrange, sizeof(busrange)) != 8)
		busrange[0] = 0;

	sc->sc_dev = dev;
	sc->sc_node = node;
	sc->sc_bus = busrange[0];

	if (sc->sc_quirks & OFW_PCI_QUIRK_RANGES_ON_CHILDREN) {
		phandle_t c;
		int n, i;

		sc->sc_nrange = 0;
		for (c = OF_child(node); c != 0; c = OF_peer(c)) {
			n = ofw_pci_nranges(c, cell_info);
			if (n > 0)
				sc->sc_nrange += n;
		}
		if (sc->sc_nrange == 0) {
			error = ENXIO;
			goto out;
		}
		sc->sc_range = malloc(sc->sc_nrange * sizeof(sc->sc_range[0]),
		    M_DEVBUF, M_WAITOK);
		i = 0;
		for (c = OF_child(node); c != 0; c = OF_peer(c)) {
			n = ofw_pci_fill_ranges(c, &sc->sc_range[i]);
			if (n > 0)
				i += n;
		}
		KASSERT(i == sc->sc_nrange, ("range count mismatch"));
	} else {
		sc->sc_nrange = ofw_pci_nranges(node, cell_info);
		if (sc->sc_nrange <= 0) {
			device_printf(dev, "could not getranges\n");
			error = ENXIO;
			goto out;
		}
		sc->sc_range = malloc(sc->sc_nrange * sizeof(sc->sc_range[0]),
		    M_DEVBUF, M_WAITOK);
		ofw_pci_fill_ranges(node, sc->sc_range);
	}

	sc->sc_io_rman.rm_type = RMAN_ARRAY;
	sc->sc_io_rman.rm_descr = "PCI I/O Ports";
	error = rman_init(&sc->sc_io_rman);
	if (error != 0) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		goto out;
	}

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "PCI Non Prefetchable Memory";
	error = rman_init(&sc->sc_mem_rman);
	if (error != 0) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		goto out;
	}

	sc->sc_pmem_rman.rm_type = RMAN_ARRAY;
	sc->sc_pmem_rman.rm_descr = "PCI Prefetchable Memory";
	error = rman_init(&sc->sc_pmem_rman);
	if (error != 0) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		goto out;
	}

	for (i = 0; i < sc->sc_nrange; i++) {
		error = 0;
		rp = sc->sc_range + i;

		if (sc->sc_range_mask & ((uint64_t)1 << i))
			continue;
		switch (rp->pci_hi & OFW_PCI_PHYS_HI_SPACEMASK) {
		case OFW_PCI_PHYS_HI_SPACE_CONFIG:
			break;
		case OFW_PCI_PHYS_HI_SPACE_IO:
			error = rman_manage_region(&sc->sc_io_rman, rp->pci,
			    rp->pci + rp->size - 1);
			break;
		case OFW_PCI_PHYS_HI_SPACE_MEM32:
		case OFW_PCI_PHYS_HI_SPACE_MEM64:
			if (rp->pci_hi & OFW_PCI_PHYS_HI_PREFETCHABLE) {
				sc->sc_have_pmem = 1;
				error = rman_manage_region(&sc->sc_pmem_rman,
				    rp->pci, rp->pci + rp->size - 1);
			} else {
				error = rman_manage_region(&sc->sc_mem_rman,
				    rp->pci, rp->pci + rp->size - 1);
			}
			break;
		}

		if (error != 0) {
			device_printf(dev,
			    "rman_manage_region(%x, %#jx, %#jx) failed. "
			    "error = %d\n", rp->pci_hi &
			    OFW_PCI_PHYS_HI_SPACEMASK, rp->pci,
			    rp->pci + rp->size - 1, error);
			goto out;
		}
	}

	ofw_bus_setup_iinfo(node, &sc->sc_pci_iinfo, sizeof(cell_t));
	return (0);

out:
	free(cell_info, M_DEVBUF);
	free(sc->sc_range, M_DEVBUF);
	rman_fini(&sc->sc_io_rman);
	rman_fini(&sc->sc_mem_rman);
	rman_fini(&sc->sc_pmem_rman);

	return (error);
}

int
ofw_pci_attach(device_t dev)
{
	struct ofw_pci_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (!sc->sc_initialized) {
		error = ofw_pci_init(dev);
		if (error != 0)
			return (error);
	}

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
ofw_pci_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

int
ofw_pci_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct ofw_pci_softc *sc;
	struct ofw_pci_register reg;
	uint32_t pintr, mintr[PCI_MAP_INTR];
	int intrcells;
	phandle_t iparent;

	sc = device_get_softc(bus);
	pintr = pin;

	/* Fabricate imap information in case this isn't an OFW device */
	bzero(&reg, sizeof(reg));
	reg.phys_hi = (pci_get_bus(dev) << OFW_PCI_PHYS_HI_BUSSHIFT) |
	    (pci_get_slot(dev) << OFW_PCI_PHYS_HI_DEVICESHIFT) |
	    (pci_get_function(dev) << OFW_PCI_PHYS_HI_FUNCTIONSHIFT);

	intrcells = ofw_bus_lookup_imap(ofw_bus_get_node(dev),
	    &sc->sc_pci_iinfo, &reg, sizeof(reg), &pintr, sizeof(pintr),
	    mintr, sizeof(mintr), &iparent);
	if (intrcells != 0) {
		pintr = ofw_bus_map_intr(dev, iparent, intrcells, mintr);
		return (pintr);
	}

	/*
	 * Maybe it's a real interrupt, not an intpin
	 */
	if (pin > PCI_INTR_PINS)
		return (pin);

	device_printf(bus, "could not route pin %d for device %d.%d\n",
	    pin, pci_get_slot(dev), pci_get_function(dev));
	return (PCI_INVALID_IRQ);
}

int
ofw_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct ofw_pci_softc *sc;

	sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = sc->sc_pci_domain;
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->sc_bus;
		return (0);
	default:
		break;
	}

	return (ENOENT);
}

int
ofw_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct ofw_pci_softc *sc;

	sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->sc_bus = value;
		return (0);
	default:
		break;
	}

	return (ENOENT);
}

int
ofw_pci_nranges(phandle_t node, struct ofw_pci_cell_info *info)
{
	ssize_t nbase_ranges;

	if (info == NULL)
		return (-1);

	info->host_address_cells = 1;
	info->size_cells = 2;
	info->pci_address_cell = 3;

	OF_getencprop(OF_parent(node), "#address-cells",
	    &(info->host_address_cells), sizeof(info->host_address_cells));
	OF_getencprop(node, "#address-cells",
	    &(info->pci_address_cell), sizeof(info->pci_address_cell));
	OF_getencprop(node, "#size-cells", &(info->size_cells),
	    sizeof(info->size_cells));

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges <= 0)
		return (-1);

	return (nbase_ranges / sizeof(cell_t) /
	    (info->pci_address_cell + info->host_address_cells +
	    info->size_cells));
}

static struct resource *
ofw_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ofw_pci_softc *sc;
	struct resource *rv;
	struct rman *rm;
	int needactivate;


	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	sc = device_get_softc(bus);

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type ==  PCI_RES_BUS) {
		  return (pci_domain_alloc_bus(sc->sc_pci_domain, child, rid,
		      start, end, count, flags | needactivate));
	}
#endif

	rm = ofw_pci_get_rman(sc, type, flags);
	if (rm == NULL)  {
		return (bus_generic_alloc_resource(bus, child, type, rid,
		    start, end, count, flags | needactivate));
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL) {
		device_printf(bus, "failed to reserve resource for %s\n",
		    device_get_nameunit(child));
		return (NULL);
	}

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv) != 0) {
			device_printf(bus,
			    "failed to activate resource for %s\n",
			    device_get_nameunit(child));
			rman_release_resource(rv);
			return (NULL);
		}
	}

	return (rv);
}

static int
ofw_pci_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct ofw_pci_softc *sc;
	struct rman *rm;
	int error;

	sc = device_get_softc(bus);

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(sc->sc_pci_domain, child, rid,
		    res));
#endif

	rm = ofw_pci_get_rman(sc, type, rman_get_flags(res));
	if (rm == NULL) {
		return (bus_generic_release_resource(bus, child, type, rid,
		    res));
	}
	KASSERT(rman_is_region_manager(res, rm), ("rman mismatch"));

	if (rman_get_flags(res) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, res);
		if (error != 0)
			return (error);
	}
	return (rman_release_resource(res));
}

static int
ofw_pci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct ofw_pci_softc *sc;
	bus_space_handle_t handle;
	bus_space_tag_t tag;
	struct ofw_pci_range *rp;
	vm_paddr_t start;
	int space;
	int rv;

	sc = device_get_softc(bus);

	if (type != SYS_RES_IOPORT && type != SYS_RES_MEMORY) {
		return (bus_generic_activate_resource(bus, child, type, rid,
		    res));
	}

	start = (vm_paddr_t)rman_get_start(res);

	/*
	 * Map this through the ranges list
	 */
	for (rp = sc->sc_range; rp < sc->sc_range + sc->sc_nrange &&
	    rp->pci_hi != 0; rp++) {
		if (start < rp->pci || start >= rp->pci + rp->size)
			continue;

		switch (rp->pci_hi & OFW_PCI_PHYS_HI_SPACEMASK) {
		case OFW_PCI_PHYS_HI_SPACE_IO:
			space = SYS_RES_IOPORT;
			break;
		case OFW_PCI_PHYS_HI_SPACE_MEM32:
		case OFW_PCI_PHYS_HI_SPACE_MEM64:
			space = SYS_RES_MEMORY;
			break;
		default:
			space = -1;
			}

		if (type == space) {
			start += (rp->host - rp->pci);
			break;
		}
	}

	if (bootverbose)
		printf("ofw_pci mapdev: start %jx, len %jd\n",
		    (rman_res_t)start, rman_get_size(res));

	tag = BUS_GET_BUS_TAG(child, child);
	if (tag == NULL)
		return (ENOMEM);

	rman_set_bustag(res, tag);
	rv = bus_space_map(tag, start,
	    rman_get_size(res), 0, &handle);
	if (rv != 0)
		return (ENOMEM);

	rman_set_bushandle(res, handle);
	rman_set_virtual(res, (void *)handle); /* XXX  for powerpc only ? */

	return (rman_activate_resource(res));
}

#ifdef __powerpc__
static bus_space_tag_t
ofw_pci_bus_get_bus_tag(device_t bus, device_t child)
{

	return (&bs_le_tag);
}
#endif

static int
ofw_pci_deactivate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	vm_size_t psize;

	if (type != SYS_RES_IOPORT && type != SYS_RES_MEMORY) {
		return (bus_generic_deactivate_resource(bus, child, type, rid,
		    res));
	}

	psize = rman_get_size(res);
	pmap_unmapdev((vm_offset_t)rman_get_virtual(res), psize);

	return (rman_deactivate_resource(res));
}

static int
ofw_pci_adjust_resource(device_t bus, device_t child, int type,
    struct resource *res, rman_res_t start, rman_res_t end)
{
	struct rman *rm;
	struct ofw_pci_softc *sc;

	sc = device_get_softc(bus);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS)
		return (pci_domain_adjust_bus(sc->sc_pci_domain, child, res,
		    start, end));
#endif

	rm = ofw_pci_get_rman(sc, type, rman_get_flags(res));
	if (rm == NULL) {
		return (bus_generic_adjust_resource(bus, child, type, res,
		    start, end));
	}
	KASSERT(rman_is_region_manager(res, rm), ("rman mismatch"));
	KASSERT(!(rman_get_flags(res) & RF_ACTIVE),
	    ("active resources cannot be adjusted"));

	return (rman_adjust_resource(res, start, end));
}

static phandle_t
ofw_pci_get_node(device_t bus, device_t dev)
{
	struct ofw_pci_softc *sc;

	sc = device_get_softc(bus);
	/* We only have one child, the PCI bus, which needs our own node. */

	return (sc->sc_node);
}

static int
ofw_pci_fill_ranges(phandle_t node, struct ofw_pci_range *ranges)
{
	int host_address_cells = 1, pci_address_cells = 3, size_cells = 2;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int nranges;
	int i, j, k;

	OF_getencprop(OF_parent(node), "#address-cells", &host_address_cells,
	    sizeof(host_address_cells));
	OF_getencprop(node, "#address-cells", &pci_address_cells,
	    sizeof(pci_address_cells));
	OF_getencprop(node, "#size-cells", &size_cells, sizeof(size_cells));

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges <= 0)
		return (-1);
	nranges = nbase_ranges / sizeof(cell_t) /
	    (pci_address_cells + host_address_cells + size_cells);

	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < nranges; i++) {
		ranges[i].pci_hi = base_ranges[j++];
		ranges[i].pci = 0;
		for (k = 0; k < pci_address_cells - 1; k++) {
			ranges[i].pci <<= 32;
			ranges[i].pci |= base_ranges[j++];
		}
		ranges[i].host = 0;
		for (k = 0; k < host_address_cells; k++) {
			ranges[i].host <<= 32;
			ranges[i].host |= base_ranges[j++];
		}
		ranges[i].size = 0;
		for (k = 0; k < size_cells; k++) {
			ranges[i].size <<= 32;
			ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_DEVBUF);
	return (nranges);
}

static struct rman *
ofw_pci_get_rman(struct ofw_pci_softc *sc, int type, u_int flags)
{

	switch (type) {
	case SYS_RES_IOPORT:
		return (&sc->sc_io_rman);
	case SYS_RES_MEMORY:
		if (sc->sc_have_pmem  && (flags & RF_PREFETCHABLE))
			return (&sc->sc_pmem_rman);
		else
			return (&sc->sc_mem_rman);
	default:
		break;
	}

	return (NULL);
}
