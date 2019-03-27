/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/domainset.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/rman.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <sys/vmem.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>

/*
 * busdma_dmar.c, the implementation of the busdma(9) interface using
 * DMAR units from Intel VT-d.
 */

static bool
dmar_bus_dma_is_dev_disabled(int domain, int bus, int slot, int func)
{
	char str[128], *env;
	int default_bounce;
	bool ret;
	static const char bounce_str[] = "bounce";
	static const char dmar_str[] = "dmar";

	default_bounce = 0;
	env = kern_getenv("hw.busdma.default");
	if (env != NULL) {
		if (strcmp(env, bounce_str) == 0)
			default_bounce = 1;
		else if (strcmp(env, dmar_str) == 0)
			default_bounce = 0;
		freeenv(env);
	}

	snprintf(str, sizeof(str), "hw.busdma.pci%d.%d.%d.%d",
	    domain, bus, slot, func);
	env = kern_getenv(str);
	if (env == NULL)
		return (default_bounce != 0);
	if (strcmp(env, bounce_str) == 0)
		ret = true;
	else if (strcmp(env, dmar_str) == 0)
		ret = false;
	else
		ret = default_bounce != 0;
	freeenv(env);
	return (ret);
}

/*
 * Given original device, find the requester ID that will be seen by
 * the DMAR unit and used for page table lookup.  PCI bridges may take
 * ownership of transactions from downstream devices, so it may not be
 * the same as the BSF of the target device.  In those cases, all
 * devices downstream of the bridge must share a single mapping
 * domain, and must collectively be assigned to use either DMAR or
 * bounce mapping.
 */
device_t
dmar_get_requester(device_t dev, uint16_t *rid)
{
	devclass_t pci_class;
	device_t l, pci, pcib, pcip, pcibp, requester;
	int cap_offset;
	uint16_t pcie_flags;
	bool bridge_is_pcie;

	pci_class = devclass_find("pci");
	l = requester = dev;

	*rid = pci_get_rid(dev);

	/*
	 * Walk the bridge hierarchy from the target device to the
	 * host port to find the translating bridge nearest the DMAR
	 * unit.
	 */
	for (;;) {
		pci = device_get_parent(l);
		KASSERT(pci != NULL, ("dmar_get_requester(%s): NULL parent "
		    "for %s", device_get_name(dev), device_get_name(l)));
		KASSERT(device_get_devclass(pci) == pci_class,
		    ("dmar_get_requester(%s): non-pci parent %s for %s",
		    device_get_name(dev), device_get_name(pci),
		    device_get_name(l)));

		pcib = device_get_parent(pci);
		KASSERT(pcib != NULL, ("dmar_get_requester(%s): NULL bridge "
		    "for %s", device_get_name(dev), device_get_name(pci)));

		/*
		 * The parent of our "bridge" isn't another PCI bus,
		 * so pcib isn't a PCI->PCI bridge but rather a host
		 * port, and the requester ID won't be translated
		 * further.
		 */
		pcip = device_get_parent(pcib);
		if (device_get_devclass(pcip) != pci_class)
			break;
		pcibp = device_get_parent(pcip);

		if (pci_find_cap(l, PCIY_EXPRESS, &cap_offset) == 0) {
			/*
			 * Do not stop the loop even if the target
			 * device is PCIe, because it is possible (but
			 * unlikely) to have a PCI->PCIe bridge
			 * somewhere in the hierarchy.
			 */
			l = pcib;
		} else {
			/*
			 * Device is not PCIe, it cannot be seen as a
			 * requester by DMAR unit.  Check whether the
			 * bridge is PCIe.
			 */
			bridge_is_pcie = pci_find_cap(pcib, PCIY_EXPRESS,
			    &cap_offset) == 0;
			requester = pcib;

			/*
			 * Check for a buggy PCIe/PCI bridge that
			 * doesn't report the express capability.  If
			 * the bridge above it is express but isn't a
			 * PCI bridge, then we know pcib is actually a
			 * PCIe/PCI bridge.
			 */
			if (!bridge_is_pcie && pci_find_cap(pcibp,
			    PCIY_EXPRESS, &cap_offset) == 0) {
				pcie_flags = pci_read_config(pcibp,
				    cap_offset + PCIER_FLAGS, 2);
				if ((pcie_flags & PCIEM_FLAGS_TYPE) !=
				    PCIEM_TYPE_PCI_BRIDGE)
					bridge_is_pcie = true;
			}

			if (bridge_is_pcie) {
				/*
				 * The current device is not PCIe, but
				 * the bridge above it is.  This is a
				 * PCIe->PCI bridge.  Assume that the
				 * requester ID will be the secondary
				 * bus number with slot and function
				 * set to zero.
				 *
				 * XXX: Doesn't handle the case where
				 * the bridge is PCIe->PCI-X, and the
				 * bridge will only take ownership of
				 * requests in some cases.  We should
				 * provide context entries with the
				 * same page tables for taken and
				 * non-taken transactions.
				 */
				*rid = PCI_RID(pci_get_bus(l), 0, 0);
				l = pcibp;
			} else {
				/*
				 * Neither the device nor the bridge
				 * above it are PCIe.  This is a
				 * conventional PCI->PCI bridge, which
				 * will use the bridge's BSF as the
				 * requester ID.
				 */
				*rid = pci_get_rid(pcib);
				l = pcib;
			}
		}
	}
	return (requester);
}

