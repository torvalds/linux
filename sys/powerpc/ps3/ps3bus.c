/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
 * Copyright (C) 2011 glevand (geoffrey.levand@mail.ru)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/cpu.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/platform.h>
#include <machine/resource.h>

#include "ps3bus.h"
#include "ps3-hvcall.h"
#include "iommu_if.h"
#include "clock_if.h"

static void	ps3bus_identify(driver_t *, device_t);
static int	ps3bus_probe(device_t);
static int	ps3bus_attach(device_t);
static int	ps3bus_print_child(device_t dev, device_t child);
static int	ps3bus_read_ivar(device_t bus, device_t child, int which,
		    uintptr_t *result);
static struct resource *ps3bus_alloc_resource(device_t bus, device_t child,
		    int type, int *rid, rman_res_t start, rman_res_t end,
		    rman_res_t count, u_int flags);
static int	ps3bus_activate_resource(device_t bus, device_t child, int type,
		    int rid, struct resource *res);
static bus_dma_tag_t ps3bus_get_dma_tag(device_t dev, device_t child);
static int	ps3_iommu_map(device_t dev, bus_dma_segment_t *segs, int *nsegs,		    bus_addr_t min, bus_addr_t max, bus_size_t alignment,
		    bus_addr_t boundary, void *cookie);
static int	ps3_iommu_unmap(device_t dev, bus_dma_segment_t *segs,
		    int nsegs, void *cookie);
static int	ps3_gettime(device_t dev, struct timespec *ts);
static int	ps3_settime(device_t dev, struct timespec *ts);

struct ps3bus_devinfo {
	int bus;
	int dev;
	uint64_t bustype;
	uint64_t devtype;
	int busidx;
	int devidx;

	struct resource_list resources;
	bus_dma_tag_t dma_tag;

	struct mtx iommu_mtx;
	bus_addr_t dma_base[4];
};

static MALLOC_DEFINE(M_PS3BUS, "ps3bus", "PS3 system bus device information");

enum ps3bus_irq_type {
	SB_IRQ = 2,
	OHCI_IRQ = 3,
	EHCI_IRQ = 4,
};

enum ps3bus_reg_type {
	OHCI_REG = 3,
	EHCI_REG = 4,
};

static device_method_t ps3bus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	ps3bus_identify),
	DEVMETHOD(device_probe,		ps3bus_probe),
	DEVMETHOD(device_attach,	ps3bus_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_get_dma_tag,	ps3bus_get_dma_tag),
	DEVMETHOD(bus_print_child,	ps3bus_print_child),
	DEVMETHOD(bus_read_ivar,	ps3bus_read_ivar),
	DEVMETHOD(bus_alloc_resource,	ps3bus_alloc_resource),
	DEVMETHOD(bus_activate_resource, ps3bus_activate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	/* IOMMU interface */
	DEVMETHOD(iommu_map,		ps3_iommu_map),
	DEVMETHOD(iommu_unmap,		ps3_iommu_unmap),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	ps3_gettime),
	DEVMETHOD(clock_settime,	ps3_settime),

	DEVMETHOD_END
};

struct ps3bus_softc {
	struct rman sc_mem_rman;
	struct rman sc_intr_rman;
	struct mem_region *regions;
	int rcount;
};

static driver_t ps3bus_driver = {
	"ps3bus",
	ps3bus_methods,
	sizeof(struct ps3bus_softc)
};

static devclass_t ps3bus_devclass;

DRIVER_MODULE(ps3bus, nexus, ps3bus_driver, ps3bus_devclass, 0, 0);

