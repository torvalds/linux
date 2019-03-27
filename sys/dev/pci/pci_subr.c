/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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

/*
 * Support APIs for Host to PCI bridge drivers and drivers that
 * provide PCI domains.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

/*
 * Try to read the bus number of a host-PCI bridge using appropriate config
 * registers.
 */
int
host_pcib_get_busno(pci_read_config_fn read_config, int bus, int slot, int func,
    uint8_t *busnum)
{
	uint32_t id;

	id = read_config(bus, slot, func, PCIR_DEVVENDOR, 4);
	if (id == 0xffffffff)
		return (0);

	switch (id) {
	case 0x12258086:
		/* Intel 824?? */
		/* XXX This is a guess */
		/* *busnum = read_config(bus, slot, func, 0x41, 1); */
		*busnum = bus;
		break;
	case 0x84c48086:
		/* Intel 82454KX/GX (Orion) */
		*busnum = read_config(bus, slot, func, 0x4a, 1);
		break;
	case 0x84ca8086:
		/*
		 * For the 450nx chipset, there is a whole bundle of
		 * things pretending to be host bridges. The MIOC will 
		 * be seen first and isn't really a pci bridge (the
		 * actual buses are attached to the PXB's). We need to 
		 * read the registers of the MIOC to figure out the
		 * bus numbers for the PXB channels.
		 *
		 * Since the MIOC doesn't have a pci bus attached, we
		 * pretend it wasn't there.
		 */
		return (0);
	case 0x84cb8086:
		switch (slot) {
		case 0x12:
			/* Intel 82454NX PXB#0, Bus#A */
			*busnum = read_config(bus, 0x10, func, 0xd0, 1);
			break;
		case 0x13:
			/* Intel 82454NX PXB#0, Bus#B */
			*busnum = read_config(bus, 0x10, func, 0xd1, 1) + 1;
			break;
		case 0x14:
			/* Intel 82454NX PXB#1, Bus#A */
			*busnum = read_config(bus, 0x10, func, 0xd3, 1);
			break;
		case 0x15:
			/* Intel 82454NX PXB#1, Bus#B */
			*busnum = read_config(bus, 0x10, func, 0xd4, 1) + 1;
			break;
		}
		break;

		/* ServerWorks -- vendor 0x1166 */
	case 0x00051166:
	case 0x00061166:
	case 0x00081166:
	case 0x00091166:
	case 0x00101166:
	case 0x00111166:
	case 0x00171166:
	case 0x01011166:
	case 0x010f1014:
	case 0x01101166:
	case 0x02011166:
	case 0x02251166:
	case 0x03021014:
		*busnum = read_config(bus, slot, func, 0x44, 1);
		break;

		/* Compaq/HP -- vendor 0x0e11 */
	case 0x60100e11:
		*busnum = read_config(bus, slot, func, 0xc8, 1);
		break;
	default:
		/* Don't know how to read bus number. */
		return 0;
	}

	return 1;
}

#ifdef NEW_PCIB
/*
 * Return a pointer to a pretty name for a PCI device.  If the device
 * has a driver attached, the device's name is used, otherwise a name
 * is generated from the device's PCI address.
 */
const char *
pcib_child_name(device_t child)
{
	static char buf[64];

	if (device_get_nameunit(child) != NULL)
		return (device_get_nameunit(child));
	snprintf(buf, sizeof(buf), "pci%d:%d:%d:%d", pci_get_domain(child),
	    pci_get_bus(child), pci_get_slot(child), pci_get_function(child));
	return (buf);
}

/*
 * Some Host-PCI bridge drivers know which resource ranges they can
 * decode and should only allocate subranges to child PCI devices.
 * This API provides a way to manage this.  The bridge driver should
 * initialize this structure during attach and call
 * pcib_host_res_decodes() on each resource range it decodes.  It can
 * then use pcib_host_res_alloc() and pcib_host_res_adjust() as helper
 * routines for BUS_ALLOC_RESOURCE() and BUS_ADJUST_RESOURCE().  This
 * API assumes that resources for any decoded ranges can be safely
 * allocated from the parent via bus_generic_alloc_resource().
 */
int
pcib_host_res_init(device_t pcib, struct pcib_host_resources *hr)
{

	hr->hr_pcib = pcib;
	resource_list_init(&hr->hr_rl);
	return (0);
}

int
pcib_host_res_free(device_t pcib, struct pcib_host_resources *hr)
{

	resource_list_free(&hr->hr_rl);
	return (0);
}

int
pcib_host_res_decodes(struct pcib_host_resources *hr, int type, rman_res_t start,
    rman_res_t end, u_int flags)
{
	struct resource_list_entry *rle;
	int rid;

	if (bootverbose)
		device_printf(hr->hr_pcib, "decoding %d %srange %#jx-%#jx\n",
		    type, flags & RF_PREFETCHABLE ? "prefetchable ": "", start,
		    end);
	rid = resource_list_add_next(&hr->hr_rl, type, start, end,
	    end - start + 1);
	if (flags & RF_PREFETCHABLE) {
		KASSERT(type == SYS_RES_MEMORY,
		    ("only memory is prefetchable"));
		rle = resource_list_find(&hr->hr_rl, type, rid);
		rle->flags = RLE_PREFETCH;
	}
	return (0);
}