struct dmar_ctx *
dmar_instantiate_ctx(struct dmar_unit *dmar, device_t dev, bool rmrr)
{
	device_t requester;
	struct dmar_ctx *ctx;
	bool disabled;
	uint16_t rid;

	requester = dmar_get_requester(dev, &rid);

	/*
	 * If the user requested the IOMMU disabled for the device, we
	 * cannot disable the DMAR, due to possibility of other
	 * devices on the same DMAR still requiring translation.
	 * Instead provide the identity mapping for the device
	 * context.
	 */
	disabled = dmar_bus_dma_is_dev_disabled(pci_get_domain(requester), 
	    pci_get_bus(requester), pci_get_slot(requester), 
	    pci_get_function(requester));
	ctx = dmar_get_ctx_for_dev(dmar, requester, rid, disabled, rmrr);
	if (ctx == NULL)
		return (NULL);
	if (disabled) {
		/*
		 * Keep the first reference on context, release the
		 * later refs.
		 */
		DMAR_LOCK(dmar);
		if ((ctx->flags & DMAR_CTX_DISABLED) == 0) {
			ctx->flags |= DMAR_CTX_DISABLED;
			DMAR_UNLOCK(dmar);
		} else {
			dmar_free_ctx_locked(dmar, ctx);
		}
		ctx = NULL;
	}
	return (ctx);
}

bus_dma_tag_t
dmar_get_dma_tag(device_t dev, device_t child)
{
	struct dmar_unit *dmar;
	struct dmar_ctx *ctx;
	bus_dma_tag_t res;

	dmar = dmar_find(child);
	/* Not in scope of any DMAR ? */
	if (dmar == NULL)
		return (NULL);
	if (!dmar->dma_enabled)
		return (NULL);
	dmar_quirks_pre_use(dmar);
	dmar_instantiate_rmrr_ctxs(dmar);

	ctx = dmar_instantiate_ctx(dmar, child, false);
	res = ctx == NULL ? NULL : (bus_dma_tag_t)&ctx->ctx_tag;
	return (res);
}

static MALLOC_DEFINE(M_DMAR_DMAMAP, "dmar_dmamap", "Intel DMAR DMA Map");

static void dmar_bus_schedule_dmamap(struct dmar_unit *unit,
    struct bus_dmamap_dmar *map);