static void
ps3bus_identify(driver_t *driver, device_t parent)
{
	if (strcmp(installed_platform(), "ps3") != 0)
		return;

	if (device_find_child(parent, "ps3bus", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "ps3bus", 0);
}

static int 
ps3bus_probe(device_t dev) 
{
	/* Do not attach to any OF nodes that may be present */
	
	device_set_desc(dev, "Playstation 3 System Bus");

	return (BUS_PROBE_NOWILDCARD);
}

static void
ps3bus_resources_init(struct rman *rm, int bus_index, int dev_index,
    struct ps3bus_devinfo *dinfo)
{
	uint64_t irq_type, irq, outlet;
	uint64_t reg_type, paddr, len;
	uint64_t ppe, junk;
	int i, result;
	int thread;

	resource_list_init(&dinfo->resources);

	lv1_get_logical_ppe_id(&ppe);
	thread = 32 - fls(mfctrl());

	/* Scan for interrupts */
	for (i = 0; i < 10; i++) {
		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("intr") | i, 0, &irq_type, &irq);

		if (result != 0)
			break;

		switch (irq_type) {
		case SB_IRQ:
			lv1_construct_event_receive_port(&outlet);
			lv1_connect_irq_plug_ext(ppe, thread, outlet, outlet,
			    0);
			lv1_connect_interrupt_event_receive_port(dinfo->bus,
			    dinfo->dev, outlet, irq);
			break;
		case OHCI_IRQ:
		case EHCI_IRQ:
			lv1_construct_io_irq_outlet(irq, &outlet);
			lv1_connect_irq_plug_ext(ppe, thread, outlet, outlet,
			    0);
			break;
		default:
			printf("Unknown IRQ type %ld for device %d.%d\n",
			    irq_type, dinfo->bus, dinfo->dev);
			break;
		}

		resource_list_add(&dinfo->resources, SYS_RES_IRQ, i,
		    outlet, outlet, 1);
	}

	/* Scan for registers */
	for (i = 0; i < 10; i++) {
		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("reg") | i, 
		    lv1_repository_string("type"), &reg_type, &junk);

		if (result != 0)
			break;

		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("reg") | i, 
		    lv1_repository_string("data"), &paddr, &len);

		result = lv1_map_device_mmio_region(dinfo->bus, dinfo->dev,
		    paddr, len, 12 /* log_2(4 KB) */, &paddr);

		if (result != 0) {
			printf("Mapping registers failed for device "
			    "%d.%d (%ld.%ld): %d\n", dinfo->bus, dinfo->dev,
			    dinfo->bustype, dinfo->devtype, result);
			continue;
		}

		rman_manage_region(rm, paddr, paddr + len - 1);
		resource_list_add(&dinfo->resources, SYS_RES_MEMORY, i,
		    paddr, paddr + len, len);
	}
}

static void
ps3bus_resources_init_by_type(struct rman *rm, int bus_index, int dev_index,
    uint64_t irq_type, uint64_t reg_type, struct ps3bus_devinfo *dinfo)
{
	uint64_t _irq_type, irq, outlet;
	uint64_t _reg_type, paddr, len;
	uint64_t ppe, junk;
	int i, result;
	int thread;

	resource_list_init(&dinfo->resources);

	lv1_get_logical_ppe_id(&ppe);
	thread = 32 - fls(mfctrl());

	/* Scan for interrupts */
	for (i = 0; i < 10; i++) {
		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("intr") | i, 0, &_irq_type, &irq);

		if (result != 0)
			break;

		if (_irq_type != irq_type)
			continue;

		lv1_construct_io_irq_outlet(irq, &outlet);
		lv1_connect_irq_plug_ext(ppe, thread, outlet, outlet,
		    0);
		resource_list_add(&dinfo->resources, SYS_RES_IRQ, i,
		    outlet, outlet, 1);
	}

	/* Scan for registers */
	for (i = 0; i < 10; i++) {
		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("reg") | i, 
		    lv1_repository_string("type"), &_reg_type, &junk);

		if (result != 0)
			break;

		if (_reg_type != reg_type)
			continue;

		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("reg") | i, 
		    lv1_repository_string("data"), &paddr, &len);

		result = lv1_map_device_mmio_region(dinfo->bus, dinfo->dev,
		    paddr, len, 12 /* log_2(4 KB) */, &paddr);

		if (result != 0) {
			printf("Mapping registers failed for device "
			    "%d.%d (%ld.%ld): %d\n", dinfo->bus, dinfo->dev,
			    dinfo->bustype, dinfo->devtype, result);
			break;
		}

		rman_manage_region(rm, paddr, paddr + len - 1);
		resource_list_add(&dinfo->resources, SYS_RES_MEMORY, i,
		    paddr, paddr + len, len);
	}
}

