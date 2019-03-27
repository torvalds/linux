/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2001 Thomas Moestl <tmm@FreeBSD.org>
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
 *
 *	from: FreeBSD: src/sys/alpha/isa/isa.c,v 1.26 2001/07/11
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <sys/rman.h>

#include <isa/isareg.h>
#include <isa/isavar.h>
#include <isa/isa_common.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sparc64/pci/ofw_pci.h>
#include <sparc64/isa/ofw_isa.h>

/* There can be only one ISA bus, so it is safe to use globals. */
static u_int64_t isa_io_base;
static u_int64_t isa_io_limit;
static u_int64_t isa_mem_base;
static u_int64_t isa_mem_limit;

device_t isa_bus_device;

static phandle_t isab_node;
static struct isa_ranges *isab_ranges;
static int isab_nrange;
static struct ofw_bus_iinfo isa_iinfo;

/*
 * XXX: This is really partly PCI-specific, but unfortunately is
 * differently enough to have to duplicate it here...
 */
#define	ISAB_RANGE_PHYS(r)						\
	(((u_int64_t)(r)->phys_mid << 32) | (u_int64_t)(r)->phys_lo)
#define	ISAB_RANGE_SPACE(r)	(((r)->phys_hi >> 24) & 0x03)

#define	ISAR_SPACE_IO		0x01
#define	ISAR_SPACE_MEM		0x02

#define INRANGE(x, start, end)	((x) >= (start) && (x) <= (end))

static void	isa_setup_children(device_t, phandle_t);

void
isa_init(device_t dev)
{
	device_t bridge;
	int i;

	/* The parent of the bus must be a PCI-ISA bridge. */
	bridge = device_get_parent(dev);
	isab_node = ofw_bus_get_node(bridge);
	isab_nrange = OF_getprop_alloc_multi(isab_node, "ranges",
	    sizeof(*isab_ranges), (void **)&isab_ranges);
	if (isab_nrange <= 0)
		panic("isa_init: cannot get bridge range property");

	ofw_bus_setup_iinfo(isab_node, &isa_iinfo, sizeof(ofw_isa_intr_t));

	isa_setup_children(dev, isab_node);

	for (i = isab_nrange - 1; i >= 0; i--) {
		switch(ISAB_RANGE_SPACE(&isab_ranges[i])) {
		case ISAR_SPACE_IO:
			/* This is probably always 0. */
			isa_io_base = ISAB_RANGE_PHYS(&isab_ranges[i]);
			isa_io_limit = isab_ranges[i].size;
			break;
		case ISAR_SPACE_MEM:
			/* This is probably always 0. */
			isa_mem_base = ISAB_RANGE_PHYS(&isab_ranges[i]);
			isa_mem_limit = isab_ranges[i].size;
			break;
		}
	}
}

static const struct {
	const char	*const name;
	uint32_t	id;
} ofw_isa_pnp_map[] = {
	{ "SUNW,lomh",	0x0000ae4e }, /* SUN0000 */
	{ "dma",	0x0002d041 }, /* PNP0200 */
	{ "floppy",	0x0007d041 }, /* PNP0700 */
	{ "rtc",	0x000bd041 }, /* PNP0B00 */
	{ "flashprom",	0x0100ae4e }, /* SUN0001 */
	{ "parallel",	0x0104d041 }, /* PNP0401 */
	{ "serial",	0x0105d041 }, /* PNP0501 */
	{ "su",		0x0105d041 }, /* PNP0501 */
	{ "i2c",	0x0200ae4e }, /* SUN0002 */
	{ "rmc-comm",	0x0300ae4e }, /* SUN0003 */
	{ "kb_ps2",	0x0303d041 }, /* PNP0303 */
	{ "kdmouse",	0x030fd041 }, /* PNP0F03 */
	{ "bscbus",	0x0400ae4e }, /* SUN0004 */
	{ "power",	0x0c0cd041 }, /* PNP0C0C */
	{ NULL,		0x0 }
};