static int
dmar_bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filter, void *filterarg, bus_size_t maxsize,
    int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
    void *lockfuncarg, bus_dma_tag_t *dmat)
{
	struct bus_dma_tag_dmar *newtag, *oldtag;
	int error;

	*dmat = NULL;
	error = common_bus_dma_tag_create(parent != NULL ?
	    &((struct bus_dma_tag_dmar *)parent)->common : NULL, alignment,
	    boundary, lowaddr, highaddr, filter, filterarg, maxsize,
	    nsegments, maxsegsz, flags, lockfunc, lockfuncarg,
	    sizeof(struct bus_dma_tag_dmar), (void **)&newtag);
	if (error != 0)
		goto out;

	oldtag = (struct bus_dma_tag_dmar *)parent;
	newtag->common.impl = &bus_dma_dmar_impl;
	newtag->ctx = oldtag->ctx;
	newtag->owner = oldtag->owner;

	*dmat = (bus_dma_tag_t)newtag;
out:
	CTR4(KTR_BUSDMA, "%s returned tag %p tag flags 0x%x error %d",
	    __func__, newtag, (newtag != NULL ? newtag->common.flags : 0),
	    error);
	return (error);
}

static int
dmar_bus_dma_tag_set_domain(bus_dma_tag_t dmat)
{

	return (0);
}

static int
dmar_bus_dma_tag_destroy(bus_dma_tag_t dmat1)
{
	struct bus_dma_tag_dmar *dmat, *dmat_copy, *parent;
	int error;

	error = 0;
	dmat_copy = dmat = (struct bus_dma_tag_dmar *)dmat1;

	if (dmat != NULL) {
		if (dmat->map_count != 0) {
			error = EBUSY;
			goto out;
		}
		while (dmat != NULL) {
			parent = (struct bus_dma_tag_dmar *)dmat->common.parent;
			if (atomic_fetchadd_int(&dmat->common.ref_count, -1) ==
			    1) {
				if (dmat == &dmat->ctx->ctx_tag)
					dmar_free_ctx(dmat->ctx);
				free_domain(dmat->segments, M_DMAR_DMAMAP);
				free(dmat, M_DEVBUF);
				dmat = parent;
			} else
				dmat = NULL;
		}
	}
out:
	CTR3(KTR_BUSDMA, "%s tag %p error %d", __func__, dmat_copy, error);
	return (error);
}

static int
dmar_bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "%s", __func__);

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = malloc_domainset(sizeof(*map), M_DMAR_DMAMAP,
	    DOMAINSET_PREF(tag->common.domain), M_NOWAIT | M_ZERO);
	if (map == NULL) {
		*mapp = NULL;
		return (ENOMEM);
	}
	if (tag->segments == NULL) {
		tag->segments = malloc_domainset(sizeof(bus_dma_segment_t) *
		    tag->common.nsegments, M_DMAR_DMAMAP,
		    DOMAINSET_PREF(tag->common.domain), M_NOWAIT);
		if (tag->segments == NULL) {
			free_domain(map, M_DMAR_DMAMAP);
			*mapp = NULL;
			return (ENOMEM);
		}
	}
	TAILQ_INIT(&map->map_entries);
	map->tag = tag;
	map->locked = true;
	map->cansleep = false;
	tag->map_count++;
	*mapp = (bus_dmamap_t)map;

	return (0);
}

static int
dmar_bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map1)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	struct dmar_domain *domain;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;
	if (map != NULL) {
		domain = tag->ctx->domain;
		DMAR_DOMAIN_LOCK(domain);
		if (!TAILQ_EMPTY(&map->map_entries)) {
			DMAR_DOMAIN_UNLOCK(domain);
			return (EBUSY);
		}
		DMAR_DOMAIN_UNLOCK(domain);
		free_domain(map, M_DMAR_DMAMAP);
	}
	tag->map_count--;
	return (0);
}


