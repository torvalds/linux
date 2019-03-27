/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013, Nathan Whitehorn <nwhitehorn@FreeBSD.org>
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
#include <sys/module.h>
#include <sys/vmem.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>

#include <powerpc/pseries/phyp-hvcall.h>
#include <powerpc/pseries/plpar_iommu.h>

MALLOC_DEFINE(M_PHYPIOMMU, "iommu", "IOMMU data for PAPR LPARs");

struct papr_iommu_map {
	uint32_t iobn;
	vmem_t *vmem;
	struct papr_iommu_map *next;
};

static SLIST_HEAD(iommu_maps, iommu_map) iommu_map_head =
    SLIST_HEAD_INITIALIZER(iommu_map_head);
static int papr_supports_stuff_tce = -1;

struct iommu_map {
	uint32_t iobn;
	vmem_t *vmem;

	SLIST_ENTRY(iommu_map) entries;
};

struct dma_window {
	struct iommu_map *map;
	bus_addr_t start;
	bus_addr_t end;
};

int
phyp_iommu_set_dma_tag(device_t bus, device_t dev, bus_dma_tag_t tag)
{
	device_t p;
	phandle_t node;
	cell_t dma_acells, dma_scells, dmawindow[6];
	struct iommu_map *i;
	int cell;

	for (p = dev; device_get_parent(p) != NULL; p = device_get_parent(p)) {
		if (ofw_bus_has_prop(p, "ibm,my-dma-window"))
			break;
		if (ofw_bus_has_prop(p, "ibm,dma-window"))
			break;
	}

	if (p == NULL)
		return (ENXIO);

	node = ofw_bus_get_node(p);
	if (OF_getencprop(node, "ibm,#dma-size-cells", &dma_scells,
	    sizeof(cell_t)) <= 0)
		OF_searchencprop(node, "#size-cells", &dma_scells,
		    sizeof(cell_t));
	if (OF_getencprop(node, "ibm,#dma-address-cells", &dma_acells,
	    sizeof(cell_t)) <= 0)
		OF_searchencprop(node, "#address-cells", &dma_acells,
		    sizeof(cell_t));

	if (ofw_bus_has_prop(p, "ibm,my-dma-window"))
		OF_getencprop(node, "ibm,my-dma-window", dmawindow,
		    sizeof(cell_t)*(dma_scells + dma_acells + 1));
	else
		OF_getencprop(node, "ibm,dma-window", dmawindow,
		    sizeof(cell_t)*(dma_scells + dma_acells + 1));

	struct dma_window *window = malloc(sizeof(struct dma_window),
	    M_PHYPIOMMU, M_WAITOK);
	window->start = 0;
	for (cell = 1; cell < 1 + dma_acells; cell++) {
		window->start <<= 32;
		window->start |= dmawindow[cell];
	}
	window->end = 0;
	for (; cell < 1 + dma_acells + dma_scells; cell++) {
		window->end <<= 32;
		window->end |= dmawindow[cell];
	}
	window->end += window->start;

	if (bootverbose)
		device_printf(dev, "Mapping IOMMU domain %#x\n", dmawindow[0]);
	window->map = NULL;
	SLIST_FOREACH(i, &iommu_map_head, entries) {
		if (i->iobn == dmawindow[0]) {
			window->map = i;
			break;
		}
	}

	if (window->map == NULL) {
		window->map = malloc(sizeof(struct iommu_map), M_PHYPIOMMU,
		    M_WAITOK);
		window->map->iobn = dmawindow[0];
		/*
		 * Allocate IOMMU range beginning at PAGE_SIZE. Some drivers
		 * (em(4), for example) do not like getting mappings at 0.
		 */
		window->map->vmem = vmem_create("IOMMU mappings", PAGE_SIZE,
		    trunc_page(VMEM_ADDR_MAX) - PAGE_SIZE, PAGE_SIZE, 0,
		    M_BESTFIT | M_NOWAIT);
		SLIST_INSERT_HEAD(&iommu_map_head, window->map, entries);
	}

	/*
	 * Check experimentally whether we can use H_STUFF_TCE. It is required
	 * by the spec but some firmware (e.g. QEMU) does not actually support
	 * it
	 */
	if (papr_supports_stuff_tce == -1)
		papr_supports_stuff_tce = !(phyp_hcall(H_STUFF_TCE,
		    window->map->iobn, 0, 0, 0) == H_FUNCTION);

	bus_dma_tag_set_iommu(tag, bus, window);

	return (0);
}