static void
isa_setup_children(device_t dev, phandle_t parent)
{
	struct isa_regs *regs;
	struct resource_list *rl;
	device_t cdev;
	u_int64_t end, start;
	ofw_isa_intr_t *intrs, rintr;
	phandle_t node;
	uint32_t *drqs, *regidx;
	int i, ndrq, nintr, nreg, nregidx, rid, rtype;
	char *name;

	/*
	 * Loop through children and fake up PnP devices for them.
	 * Their resources are added as fully mapped and specified because
	 * adjusting the resources and the resource list entries respectively
	 * in isa_alloc_resource() causes trouble with drivers which use
	 * rman_get_start(), pass-through or allocate and release resources
	 * multiple times, etc. Adjusting the resources might be better off
	 * in a bus_activate_resource method but the common ISA code doesn't
	 * allow for an isa_activate_resource().
	 */
	for (node = OF_child(parent); node != 0; node = OF_peer(node)) {
		if ((OF_getprop_alloc(node, "name", (void **)&name)) == -1)
			continue;

		/*
		 * Keyboard and mouse controllers hang off of the `8042'
		 * node but we have no real use for the `8042' itself.
		 */
		if (strcmp(name, "8042") == 0) {
			isa_setup_children(dev, node);
			OF_prop_free(name);
			continue;
		}

		for (i = 0; ofw_isa_pnp_map[i].name != NULL; i++)
			if (strcmp(ofw_isa_pnp_map[i].name, name) == 0)
				break;
		if (ofw_isa_pnp_map[i].name == NULL) {
			device_printf(dev, "no PnP map entry for node "
			    "0x%lx: %s\n", (unsigned long)node, name);
			OF_prop_free(name);
			continue;
		}

		if ((cdev = BUS_ADD_CHILD(dev, ISA_ORDER_PNPBIOS, NULL, -1)) ==
		    NULL)
			panic("isa_setup_children: BUS_ADD_CHILD failed");
		isa_set_logicalid(cdev, ofw_isa_pnp_map[i].id);
		isa_set_vendorid(cdev, ofw_isa_pnp_map[i].id);

		rl = BUS_GET_RESOURCE_LIST(dev, cdev);
		nreg = OF_getprop_alloc_multi(node, "reg", sizeof(*regs),
		    (void **)&regs);
		for (i = 0; i < nreg; i++) {
			start = ISA_REG_PHYS(&regs[i]);
			end = start + regs[i].size - 1;
			rtype = ofw_isa_range_map(isab_ranges, isab_nrange,
			    &start, &end, NULL);
			rid = 0;
			while (resource_list_find(rl, rtype, rid) != NULL)
				rid++;
			bus_set_resource(cdev, rtype, rid, start,
			    end - start + 1);
		}
		if (nreg == -1 && parent != isab_node) {
			/*
			 * The "reg" property still might be an index into
			 * the set of registers of the parent device like
			 * with the nodes hanging off of the `8042' node.
			 */
			nregidx = OF_getprop_alloc_multi(node, "reg", sizeof(*regidx),
			    (void **)&regidx);
			if (nregidx > 2)
				panic("isa_setup_children: impossible number "
				    "of register indices");
			if (nregidx != -1 && (nreg = OF_getprop_alloc_multi(parent,
			    "reg", sizeof(*regs), (void **)&regs)) >= nregidx) {
				for (i = 0; i < nregidx; i++) {
					start = ISA_REG_PHYS(&regs[regidx[i]]);
					end = start + regs[regidx[i]].size - 1;
					rtype = ofw_isa_range_map(isab_ranges,
					    isab_nrange, &start, &end, NULL);
					rid = 0;
					while (resource_list_find(rl, rtype,
					    rid) != NULL)
						rid++;
					bus_set_resource(cdev, rtype, rid,
					    start, end - start + 1);
				}
			}
			if (regidx != NULL)
				OF_prop_free(regidx);
		}
		if (regs != NULL)
			OF_prop_free(regs);

		nintr = OF_getprop_alloc_multi(node, "interrupts", sizeof(*intrs),
		    (void **)&intrs);
		for (i = 0; i < nintr; i++) {
			if (intrs[i] > 7)
				panic("isa_setup_children: intr too large");
			rintr = ofw_isa_route_intr(device_get_parent(dev), node,
			    &isa_iinfo, intrs[i]);
			if (rintr == PCI_INVALID_IRQ) {
				device_printf(dev, "could not map ISA "
				    "interrupt %d for node 0x%lx: %s\n",
				    intrs[i], (unsigned long)node, name);
				continue;
			}
			bus_set_resource(cdev, SYS_RES_IRQ, i, rintr, 1);
		}
		if (intrs != NULL)
			OF_prop_free(intrs);

		ndrq = OF_getprop_alloc_multi(node, "dma-channel", sizeof(*drqs),
		    (void **)&drqs);
		for (i = 0; i < ndrq; i++)
			bus_set_resource(cdev, SYS_RES_DRQ, i, drqs[i], 1);
		if (drqs != NULL)
			OF_prop_free(drqs);

		/*
		 * Devices using DMA hang off of the `dma' node instead of
		 * directly from the ISA bridge node.
		 */
		if (strcmp(name, "dma") == 0)
			isa_setup_children(dev, node);

		OF_prop_free(name);
	}
}

struct resource *
isa_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	/*
	 * Consider adding a resource definition.
	 */
	int passthrough = (device_get_parent(child) != bus);
	int isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	struct resource_list *rl;
	struct resource_list_entry *rle;
	u_long base, limit;

	rl = BUS_GET_RESOURCE_LIST(bus, child);
	if (!passthrough && !isdefault) {
		rle = resource_list_find(rl, type, *rid);
		if (!rle) {
			if (*rid < 0)
				return (NULL);
			switch (type) {
			case SYS_RES_IRQ:
				if (*rid >= ISA_NIRQ)
					return (NULL);
				break;
			case SYS_RES_DRQ:
				if (*rid >= ISA_NDRQ)
					return (NULL);
				break;
			case SYS_RES_MEMORY:
				if (*rid >= ISA_NMEM)
					return (NULL);
				break;
			case SYS_RES_IOPORT:
				if (*rid >= ISA_NPORT)
					return (NULL);
				break;
			default:
				return (NULL);
			}
			resource_list_add(rl, type, *rid, start, end, count);
		}
	}

	/*
	 * Sanity check if the resource in the respective entry is fully
	 * mapped and specified and its type allocable. A driver could
	 * have added an out of range resource on its own.
	 */
	if (!passthrough) {
		if ((rle = resource_list_find(rl, type, *rid)) == NULL)
			return (NULL);
		base = limit = 0;
		switch (type) {
		case SYS_RES_MEMORY:
			base = isa_mem_base;
			limit = base + isa_mem_limit;
			break;
		case SYS_RES_IOPORT:
			base = isa_io_base;
			limit = base + isa_io_limit;
			break;
		case SYS_RES_IRQ:
			if (rle->start != rle->end || rle->start <= 7)
				return (NULL);
			break;
		case SYS_RES_DRQ:
			break;
		default:
			return (NULL);
		}
		if (type == SYS_RES_MEMORY || type == SYS_RES_IOPORT) {
			if (!INRANGE(rle->start, base, limit) ||
			    !INRANGE(rle->end, base, limit))
				return (NULL);
		}
	}

	return (resource_list_alloc(rl, bus, child, type, rid, start, end,
	    count, flags));
}

int
isa_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{

	return (bus_generic_rl_release_resource(bus, child, type, rid, res));
}