static int
dmar_bus_dmamem_alloc(bus_dma_tag_t dmat, void** vaddr, int flags,
    bus_dmamap_t *mapp)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	int error, mflags;
	vm_memattr_t attr;

	error = dmar_bus_dmamap_create(dmat, flags, mapp);
	if (error != 0)
		return (error);

	mflags = (flags & BUS_DMA_NOWAIT) != 0 ? M_NOWAIT : M_WAITOK;
	mflags |= (flags & BUS_DMA_ZERO) != 0 ? M_ZERO : 0;
	attr = (flags & BUS_DMA_NOCACHE) != 0 ? VM_MEMATTR_UNCACHEABLE :
	    VM_MEMATTR_DEFAULT;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)*mapp;

	if (tag->common.maxsize < PAGE_SIZE &&
	    tag->common.alignment <= tag->common.maxsize &&
	    attr == VM_MEMATTR_DEFAULT) {
		*vaddr = malloc_domainset(tag->common.maxsize, M_DEVBUF,
		    DOMAINSET_PREF(tag->common.domain), mflags);
		map->flags |= BUS_DMAMAP_DMAR_MALLOC;
	} else {
		*vaddr = (void *)kmem_alloc_attr_domainset(
		    DOMAINSET_PREF(tag->common.domain), tag->common.maxsize,
		    mflags, 0ul, BUS_SPACE_MAXADDR, attr);
		map->flags |= BUS_DMAMAP_DMAR_KMEM_ALLOC;
	}
	if (*vaddr == NULL) {
		dmar_bus_dmamap_destroy(dmat, *mapp);
		*mapp = NULL;
		return (ENOMEM);
	}
	return (0);
}

static void
dmar_bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map1)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;

	if ((map->flags & BUS_DMAMAP_DMAR_MALLOC) != 0) {
		free_domain(vaddr, M_DEVBUF);
		map->flags &= ~BUS_DMAMAP_DMAR_MALLOC;
	} else {
		KASSERT((map->flags & BUS_DMAMAP_DMAR_KMEM_ALLOC) != 0,
		    ("dmar_bus_dmamem_free for non alloced map %p", map));
		kmem_free((vm_offset_t)vaddr, tag->common.maxsize);
		map->flags &= ~BUS_DMAMAP_DMAR_KMEM_ALLOC;
	}

	dmar_bus_dmamap_destroy(dmat, map1);
}

static int
dmar_bus_dmamap_load_something1(struct bus_dma_tag_dmar *tag,
    struct bus_dmamap_dmar *map, vm_page_t *ma, int offset, bus_size_t buflen,
    int flags, bus_dma_segment_t *segs, int *segp,
    struct dmar_map_entries_tailq *unroll_list)
{
	struct dmar_ctx *ctx;
	struct dmar_domain *domain;
	struct dmar_map_entry *entry;
	dmar_gaddr_t size;
	bus_size_t buflen1;
	int error, idx, gas_flags, seg;