int
phyp_iommu_map(device_t dev, bus_dma_segment_t *segs, int *nsegs,
    bus_addr_t min, bus_addr_t max, bus_size_t alignment, bus_addr_t boundary,
    void *cookie)
{
	struct dma_window *window = cookie;
	bus_addr_t minaddr, maxaddr;
	bus_addr_t alloced;
	bus_size_t allocsize;
	int error, i, j;
	uint64_t tce;
	minaddr = window->start;
	maxaddr = window->end;

	/* XXX: handle exclusion range in a more useful way */
	if (min < maxaddr)
		maxaddr = min;

	/* XXX: consolidate segs? */
	for (i = 0; i < *nsegs; i++) {
		allocsize = round_page(segs[i].ds_len +
		    (segs[i].ds_addr & PAGE_MASK));
		error = vmem_xalloc(window->map->vmem, allocsize,
		    (alignment < PAGE_SIZE) ? PAGE_SIZE : alignment, 0,
		    boundary, minaddr, maxaddr, M_BESTFIT | M_NOWAIT, &alloced);
		if (error != 0) {
			panic("VMEM failure: %d\n", error);
			return (error);
		}
		KASSERT(alloced % PAGE_SIZE == 0, ("Alloc not page aligned"));
		KASSERT((alloced + (segs[i].ds_addr & PAGE_MASK)) %
		    alignment == 0,
		    ("Allocated segment does not match alignment constraint"));

		tce = trunc_page(segs[i].ds_addr);
		tce |= 0x3; /* read/write */
		for (j = 0; j < allocsize; j += PAGE_SIZE) {
			error = phyp_hcall(H_PUT_TCE, window->map->iobn,
			    alloced + j, tce + j);
			if (error < 0) {
				panic("IOMMU mapping error: %d\n", error);
				return (ENOMEM);
			}
		}

		segs[i].ds_addr = alloced + (segs[i].ds_addr & PAGE_MASK);
		KASSERT(segs[i].ds_addr > 0, ("Address needs to be positive"));
		KASSERT(segs[i].ds_addr + segs[i].ds_len < maxaddr,
		    ("Address not in range"));
		if (error < 0) {
			panic("IOMMU mapping error: %d\n", error);
			return (ENOMEM);
		}
	}

	return (0);
}
	
int
phyp_iommu_unmap(device_t dev, bus_dma_segment_t *segs, int nsegs, void *cookie)
{
	struct dma_window *window = cookie;
	bus_addr_t pageround;
	bus_size_t roundedsize;
	int i;
	bus_addr_t j;

	for (i = 0; i < nsegs; i++) {
		pageround = trunc_page(segs[i].ds_addr);
		roundedsize = round_page(segs[i].ds_len +
		    (segs[i].ds_addr & PAGE_MASK));

		if (papr_supports_stuff_tce) {
			phyp_hcall(H_STUFF_TCE, window->map->iobn, pageround, 0,
			    roundedsize/PAGE_SIZE);
		} else {
			for (j = 0; j < roundedsize; j += PAGE_SIZE)
				phyp_hcall(H_PUT_TCE, window->map->iobn,
				    pageround + j, 0);
		}

		vmem_xfree(window->map->vmem, pageround, roundedsize);
	}

	return (0);
}