static int 
ps3bus_attach(device_t self) 
{
	struct ps3bus_softc *sc;
	struct ps3bus_devinfo *dinfo;
	int bus_index, dev_index, result;
	uint64_t bustype, bus, devs;
	uint64_t dev, devtype;
	uint64_t junk;
	device_t cdev;

	sc = device_get_softc(self);
	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "PS3Bus Memory Mapped I/O";
	sc->sc_intr_rman.rm_type = RMAN_ARRAY;
	sc->sc_intr_rman.rm_descr = "PS3Bus Interrupts";
	rman_init(&sc->sc_mem_rman);
	rman_init(&sc->sc_intr_rman);
	rman_manage_region(&sc->sc_intr_rman, 0, ~0);

	/* Get memory regions for DMA */
	mem_regions(&sc->regions, &sc->rcount, NULL, NULL);

	/*
	 * Probe all the PS3's buses.
	 */

	for (bus_index = 0; bus_index < 5; bus_index++) {
		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("type"), 0, 0, &bustype, &junk);

		if (result != 0)
			continue;

		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("id"), 0, 0, &bus, &junk);

		if (result != 0)
			continue;

		result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("num_dev"), 0, 0, &devs, &junk);

		for (dev_index = 0; dev_index < devs; dev_index++) {
			result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
			    (lv1_repository_string("bus") >> 32) | bus_index,
			    lv1_repository_string("dev") | dev_index,
			    lv1_repository_string("type"), 0, &devtype, &junk);

			if (result != 0)
				continue;

			result = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
			    (lv1_repository_string("bus") >> 32) | bus_index,
			    lv1_repository_string("dev") | dev_index,
			    lv1_repository_string("id"), 0, &dev, &junk);

			if (result != 0)
				continue;
			
			switch (devtype) {
			case PS3_DEVTYPE_USB:
				/* USB device has OHCI and EHCI USB host controllers */

				lv1_open_device(bus, dev, 0);

				/* OHCI host controller */

				dinfo = malloc(sizeof(*dinfo), M_PS3BUS,
				    M_WAITOK | M_ZERO);

				dinfo->bus = bus;
				dinfo->dev = dev;
				dinfo->bustype = bustype;
				dinfo->devtype = devtype;
				dinfo->busidx = bus_index;
				dinfo->devidx = dev_index;

				ps3bus_resources_init_by_type(&sc->sc_mem_rman, bus_index,
				    dev_index, OHCI_IRQ, OHCI_REG, dinfo);

				cdev = device_add_child(self, "ohci", -1);
				if (cdev == NULL) {
					device_printf(self,
					    "device_add_child failed\n");
					free(dinfo, M_PS3BUS);
					continue;
				}

				mtx_init(&dinfo->iommu_mtx, "iommu", NULL, MTX_DEF);
				device_set_ivars(cdev, dinfo);

				/* EHCI host controller */

				dinfo = malloc(sizeof(*dinfo), M_PS3BUS,
				    M_WAITOK | M_ZERO);

				dinfo->bus = bus;
				dinfo->dev = dev;
				dinfo->bustype = bustype;
				dinfo->devtype = devtype;
				dinfo->busidx = bus_index;
				dinfo->devidx = dev_index;

				ps3bus_resources_init_by_type(&sc->sc_mem_rman, bus_index,
				    dev_index, EHCI_IRQ, EHCI_REG, dinfo);

				cdev = device_add_child(self, "ehci", -1);
				if (cdev == NULL) {
					device_printf(self,
					    "device_add_child failed\n");
					free(dinfo, M_PS3BUS);
					continue;
				}

				mtx_init(&dinfo->iommu_mtx, "iommu", NULL, MTX_DEF);
				device_set_ivars(cdev, dinfo);
				break;
			default:
				dinfo = malloc(sizeof(*dinfo), M_PS3BUS,
				    M_WAITOK | M_ZERO);

				dinfo->bus = bus;
				dinfo->dev = dev;
				dinfo->bustype = bustype;
				dinfo->devtype = devtype;
				dinfo->busidx = bus_index;
				dinfo->devidx = dev_index;

				if (dinfo->bustype == PS3_BUSTYPE_SYSBUS ||
				    dinfo->bustype == PS3_BUSTYPE_STORAGE)
					lv1_open_device(bus, dev, 0);

				ps3bus_resources_init(&sc->sc_mem_rman, bus_index,
				    dev_index, dinfo);

				cdev = device_add_child(self, NULL, -1);
				if (cdev == NULL) {
					device_printf(self,
					    "device_add_child failed\n");
					free(dinfo, M_PS3BUS);
					continue;
				}

				mtx_init(&dinfo->iommu_mtx, "iommu", NULL, MTX_DEF);
				device_set_ivars(cdev, dinfo);
			}
		}
	}
	
	clock_register(self, 1000);

	return (bus_generic_attach(self));
}