	KASSERT(offset < DMAR_PAGE_SIZE, ("offset %d", offset));
	if (segs == NULL)
		segs = tag->segments;
	ctx = tag->ctx;
	domain = ctx->domain;
	seg = *segp;
	error = 0;
	idx = 0;
	while (buflen > 0) {
		seg++;
		if (seg >= tag->common.nsegments) {
			error = EFBIG;
			break;
		}
		buflen1 = buflen > tag->common.maxsegsz ?
		    tag->common.maxsegsz : buflen;
		size = round_page(offset + buflen1);

		/*
		 * (Too) optimistically allow split if there are more
		 * then one segments left.
		 */
		gas_flags = map->cansleep ? DMAR_GM_CANWAIT : 0;
		if (seg + 1 < tag->common.nsegments)
			gas_flags |= DMAR_GM_CANSPLIT;

		error = dmar_gas_map(domain, &tag->common, size, offset,
		    DMAR_MAP_ENTRY_READ | DMAR_MAP_ENTRY_WRITE,
		    gas_flags, ma + idx, &entry);
		if (error != 0)
			break;
		if ((gas_flags & DMAR_GM_CANSPLIT) != 0) {
			KASSERT(size >= entry->end - entry->start,
			    ("split increased entry size %jx %jx %jx",
			    (uintmax_t)size, (uintmax_t)entry->start,
			    (uintmax_t)entry->end));
			size = entry->end - entry->start;
			if (buflen1 > size)
				buflen1 = size;
		} else {
			KASSERT(entry->end - entry->start == size,
			    ("no split allowed %jx %jx %jx",
			    (uintmax_t)size, (uintmax_t)entry->start,
			    (uintmax_t)entry->end));
		}
		if (offset + buflen1 > size)
			buflen1 = size - offset;
		if (buflen1 > tag->common.maxsegsz)
			buflen1 = tag->common.maxsegsz;

		KASSERT(((entry->start + offset) & (tag->common.alignment - 1))
		    == 0,
		    ("alignment failed: ctx %p start 0x%jx offset %x "
		    "align 0x%jx", ctx, (uintmax_t)entry->start, offset,
		    (uintmax_t)tag->common.alignment));
		KASSERT(entry->end <= tag->common.lowaddr ||
		    entry->start >= tag->common.highaddr,
		    ("entry placement failed: ctx %p start 0x%jx end 0x%jx "
		    "lowaddr 0x%jx highaddr 0x%jx", ctx,
		    (uintmax_t)entry->start, (uintmax_t)entry->end,
		    (uintmax_t)tag->common.lowaddr,
		    (uintmax_t)tag->common.highaddr));
		KASSERT(dmar_test_boundary(entry->start + offset, buflen1,
		    tag->common.boundary),
		    ("boundary failed: ctx %p start 0x%jx end 0x%jx "
		    "boundary 0x%jx", ctx, (uintmax_t)entry->start,
		    (uintmax_t)entry->end, (uintmax_t)tag->common.boundary));
		KASSERT(buflen1 <= tag->common.maxsegsz,
		    ("segment too large: ctx %p start 0x%jx end 0x%jx "
		    "buflen1 0x%jx maxsegsz 0x%jx", ctx,
		    (uintmax_t)entry->start, (uintmax_t)entry->end,
		    (uintmax_t)buflen1, (uintmax_t)tag->common.maxsegsz));

		DMAR_DOMAIN_LOCK(domain);
		TAILQ_INSERT_TAIL(&map->map_entries, entry, dmamap_link);
		entry->flags |= DMAR_MAP_ENTRY_MAP;
		DMAR_DOMAIN_UNLOCK(domain);
		TAILQ_INSERT_TAIL(unroll_list, entry, unroll_link);

		segs[seg].ds_addr = entry->start + offset;
		segs[seg].ds_len = buflen1;

		idx += OFF_TO_IDX(trunc_page(offset + buflen1));
		offset += buflen1;
		offset &= DMAR_PAGE_MASK;
		buflen -= buflen1;
	}
	if (error == 0)
		*segp = seg;
	return (error);
}