struct resource *
pcib_host_res_alloc(struct pcib_host_resources *hr, device_t dev, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list_entry *rle;
	struct resource *r;
	rman_res_t new_start, new_end;

	if (flags & RF_PREFETCHABLE)
		KASSERT(type == SYS_RES_MEMORY,
		    ("only memory is prefetchable"));

	rle = resource_list_find(&hr->hr_rl, type, 0);
	if (rle == NULL) {
		/*
		 * No decoding ranges for this resource type, just pass
		 * the request up to the parent.
		 */
		return (bus_generic_alloc_resource(hr->hr_pcib, dev, type, rid,
		    start, end, count, flags));
	}

restart:
	/* Try to allocate from each decoded range. */
	for (; rle != NULL; rle = STAILQ_NEXT(rle, link)) {
		if (rle->type != type)
			continue;
		if (((flags & RF_PREFETCHABLE) != 0) !=
		    ((rle->flags & RLE_PREFETCH) != 0))
			continue;
		new_start = ummax(start, rle->start);
		new_end = ummin(end, rle->end);
		if (new_start > new_end ||
		    new_start + count - 1 > new_end ||
		    new_start + count < new_start)
			continue;
		r = bus_generic_alloc_resource(hr->hr_pcib, dev, type, rid,
		    new_start, new_end, count, flags);
		if (r != NULL) {
			if (bootverbose)
				device_printf(hr->hr_pcib,
			    "allocated type %d (%#jx-%#jx) for rid %x of %s\n",
				    type, rman_get_start(r), rman_get_end(r),
				    *rid, pcib_child_name(dev));
			return (r);
		}
	}

	/*
	 * If we failed to find a prefetch range for a memory
	 * resource, try again without prefetch.
	 */
	if (flags & RF_PREFETCHABLE) {
		flags &= ~RF_PREFETCHABLE;
		rle = resource_list_find(&hr->hr_rl, type, 0);
		goto restart;
	}
	return (NULL);
}

int
pcib_host_res_adjust(struct pcib_host_resources *hr, device_t dev, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(&hr->hr_rl, type, 0);
	if (rle == NULL) {
		/*
		 * No decoding ranges for this resource type, just pass
		 * the request up to the parent.
		 */
		return (bus_generic_adjust_resource(hr->hr_pcib, dev, type, r,
		    start, end));
	}

	/* Only allow adjustments that stay within a decoded range. */
	for (; rle != NULL; rle = STAILQ_NEXT(rle, link)) {
		if (rle->start <= start && rle->end >= end)
			return (bus_generic_adjust_resource(hr->hr_pcib, dev,
			    type, r, start, end));
	}
	return (ERANGE);
}

#ifdef PCI_RES_BUS
struct pci_domain {
	int	pd_domain;
	struct rman pd_bus_rman;
	TAILQ_ENTRY(pci_domain) pd_link;
};

static TAILQ_HEAD(, pci_domain) domains = TAILQ_HEAD_INITIALIZER(domains);

/*
 * Each PCI domain maintains its own resource manager for PCI bus
 * numbers in that domain.  Domain objects are created on first use.
 * Host to PCI bridge drivers and PCI-PCI bridge drivers should
 * allocate their bus ranges from their domain.
 */
static struct pci_domain *
pci_find_domain(int domain)
{
	struct pci_domain *d;
	char buf[64];
	int error;

	TAILQ_FOREACH(d, &domains, pd_link) {
		if (d->pd_domain == domain)
			return (d);
	}

	snprintf(buf, sizeof(buf), "PCI domain %d bus numbers", domain);
	d = malloc(sizeof(*d) + strlen(buf) + 1, M_DEVBUF, M_WAITOK | M_ZERO);
	d->pd_domain = domain;
	d->pd_bus_rman.rm_start = 0;
	d->pd_bus_rman.rm_end = PCI_BUSMAX;
	d->pd_bus_rman.rm_type = RMAN_ARRAY;
	strcpy((char *)(d + 1), buf);
	d->pd_bus_rman.rm_descr = (char *)(d + 1);
	error = rman_init(&d->pd_bus_rman);
	if (error == 0)
		error = rman_manage_region(&d->pd_bus_rman, 0, PCI_BUSMAX);
	if (error)
		panic("Failed to initialize PCI domain %d rman", domain);
	TAILQ_INSERT_TAIL(&domains, d, pd_link);
	return (d);
}

struct resource *
pci_domain_alloc_bus(int domain, device_t dev, int *rid, rman_res_t start,
    rman_res_t end, rman_res_t count, u_int flags)
{
	struct pci_domain *d;
	struct resource *res;

	if (domain < 0 || domain > PCI_DOMAINMAX)
		return (NULL);
	d = pci_find_domain(domain);
	res = rman_reserve_resource(&d->pd_bus_rman, start, end, count, flags,
	    dev);
	if (res == NULL)
		return (NULL);

	rman_set_rid(res, *rid);
	return (res);
}

int
pci_domain_adjust_bus(int domain, device_t dev, struct resource *r,
    rman_res_t start, rman_res_t end)
{
#ifdef INVARIANTS
	struct pci_domain *d;
#endif

	if (domain < 0 || domain > PCI_DOMAINMAX)
		return (EINVAL);
#ifdef INVARIANTS
	d = pci_find_domain(domain);
	KASSERT(rman_is_region_manager(r, &d->pd_bus_rman), ("bad resource"));
#endif
	return (rman_adjust_resource(r, start, end));
}

int
pci_domain_release_bus(int domain, device_t dev, int rid, struct resource *r)
{
#ifdef INVARIANTS
	struct pci_domain *d;
#endif

	if (domain < 0 || domain > PCI_DOMAINMAX)
		return (EINVAL);
#ifdef INVARIANTS
	d = pci_find_domain(domain);
	KASSERT(rman_is_region_manager(r, &d->pd_bus_rman), ("bad resource"));
#endif
	return (rman_release_resource(r));
}
#endif /* PCI_RES_BUS */

#endif /* NEW_PCIB */