static int
ps3bus_print_child(device_t dev, device_t child)
{
	struct ps3bus_devinfo *dinfo = device_get_ivars(child);
	int retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += resource_list_print_type(&dinfo->resources, "mem",
	    SYS_RES_MEMORY, "%#jx");
	retval += resource_list_print_type(&dinfo->resources, "irq",
	    SYS_RES_IRQ, "%jd");

	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
ps3bus_read_ivar(device_t bus, device_t child, int which, uintptr_t *result)
{
	struct ps3bus_devinfo *dinfo = device_get_ivars(child);

	switch (which) {
	case PS3BUS_IVAR_BUS:
		*result = dinfo->bus;
		break;
	case PS3BUS_IVAR_DEVICE:
		*result = dinfo->dev;
		break;
	case PS3BUS_IVAR_BUSTYPE:
		*result = dinfo->bustype;
		break;
	case PS3BUS_IVAR_DEVTYPE:
		*result = dinfo->devtype;
		break;
	case PS3BUS_IVAR_BUSIDX:
		*result = dinfo->busidx;
		break;
	case PS3BUS_IVAR_DEVIDX:
		*result = dinfo->devidx;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static struct resource *
ps3bus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct	ps3bus_devinfo *dinfo;
	struct	ps3bus_softc *sc;
	int	needactivate;
        struct	resource *rv;
        struct	rman *rm;
        rman_res_t	adjstart, adjend, adjcount;
        struct	resource_list_entry *rle;

	sc = device_get_softc(bus);
	dinfo = device_get_ivars(child);
	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	switch (type) {
	case SYS_RES_MEMORY:
		rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY,
		    *rid);
		if (rle == NULL) {
			device_printf(bus, "no rle for %s memory %d\n",
				      device_get_nameunit(child), *rid);
			return (NULL);
		}

		if (start < rle->start)
			adjstart = rle->start;
		else if (start > rle->end)
			adjstart = rle->end;
		else
			adjstart = start;

		if (end < rle->start)
			adjend = rle->start;
		else if (end > rle->end)
			adjend = rle->end;
		else
			adjend = end;

		adjcount = adjend - adjstart;

		rm = &sc->sc_mem_rman;
		break;
	case SYS_RES_IRQ:
		rle = resource_list_find(&dinfo->resources, SYS_RES_IRQ,
		    *rid);
		rm = &sc->sc_intr_rman;
		adjstart = rle->start;
		adjcount = ulmax(count, rle->count);
		adjend = ulmax(rle->end, rle->start + adjcount - 1);
		break;
	default:
		device_printf(bus, "unknown resource request from %s\n",
			      device_get_nameunit(child));
		return (NULL);
        }

	rv = rman_reserve_resource(rm, adjstart, adjend, adjcount, flags,
	    child);
	if (rv == NULL) {
		device_printf(bus,
			"failed to reserve resource %#lx - %#lx (%#lx)"
			" for %s\n", adjstart, adjend, adjcount,
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
ps3bus_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	void *p;

	if (type == SYS_RES_IRQ)
		return (bus_activate_resource(bus, type, rid, res));

	if (type == SYS_RES_MEMORY) {
		vm_offset_t start;

		start = (vm_offset_t) rman_get_start(res);

		if (bootverbose)
			printf("ps3 mapdev: start %zx, len %ld\n", start,
			       rman_get_size(res));

		p = pmap_mapdev(start, (vm_size_t) rman_get_size(res));
		if (p == NULL)
			return (ENOMEM);
		rman_set_virtual(res, p);
		rman_set_bustag(res, &bs_be_tag);
		rman_set_bushandle(res, (rman_res_t)p);
	}

	return (rman_activate_resource(res));
}

static bus_dma_tag_t
ps3bus_get_dma_tag(device_t dev, device_t child)
{
	struct ps3bus_devinfo *dinfo = device_get_ivars(child);
	struct ps3bus_softc *sc = device_get_softc(dev);
	int i, err, flags, pagesize;

	if (dinfo->bustype != PS3_BUSTYPE_SYSBUS &&
	    dinfo->bustype != PS3_BUSTYPE_STORAGE)
		return (bus_get_dma_tag(dev));

	mtx_lock(&dinfo->iommu_mtx);
	if (dinfo->dma_tag != NULL) {
		mtx_unlock(&dinfo->iommu_mtx);
		return (dinfo->dma_tag);
	}

	flags = 0; /* 32-bit mode */
	if (dinfo->bustype == PS3_BUSTYPE_SYSBUS &&
	    dinfo->devtype == PS3_DEVTYPE_USB)
		flags = 2; /* 8-bit mode */

	pagesize = 24; /* log_2(16 MB) */
	if (dinfo->bustype == PS3_BUSTYPE_STORAGE)
		pagesize = 12; /* 4 KB */

	for (i = 0; i < sc->rcount; i++) {
		err = lv1_allocate_device_dma_region(dinfo->bus, dinfo->dev,
		    sc->regions[i].mr_size, pagesize, flags,
		    &dinfo->dma_base[i]);
		if (err != 0) {
			device_printf(child,
			    "could not allocate DMA region %d: %d\n", i, err);
			goto fail;
		}

		err = lv1_map_device_dma_region(dinfo->bus, dinfo->dev,
		    sc->regions[i].mr_start, dinfo->dma_base[i],
		    sc->regions[i].mr_size,
		    0xf800000000000800UL /* Cell Handbook Figure 7.3.4.1 */);
		if (err != 0) {
			device_printf(child,
			    "could not map DMA region %d: %d\n", i, err);
			goto fail;
		}
	}

	err = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, BUS_SPACE_MAXSIZE, 0, BUS_SPACE_MAXSIZE,
	    0, NULL, NULL, &dinfo->dma_tag);

	/*
	 * Note: storage devices have IOMMU mappings set up by the hypervisor,
	 * but use physical, non-translated addresses. The above IOMMU
	 * initialization is necessary for the hypervisor to be able to set up
	 * the mappings, but actual DMA mappings should not use the IOMMU
	 * routines.
	 */
	if (dinfo->bustype != PS3_BUSTYPE_STORAGE)
		bus_dma_tag_set_iommu(dinfo->dma_tag, dev, dinfo);

fail:
	mtx_unlock(&dinfo->iommu_mtx);

	if (err)
		return (NULL);

	return (dinfo->dma_tag);
}

static int
ps3_iommu_map(device_t dev, bus_dma_segment_t *segs, int *nsegs,
    bus_addr_t min, bus_addr_t max, bus_size_t alignment, bus_addr_t boundary,
    void *cookie)
{
	struct ps3bus_devinfo *dinfo = cookie;
	struct ps3bus_softc *sc = device_get_softc(dev);
	int i, j;

	for (i = 0; i < *nsegs; i++) {
		for (j = 0; j < sc->rcount; j++) {
			if (segs[i].ds_addr >= sc->regions[j].mr_start &&
			    segs[i].ds_addr < sc->regions[j].mr_start +
			      sc->regions[j].mr_size)
				break;
		}
		KASSERT(j < sc->rcount,
		    ("Trying to map address %#lx not in physical memory",
		    segs[i].ds_addr));

		segs[i].ds_addr = dinfo->dma_base[j] +
		    (segs[i].ds_addr - sc->regions[j].mr_start);
	}

	return (0);
}

static int
ps3_iommu_unmap(device_t dev, bus_dma_segment_t *segs, int nsegs, void *cookie)
{

	return (0);
}

#define Y2K 946684800

static int
ps3_gettime(device_t dev, struct timespec *ts)
{
	uint64_t rtc, tb;
	int result;

	result = lv1_get_rtc(&rtc, &tb);
	if (result)
		return (result);

	ts->tv_sec = rtc + Y2K;
	ts->tv_nsec = 0;
	return (0);
}
	
static int
ps3_settime(device_t dev, struct timespec *ts)
{
	return (-1);
}