static int
dmar_bus_dmamap_load_something(struct bus_dma_tag_dmar *tag,
    struct bus_dmamap_dmar *map, vm_page_t *ma, int offset, bus_size_t buflen,
    int flags, bus_dma_segment_t *segs, int *segp)
{
	struct dmar_ctx *ctx;
	struct dmar_domain *domain;
	struct dmar_map_entry *entry, *entry1;
	struct dmar_map_entries_tailq unroll_list;
	int error;

	ctx = tag->ctx;
	domain = ctx->domain;
	atomic_add_long(&ctx->loads, 1);

	TAILQ_INIT(&unroll_list);
	error = dmar_bus_dmamap_load_something1(tag, map, ma, offset,
	    buflen, flags, segs, segp, &unroll_list);
	if (error != 0) {
		/*
		 * The busdma interface does not allow us to report
		 * partial buffer load, so unfortunately we have to
		 * revert all work done.
		 */
		DMAR_DOMAIN_LOCK(domain);
		TAILQ_FOREACH_SAFE(entry, &unroll_list, unroll_link,
		    entry1) {
			/*
			 * No entries other than what we have created
			 * during the failed run might have been
			 * inserted there in between, since we own ctx
			 * pglock.
			 */
			TAILQ_REMOVE(&map->map_entries, entry, dmamap_link);
			TAILQ_REMOVE(&unroll_list, entry, unroll_link);
			TAILQ_INSERT_TAIL(&domain->unload_entries, entry,
			    dmamap_link);
		}
		DMAR_DOMAIN_UNLOCK(domain);
		taskqueue_enqueue(domain->dmar->delayed_taskqueue,
		    &domain->unload_task);
	}

	if (error == ENOMEM && (flags & BUS_DMA_NOWAIT) == 0 &&
	    !map->cansleep)
		error = EINPROGRESS;
	if (error == EINPROGRESS)
		dmar_bus_schedule_dmamap(domain->dmar, map);
	return (error);
}

static int
dmar_bus_dmamap_load_ma(bus_dma_tag_t dmat, bus_dmamap_t map1,
    struct vm_page **ma, bus_size_t tlen, int ma_offs, int flags,
    bus_dma_segment_t *segs, int *segp)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;
	return (dmar_bus_dmamap_load_something(tag, map, ma, ma_offs, tlen,
	    flags, segs, segp));
}

static int
dmar_bus_dmamap_load_phys(bus_dma_tag_t dmat, bus_dmamap_t map1,
    vm_paddr_t buf, bus_size_t buflen, int flags, bus_dma_segment_t *segs,
    int *segp)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	vm_page_t *ma;
	vm_paddr_t pstart, pend;
	int error, i, ma_cnt, offset;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;
	pstart = trunc_page(buf);
	pend = round_page(buf + buflen);
	offset = buf & PAGE_MASK;
	ma_cnt = OFF_TO_IDX(pend - pstart);
	ma = malloc(sizeof(vm_page_t) * ma_cnt, M_DEVBUF, map->cansleep ?
	    M_WAITOK : M_NOWAIT);
	if (ma == NULL)
		return (ENOMEM);
	for (i = 0; i < ma_cnt; i++)
		ma[i] = PHYS_TO_VM_PAGE(pstart + i * PAGE_SIZE);
	error = dmar_bus_dmamap_load_something(tag, map, ma, offset, buflen,
	    flags, segs, segp);
	free(ma, M_DEVBUF);
	return (error);
}

static int
dmar_bus_dmamap_load_buffer(bus_dma_tag_t dmat, bus_dmamap_t map1, void *buf,
    bus_size_t buflen, pmap_t pmap, int flags, bus_dma_segment_t *segs,
    int *segp)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	vm_page_t *ma, fma;
	vm_paddr_t pstart, pend, paddr;
	int error, i, ma_cnt, offset;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;
	pstart = trunc_page((vm_offset_t)buf);
	pend = round_page((vm_offset_t)buf + buflen);
	offset = (vm_offset_t)buf & PAGE_MASK;
	ma_cnt = OFF_TO_IDX(pend - pstart);
	ma = malloc(sizeof(vm_page_t) * ma_cnt, M_DEVBUF, map->cansleep ?
	    M_WAITOK : M_NOWAIT);
	if (ma == NULL)
		return (ENOMEM);
	if (dumping) {
		/*
		 * If dumping, do not attempt to call
		 * PHYS_TO_VM_PAGE() at all.  It may return non-NULL
		 * but the vm_page returned might be not initialized,
		 * e.g. for the kernel itself.
		 */
		KASSERT(pmap == kernel_pmap, ("non-kernel address write"));
		fma = malloc(sizeof(struct vm_page) * ma_cnt, M_DEVBUF,
		    M_ZERO | (map->cansleep ? M_WAITOK : M_NOWAIT));
		if (fma == NULL) {
			free(ma, M_DEVBUF);
			return (ENOMEM);
		}
		for (i = 0; i < ma_cnt; i++, pstart += PAGE_SIZE) {
			paddr = pmap_kextract(pstart);
			vm_page_initfake(&fma[i], paddr, VM_MEMATTR_DEFAULT);
			ma[i] = &fma[i];
		}
	} else {
		fma = NULL;
		for (i = 0; i < ma_cnt; i++, pstart += PAGE_SIZE) {
			if (pmap == kernel_pmap)
				paddr = pmap_kextract(pstart);
			else
				paddr = pmap_extract(pmap, pstart);
			ma[i] = PHYS_TO_VM_PAGE(paddr);
			KASSERT(VM_PAGE_TO_PHYS(ma[i]) == paddr,
			    ("PHYS_TO_VM_PAGE failed %jx %jx m %p",
			    (uintmax_t)paddr, (uintmax_t)VM_PAGE_TO_PHYS(ma[i]),
			    ma[i]));
		}
	}
	error = dmar_bus_dmamap_load_something(tag, map, ma, offset, buflen,
	    flags, segs, segp);
	free(ma, M_DEVBUF);
	free(fma, M_DEVBUF);
	return (error);
}

static void
dmar_bus_dmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map1,
    struct memdesc *mem, bus_dmamap_callback_t *callback, void *callback_arg)
{
	struct bus_dmamap_dmar *map;

	if (map1 == NULL)
		return;
	map = (struct bus_dmamap_dmar *)map1;
	map->mem = *mem;
	map->tag = (struct bus_dma_tag_dmar *)dmat;
	map->callback = callback;
	map->callback_arg = callback_arg;
}

static bus_dma_segment_t *
dmar_bus_dmamap_complete(bus_dma_tag_t dmat, bus_dmamap_t map1,
    bus_dma_segment_t *segs, int nsegs, int error)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;

	if (!map->locked) {
		KASSERT(map->cansleep,
		    ("map not locked and not sleepable context %p", map));

		/*
		 * We are called from the delayed context.  Relock the
		 * driver.
		 */
		(tag->common.lockfunc)(tag->common.lockfuncarg, BUS_DMA_LOCK);
		map->locked = true;
	}

	if (segs == NULL)
		segs = tag->segments;
	return (segs);
}

/*
 * The limitations of busdma KPI forces the dmar to perform the actual
 * unload, consisting of the unmapping of the map entries page tables,
 * from the delayed context on i386, since page table page mapping
 * might require a sleep to be successfull.  The unfortunate
 * consequence is that the DMA requests can be served some time after
 * the bus_dmamap_unload() call returned.
 *
 * On amd64, we assume that sf allocation cannot fail.
 */
static void
dmar_bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map1)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	struct dmar_ctx *ctx;
	struct dmar_domain *domain;
#if defined(__amd64__)
	struct dmar_map_entries_tailq entries;
#endif

	tag = (struct bus_dma_tag_dmar *)dmat;
	map = (struct bus_dmamap_dmar *)map1;
	ctx = tag->ctx;
	domain = ctx->domain;
	atomic_add_long(&ctx->unloads, 1);

#if defined(__i386__)
	DMAR_DOMAIN_LOCK(domain);
	TAILQ_CONCAT(&domain->unload_entries, &map->map_entries, dmamap_link);
	DMAR_DOMAIN_UNLOCK(domain);
	taskqueue_enqueue(domain->dmar->delayed_taskqueue,
	    &domain->unload_task);
#else /* defined(__amd64__) */
	TAILQ_INIT(&entries);
	DMAR_DOMAIN_LOCK(domain);
	TAILQ_CONCAT(&entries, &map->map_entries, dmamap_link);
	DMAR_DOMAIN_UNLOCK(domain);
	THREAD_NO_SLEEPING();
	dmar_domain_unload(domain, &entries, false);
	THREAD_SLEEPING_OK();
	KASSERT(TAILQ_EMPTY(&entries), ("lazy dmar_ctx_unload %p", ctx));
#endif
}

static void
dmar_bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dmasync_op_t op)
{
}

struct bus_dma_impl bus_dma_dmar_impl = {
	.tag_create = dmar_bus_dma_tag_create,
	.tag_destroy = dmar_bus_dma_tag_destroy,
	.tag_set_domain = dmar_bus_dma_tag_set_domain,
	.map_create = dmar_bus_dmamap_create,
	.map_destroy = dmar_bus_dmamap_destroy,
	.mem_alloc = dmar_bus_dmamem_alloc,
	.mem_free = dmar_bus_dmamem_free,
	.load_phys = dmar_bus_dmamap_load_phys,
	.load_buffer = dmar_bus_dmamap_load_buffer,
	.load_ma = dmar_bus_dmamap_load_ma,
	.map_waitok = dmar_bus_dmamap_waitok,
	.map_complete = dmar_bus_dmamap_complete,
	.map_unload = dmar_bus_dmamap_unload,
	.map_sync = dmar_bus_dmamap_sync,
};

static void
dmar_bus_task_dmamap(void *arg, int pending)
{
	struct bus_dma_tag_dmar *tag;
	struct bus_dmamap_dmar *map;
	struct dmar_unit *unit;

	unit = arg;
	DMAR_LOCK(unit);
	while ((map = TAILQ_FIRST(&unit->delayed_maps)) != NULL) {
		TAILQ_REMOVE(&unit->delayed_maps, map, delay_link);
		DMAR_UNLOCK(unit);
		tag = map->tag;
		map->cansleep = true;
		map->locked = false;
		bus_dmamap_load_mem((bus_dma_tag_t)tag, (bus_dmamap_t)map,
		    &map->mem, map->callback, map->callback_arg,
		    BUS_DMA_WAITOK);
		map->cansleep = false;
		if (map->locked) {
			(tag->common.lockfunc)(tag->common.lockfuncarg,
			    BUS_DMA_UNLOCK);
		} else
			map->locked = true;
		map->cansleep = false;
		DMAR_LOCK(unit);
	}
	DMAR_UNLOCK(unit);
}

static void
dmar_bus_schedule_dmamap(struct dmar_unit *unit, struct bus_dmamap_dmar *map)
{

	map->locked = false;
	DMAR_LOCK(unit);
	TAILQ_INSERT_TAIL(&unit->delayed_maps, map, delay_link);
	DMAR_UNLOCK(unit);
	taskqueue_enqueue(unit->delayed_taskqueue, &unit->dmamap_load_task);
}

int
dmar_init_busdma(struct dmar_unit *unit)
{

	unit->dma_enabled = 1;
	TUNABLE_INT_FETCH("hw.dmar.dma", &unit->dma_enabled);
	TAILQ_INIT(&unit->delayed_maps);
	TASK_INIT(&unit->dmamap_load_task, 0, dmar_bus_task_dmamap, unit);
	unit->delayed_taskqueue = taskqueue_create("dmar", M_WAITOK,
	    taskqueue_thread_enqueue, &unit->delayed_taskqueue);
	taskqueue_start_threads(&unit->delayed_taskqueue, 1, PI_DISK,
	    "dmar%d busdma taskq", unit->unit);
	return (0);
}

void
dmar_fini_busdma(struct dmar_unit *unit)
{

	if (unit->delayed_taskqueue == NULL)
		return;

	taskqueue_drain(unit->delayed_taskqueue, &unit->dmamap_load_task);
	taskqueue_free(unit->delayed_taskqueue);
	unit->delayed_taskqueue = NULL;
}
