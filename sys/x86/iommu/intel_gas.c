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

#define	RB_AUGMENT(entry) dmar_gas_augment_entry(entry)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/rman.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <sys/vmem.h>
#include <dev/pci/pcivar.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/uma.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>

/*
 * Guest Address Space management.
 */

static uma_zone_t dmar_map_entry_zone;

static void
intel_gas_init(void)
{

	dmar_map_entry_zone = uma_zcreate("DMAR_MAP_ENTRY",
	    sizeof(struct dmar_map_entry), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NODUMP);
}
SYSINIT(intel_gas, SI_SUB_DRIVERS, SI_ORDER_FIRST, intel_gas_init, NULL);

struct dmar_map_entry *
dmar_gas_alloc_entry(struct dmar_domain *domain, u_int flags)
{
	struct dmar_map_entry *res;

	KASSERT((flags & ~(DMAR_PGF_WAITOK)) == 0,
	    ("unsupported flags %x", flags));

	res = uma_zalloc(dmar_map_entry_zone, ((flags & DMAR_PGF_WAITOK) !=
	    0 ? M_WAITOK : M_NOWAIT) | M_ZERO);
	if (res != NULL) {
		res->domain = domain;
		atomic_add_int(&domain->entries_cnt, 1);
	}
	return (res);
}

void
dmar_gas_free_entry(struct dmar_domain *domain, struct dmar_map_entry *entry)
{

	KASSERT(domain == entry->domain,
	    ("mismatched free domain %p entry %p entry->domain %p", domain,
	    entry, entry->domain));
	atomic_subtract_int(&domain->entries_cnt, 1);
	uma_zfree(dmar_map_entry_zone, entry);
}

static int
dmar_gas_cmp_entries(struct dmar_map_entry *a, struct dmar_map_entry *b)
{

	/* Last entry have zero size, so <= */
	KASSERT(a->start <= a->end, ("inverted entry %p (%jx, %jx)",
	    a, (uintmax_t)a->start, (uintmax_t)a->end));
	KASSERT(b->start <= b->end, ("inverted entry %p (%jx, %jx)",
	    b, (uintmax_t)b->start, (uintmax_t)b->end));
	KASSERT(a->end <= b->start || b->end <= a->start ||
	    a->end == a->start || b->end == b->start,
	    ("overlapping entries %p (%jx, %jx) %p (%jx, %jx)",
	    a, (uintmax_t)a->start, (uintmax_t)a->end,
	    b, (uintmax_t)b->start, (uintmax_t)b->end));

	if (a->end < b->end)
		return (-1);
	else if (b->end < a->end)
		return (1);
	return (0);
}

static void
dmar_gas_augment_entry(struct dmar_map_entry *entry)
{
	struct dmar_map_entry *l, *r;

	for (; entry != NULL; entry = RB_PARENT(entry, rb_entry)) {
		l = RB_LEFT(entry, rb_entry);
		r = RB_RIGHT(entry, rb_entry);
		if (l == NULL && r == NULL) {
			entry->free_down = entry->free_after;
		} else if (l == NULL && r != NULL) {
			entry->free_down = MAX(entry->free_after, r->free_down);
		} else if (/*l != NULL && */ r == NULL) {
			entry->free_down = MAX(entry->free_after, l->free_down);
		} else /* if (l != NULL && r != NULL) */ {
			entry->free_down = MAX(entry->free_after, l->free_down);
			entry->free_down = MAX(entry->free_down, r->free_down);
		}
	}
}

RB_GENERATE(dmar_gas_entries_tree, dmar_map_entry, rb_entry,
    dmar_gas_cmp_entries);

static void
dmar_gas_fix_free(struct dmar_domain *domain, struct dmar_map_entry *entry)
{
	struct dmar_map_entry *next;

	next = RB_NEXT(dmar_gas_entries_tree, &domain->rb_root, entry);
	entry->free_after = (next != NULL ? next->start : domain->end) -
	    entry->end;
	dmar_gas_augment_entry(entry);
}

#ifdef INVARIANTS
static void
dmar_gas_check_free(struct dmar_domain *domain)
{
	struct dmar_map_entry *entry, *next, *l, *r;
	dmar_gaddr_t v;

	RB_FOREACH(entry, dmar_gas_entries_tree, &domain->rb_root) {
		KASSERT(domain == entry->domain,
		    ("mismatched free domain %p entry %p entry->domain %p",
		    domain, entry, entry->domain));
		next = RB_NEXT(dmar_gas_entries_tree, &domain->rb_root, entry);
		if (next == NULL) {
			MPASS(entry->free_after == domain->end - entry->end);
		} else {
			MPASS(entry->free_after = next->start - entry->end);
			MPASS(entry->end <= next->start);
		}
		l = RB_LEFT(entry, rb_entry);
		r = RB_RIGHT(entry, rb_entry);
		if (l == NULL && r == NULL) {
			MPASS(entry->free_down == entry->free_after);
		} else if (l == NULL && r != NULL) {
			MPASS(entry->free_down = MAX(entry->free_after,
			    r->free_down));
		} else if (r == NULL) {
			MPASS(entry->free_down = MAX(entry->free_after,
			    l->free_down));
		} else {
			v = MAX(entry->free_after, l->free_down);
			v = MAX(v, r->free_down);
			MPASS(entry->free_down == v);
		}
	}
}
#endif

static bool
dmar_gas_rb_insert(struct dmar_domain *domain, struct dmar_map_entry *entry)
{
	struct dmar_map_entry *prev, *found;

	found = RB_INSERT(dmar_gas_entries_tree, &domain->rb_root, entry);
	dmar_gas_fix_free(domain, entry);
	prev = RB_PREV(dmar_gas_entries_tree, &domain->rb_root, entry);
	if (prev != NULL)
		dmar_gas_fix_free(domain, prev);
	return (found == NULL);
}

static void
dmar_gas_rb_remove(struct dmar_domain *domain, struct dmar_map_entry *entry)
{
	struct dmar_map_entry *prev;

	prev = RB_PREV(dmar_gas_entries_tree, &domain->rb_root, entry);
	RB_REMOVE(dmar_gas_entries_tree, &domain->rb_root, entry);
	if (prev != NULL)
		dmar_gas_fix_free(domain, prev);
}

void
dmar_gas_init_domain(struct dmar_domain *domain)
{
	struct dmar_map_entry *begin, *end;

	begin = dmar_gas_alloc_entry(domain, DMAR_PGF_WAITOK);
	end = dmar_gas_alloc_entry(domain, DMAR_PGF_WAITOK);

	DMAR_DOMAIN_LOCK(domain);
	KASSERT(domain->entries_cnt == 2, ("dirty domain %p", domain));
	KASSERT(RB_EMPTY(&domain->rb_root), ("non-empty entries %p", domain));

	begin->start = 0;
	begin->end = DMAR_PAGE_SIZE;
	begin->free_after = domain->end - begin->end;
	begin->flags = DMAR_MAP_ENTRY_PLACE | DMAR_MAP_ENTRY_UNMAPPED;
	dmar_gas_rb_insert(domain, begin);

	end->start = domain->end;
	end->end = domain->end;
	end->free_after = 0;
	end->flags = DMAR_MAP_ENTRY_PLACE | DMAR_MAP_ENTRY_UNMAPPED;
	dmar_gas_rb_insert(domain, end);

	domain->first_place = begin;
	domain->last_place = end;
	domain->flags |= DMAR_DOMAIN_GAS_INITED;
	DMAR_DOMAIN_UNLOCK(domain);
}

void
dmar_gas_fini_domain(struct dmar_domain *domain)
{
	struct dmar_map_entry *entry, *entry1;

	DMAR_DOMAIN_ASSERT_LOCKED(domain);
	KASSERT(domain->entries_cnt == 2, ("domain still in use %p", domain));

	entry = RB_MIN(dmar_gas_entries_tree, &domain->rb_root);
	KASSERT(entry->start == 0, ("start entry start %p", domain));
	KASSERT(entry->end == DMAR_PAGE_SIZE, ("start entry end %p", domain));
	KASSERT(entry->flags == DMAR_MAP_ENTRY_PLACE,
	    ("start entry flags %p", domain));
	RB_REMOVE(dmar_gas_entries_tree, &domain->rb_root, entry);
	dmar_gas_free_entry(domain, entry);

	entry = RB_MAX(dmar_gas_entries_tree, &domain->rb_root);
	KASSERT(entry->start == domain->end, ("end entry start %p", domain));
	KASSERT(entry->end == domain->end, ("end entry end %p", domain));
	KASSERT(entry->free_after == 0, ("end entry free_after %p", domain));
	KASSERT(entry->flags == DMAR_MAP_ENTRY_PLACE,
	    ("end entry flags %p", domain));
	RB_REMOVE(dmar_gas_entries_tree, &domain->rb_root, entry);
	dmar_gas_free_entry(domain, entry);

	RB_FOREACH_SAFE(entry, dmar_gas_entries_tree, &domain->rb_root,
	    entry1) {
		KASSERT((entry->flags & DMAR_MAP_ENTRY_RMRR) != 0,
		    ("non-RMRR entry left %p", domain));
		RB_REMOVE(dmar_gas_entries_tree, &domain->rb_root, entry);
		dmar_gas_free_entry(domain, entry);
	}
}

struct dmar_gas_match_args {
	struct dmar_domain *domain;
	dmar_gaddr_t size;
	int offset;
	const struct bus_dma_tag_common *common;
	u_int gas_flags;
	struct dmar_map_entry *entry;
};

static bool
dmar_gas_match_one(struct dmar_gas_match_args *a, struct dmar_map_entry *prev,
    dmar_gaddr_t end)
{
	dmar_gaddr_t bs, start;

	if (a->entry->start + a->size > end)
		return (false);

	/* DMAR_PAGE_SIZE to create gap after new entry. */
	if (a->entry->start < prev->end + DMAR_PAGE_SIZE ||
	    a->entry->start + a->size + a->offset + DMAR_PAGE_SIZE >
	    prev->end + prev->free_after)
		return (false);

	/* No boundary crossing. */
	if (dmar_test_boundary(a->entry->start + a->offset, a->size,
	    a->common->boundary))
		return (true);

	/*
	 * The start + offset to start + offset + size region crosses
	 * the boundary.  Check if there is enough space after the
	 * next boundary after the prev->end.
	 */
	bs = rounddown2(a->entry->start + a->offset + a->common->boundary,
	    a->common->boundary);
	start = roundup2(bs, a->common->alignment);
	/* DMAR_PAGE_SIZE to create gap after new entry. */
	if (start + a->offset + a->size + DMAR_PAGE_SIZE <=
	    prev->end + prev->free_after &&
	    start + a->offset + a->size <= end &&
	    dmar_test_boundary(start + a->offset, a->size,
	    a->common->boundary)) {
		a->entry->start = start;
		return (true);
	}

	/*
	 * Not enough space to align at the requested boundary, or
	 * boundary is smaller than the size, but allowed to split.
	 * We already checked that start + size does not overlap end.
	 *
	 * XXXKIB. It is possible that bs is exactly at the start of
	 * the next entry, then we do not have gap.  Ignore for now.
	 */
	if ((a->gas_flags & DMAR_GM_CANSPLIT) != 0) {
		a->size = bs - a->entry->start;
		return (true);
	}

	return (false);
}

static void
dmar_gas_match_insert(struct dmar_gas_match_args *a,
    struct dmar_map_entry *prev)
{
	struct dmar_map_entry *next;
	bool found;

	/*
	 * The prev->end is always aligned on the page size, which
	 * causes page alignment for the entry->start too.  The size
	 * is checked to be multiple of the page size.
	 *
	 * The page sized gap is created between consequent
	 * allocations to ensure that out-of-bounds accesses fault.
	 */
	a->entry->end = a->entry->start + a->size;

	next = RB_NEXT(dmar_gas_entries_tree, &a->domain->rb_root, prev);
	KASSERT(next->start >= a->entry->end &&
	    next->start - a->entry->start >= a->size &&
	    prev->end <= a->entry->end,
	    ("dmar_gas_match_insert hole failed %p prev (%jx, %jx) "
	    "free_after %jx next (%jx, %jx) entry (%jx, %jx)", a->domain,
	    (uintmax_t)prev->start, (uintmax_t)prev->end,
	    (uintmax_t)prev->free_after,
	    (uintmax_t)next->start, (uintmax_t)next->end,
	    (uintmax_t)a->entry->start, (uintmax_t)a->entry->end));

	prev->free_after = a->entry->start - prev->end;
	a->entry->free_after = next->start - a->entry->end;

	found = dmar_gas_rb_insert(a->domain, a->entry);
	KASSERT(found, ("found dup %p start %jx size %jx",
	    a->domain, (uintmax_t)a->entry->start, (uintmax_t)a->size));
	a->entry->flags = DMAR_MAP_ENTRY_MAP;

	KASSERT(RB_PREV(dmar_gas_entries_tree, &a->domain->rb_root,
	    a->entry) == prev,
	    ("entry %p prev %p inserted prev %p", a->entry, prev,
	    RB_PREV(dmar_gas_entries_tree, &a->domain->rb_root, a->entry)));
	KASSERT(RB_NEXT(dmar_gas_entries_tree, &a->domain->rb_root,
	    a->entry) == next,
	    ("entry %p next %p inserted next %p", a->entry, next,
	    RB_NEXT(dmar_gas_entries_tree, &a->domain->rb_root, a->entry)));
}

static int
dmar_gas_lowermatch(struct dmar_gas_match_args *a, struct dmar_map_entry *prev)
{
	struct dmar_map_entry *l;
	int ret;

	if (prev->end < a->common->lowaddr) {
		a->entry->start = roundup2(prev->end + DMAR_PAGE_SIZE,
		    a->common->alignment);
		if (dmar_gas_match_one(a, prev, a->common->lowaddr)) {
			dmar_gas_match_insert(a, prev);
			return (0);
		}
	}
	if (prev->free_down < a->size + a->offset + DMAR_PAGE_SIZE)
		return (ENOMEM);
	l = RB_LEFT(prev, rb_entry);
	if (l != NULL) {
		ret = dmar_gas_lowermatch(a, l);
		if (ret == 0)
			return (0);
	}
	l = RB_RIGHT(prev, rb_entry);
	if (l != NULL)
		return (dmar_gas_lowermatch(a, l));
	return (ENOMEM);
}

static int
dmar_gas_uppermatch(struct dmar_gas_match_args *a)
{
	struct dmar_map_entry *next, *prev, find_entry;

	find_entry.start = a->common->highaddr;
	next = RB_NFIND(dmar_gas_entries_tree, &a->domain->rb_root,
	    &find_entry);
	if (next == NULL)
		return (ENOMEM);
	prev = RB_PREV(dmar_gas_entries_tree, &a->domain->rb_root, next);
	KASSERT(prev != NULL, ("no prev %p %jx", a->domain,
	    (uintmax_t)find_entry.start));
	for (;;) {
		a->entry->start = prev->start + DMAR_PAGE_SIZE;
		if (a->entry->start < a->common->highaddr)
			a->entry->start = a->common->highaddr;
		a->entry->start = roundup2(a->entry->start,
		    a->common->alignment);
		if (dmar_gas_match_one(a, prev, a->domain->end)) {
			dmar_gas_match_insert(a, prev);
			return (0);
		}

		/*
		 * XXXKIB.  This falls back to linear iteration over
		 * the free space in the high region.  But high
		 * regions are almost unused, the code should be
		 * enough to cover the case, although in the
		 * non-optimal way.
		 */
		prev = next;
		next = RB_NEXT(dmar_gas_entries_tree, &a->domain->rb_root,
		    prev);
		KASSERT(next != NULL, ("no next %p %jx", a->domain,
		    (uintmax_t)find_entry.start));
		if (next->end >= a->domain->end)
			return (ENOMEM);
	}
}

static int
dmar_gas_find_space(struct dmar_domain *domain,
    const struct bus_dma_tag_common *common, dmar_gaddr_t size,
    int offset, u_int flags, struct dmar_map_entry *entry)
{
	struct dmar_gas_match_args a;
	int error;

	DMAR_DOMAIN_ASSERT_LOCKED(domain);
	KASSERT(entry->flags == 0, ("dirty entry %p %p", domain, entry));
	KASSERT((size & DMAR_PAGE_MASK) == 0, ("size %jx", (uintmax_t)size));

	a.domain = domain;
	a.size = size;
	a.offset = offset;
	a.common = common;
	a.gas_flags = flags;
	a.entry = entry;

	/* Handle lower region. */
	if (common->lowaddr > 0) {
		error = dmar_gas_lowermatch(&a, RB_ROOT(&domain->rb_root));
		if (error == 0)
			return (0);
		KASSERT(error == ENOMEM,
		    ("error %d from dmar_gas_lowermatch", error));
	}
	/* Handle upper region. */
	if (common->highaddr >= domain->end)
		return (ENOMEM);
	error = dmar_gas_uppermatch(&a);
	KASSERT(error == ENOMEM,
	    ("error %d from dmar_gas_uppermatch", error));
	return (error);
}

static int
dmar_gas_alloc_region(struct dmar_domain *domain, struct dmar_map_entry *entry,
    u_int flags)
{
	struct dmar_map_entry *next, *prev;
	bool found;

	DMAR_DOMAIN_ASSERT_LOCKED(domain);

	if ((entry->start & DMAR_PAGE_MASK) != 0 ||
	    (entry->end & DMAR_PAGE_MASK) != 0)
		return (EINVAL);
	if (entry->start >= entry->end)
		return (EINVAL);
	if (entry->end >= domain->end)
		return (EINVAL);

	next = RB_NFIND(dmar_gas_entries_tree, &domain->rb_root, entry);
	KASSERT(next != NULL, ("next must be non-null %p %jx", domain,
	    (uintmax_t)entry->start));
	prev = RB_PREV(dmar_gas_entries_tree, &domain->rb_root, next);
	/* prev could be NULL */

	/*
	 * Adapt to broken BIOSes which specify overlapping RMRR
	 * entries.
	 *
	 * XXXKIB: this does not handle a case when prev or next
	 * entries are completely covered by the current one, which
	 * extends both ways.
	 */
	if (prev != NULL && prev->end > entry->start &&
	    (prev->flags & DMAR_MAP_ENTRY_PLACE) == 0) {
		if ((prev->flags & DMAR_MAP_ENTRY_RMRR) == 0)
			return (EBUSY);
		entry->start = prev->end;
	}
	if (next != NULL && next->start < entry->end &&
	    (next->flags & DMAR_MAP_ENTRY_PLACE) == 0) {
		if ((next->flags & DMAR_MAP_ENTRY_RMRR) == 0)
			return (EBUSY);
		entry->end = next->start;
	}
	if (entry->end == entry->start)
		return (0);

	if (prev != NULL && prev->end > entry->start) {
		/* This assumes that prev is the placeholder entry. */
		dmar_gas_rb_remove(domain, prev);
		prev = NULL;
	}
	if (next != NULL && next->start < entry->end) {
		dmar_gas_rb_remove(domain, next);
		next = NULL;
	}

	found = dmar_gas_rb_insert(domain, entry);
	KASSERT(found, ("found RMRR dup %p start %jx end %jx",
	    domain, (uintmax_t)entry->start, (uintmax_t)entry->end));
	entry->flags = DMAR_MAP_ENTRY_RMRR;

#ifdef INVARIANTS
	struct dmar_map_entry *ip, *in;
	ip = RB_PREV(dmar_gas_entries_tree, &domain->rb_root, entry);
	in = RB_NEXT(dmar_gas_entries_tree, &domain->rb_root, entry);
	KASSERT(prev == NULL || ip == prev,
	    ("RMRR %p (%jx %jx) prev %p (%jx %jx) ins prev %p (%jx %jx)",
	    entry, entry->start, entry->end, prev,
	    prev == NULL ? 0 : prev->start, prev == NULL ? 0 : prev->end,
	    ip, ip == NULL ? 0 : ip->start, ip == NULL ? 0 : ip->end));
	KASSERT(next == NULL || in == next,
	    ("RMRR %p (%jx %jx) next %p (%jx %jx) ins next %p (%jx %jx)",
	    entry, entry->start, entry->end, next,
	    next == NULL ? 0 : next->start, next == NULL ? 0 : next->end,
	    in, in == NULL ? 0 : in->start, in == NULL ? 0 : in->end));
#endif

	return (0);
}

void
dmar_gas_free_space(struct dmar_domain *domain, struct dmar_map_entry *entry)
{

	DMAR_DOMAIN_ASSERT_LOCKED(domain);
	KASSERT((entry->flags & (DMAR_MAP_ENTRY_PLACE | DMAR_MAP_ENTRY_RMRR |
	    DMAR_MAP_ENTRY_MAP)) == DMAR_MAP_ENTRY_MAP,
	    ("permanent entry %p %p", domain, entry));

	dmar_gas_rb_remove(domain, entry);
	entry->flags &= ~DMAR_MAP_ENTRY_MAP;
#ifdef INVARIANTS
	if (dmar_check_free)
		dmar_gas_check_free(domain);
#endif
}

void
dmar_gas_free_region(struct dmar_domain *domain, struct dmar_map_entry *entry)
{
	struct dmar_map_entry *next, *prev;

	DMAR_DOMAIN_ASSERT_LOCKED(domain);
	KASSERT((entry->flags & (DMAR_MAP_ENTRY_PLACE | DMAR_MAP_ENTRY_RMRR |
	    DMAR_MAP_ENTRY_MAP)) == DMAR_MAP_ENTRY_RMRR,
	    ("non-RMRR entry %p %p", domain, entry));

	prev = RB_PREV(dmar_gas_entries_tree, &domain->rb_root, entry);
	next = RB_NEXT(dmar_gas_entries_tree, &domain->rb_root, entry);
	dmar_gas_rb_remove(domain, entry);
	entry->flags &= ~DMAR_MAP_ENTRY_RMRR;

	if (prev == NULL)
		dmar_gas_rb_insert(domain, domain->first_place);
	if (next == NULL)
		dmar_gas_rb_insert(domain, domain->last_place);
}

int
dmar_gas_map(struct dmar_domain *domain,
    const struct bus_dma_tag_common *common, dmar_gaddr_t size, int offset,
    u_int eflags, u_int flags, vm_page_t *ma, struct dmar_map_entry **res)
{
	struct dmar_map_entry *entry;
	int error;

	KASSERT((flags & ~(DMAR_GM_CANWAIT | DMAR_GM_CANSPLIT)) == 0,
	    ("invalid flags 0x%x", flags));

	entry = dmar_gas_alloc_entry(domain, (flags & DMAR_GM_CANWAIT) != 0 ?
	    DMAR_PGF_WAITOK : 0);
	if (entry == NULL)
		return (ENOMEM);
	DMAR_DOMAIN_LOCK(domain);
	error = dmar_gas_find_space(domain, common, size, offset, flags,
	    entry);
	if (error == ENOMEM) {
		DMAR_DOMAIN_UNLOCK(domain);
		dmar_gas_free_entry(domain, entry);
		return (error);
	}
#ifdef INVARIANTS
	if (dmar_check_free)
		dmar_gas_check_free(domain);
#endif
	KASSERT(error == 0,
	    ("unexpected error %d from dmar_gas_find_entry", error));
	KASSERT(entry->end < domain->end, ("allocated GPA %jx, max GPA %jx",
	    (uintmax_t)entry->end, (uintmax_t)domain->end));
	entry->flags |= eflags;
	DMAR_DOMAIN_UNLOCK(domain);

	error = domain_map_buf(domain, entry->start, entry->end - entry->start,
	    ma,
	    ((eflags & DMAR_MAP_ENTRY_READ) != 0 ? DMAR_PTE_R : 0) |
	    ((eflags & DMAR_MAP_ENTRY_WRITE) != 0 ? DMAR_PTE_W : 0) |
	    ((eflags & DMAR_MAP_ENTRY_SNOOP) != 0 ? DMAR_PTE_SNP : 0) |
	    ((eflags & DMAR_MAP_ENTRY_TM) != 0 ? DMAR_PTE_TM : 0),
	    (flags & DMAR_GM_CANWAIT) != 0 ? DMAR_PGF_WAITOK : 0);
	if (error == ENOMEM) {
		dmar_domain_unload_entry(entry, true);
		return (error);
	}
	KASSERT(error == 0,
	    ("unexpected error %d from domain_map_buf", error));

	*res = entry;
	return (0);
}

int
dmar_gas_map_region(struct dmar_domain *domain, struct dmar_map_entry *entry,
    u_int eflags, u_int flags, vm_page_t *ma)
{
	dmar_gaddr_t start;
	int error;

	KASSERT(entry->flags == 0, ("used RMRR entry %p %p %x", domain,
	    entry, entry->flags));
	KASSERT((flags & ~(DMAR_GM_CANWAIT)) == 0,
	    ("invalid flags 0x%x", flags));

	start = entry->start;
	DMAR_DOMAIN_LOCK(domain);
	error = dmar_gas_alloc_region(domain, entry, flags);
	if (error != 0) {
		DMAR_DOMAIN_UNLOCK(domain);
		return (error);
	}
	entry->flags |= eflags;
	DMAR_DOMAIN_UNLOCK(domain);
	if (entry->end == entry->start)
		return (0);

	error = domain_map_buf(domain, entry->start, entry->end - entry->start,
	    ma + OFF_TO_IDX(start - entry->start),
	    ((eflags & DMAR_MAP_ENTRY_READ) != 0 ? DMAR_PTE_R : 0) |
	    ((eflags & DMAR_MAP_ENTRY_WRITE) != 0 ? DMAR_PTE_W : 0) |
	    ((eflags & DMAR_MAP_ENTRY_SNOOP) != 0 ? DMAR_PTE_SNP : 0) |
	    ((eflags & DMAR_MAP_ENTRY_TM) != 0 ? DMAR_PTE_TM : 0),
	    (flags & DMAR_GM_CANWAIT) != 0 ? DMAR_PGF_WAITOK : 0);
	if (error == ENOMEM) {
		dmar_domain_unload_entry(entry, false);
		return (error);
	}
	KASSERT(error == 0,
	    ("unexpected error %d from domain_map_buf", error));

	return (0);
}

int
dmar_gas_reserve_region(struct dmar_domain *domain, dmar_gaddr_t start,
    dmar_gaddr_t end)
{
	struct dmar_map_entry *entry;
	int error;

	entry = dmar_gas_alloc_entry(domain, DMAR_PGF_WAITOK);
	entry->start = start;
	entry->end = end;
	DMAR_DOMAIN_LOCK(domain);
	error = dmar_gas_alloc_region(domain, entry, DMAR_GM_CANWAIT);
	if (error == 0)
		entry->flags |= DMAR_MAP_ENTRY_UNMAPPED;
	DMAR_DOMAIN_UNLOCK(domain);
	if (error != 0)
		dmar_gas_free_entry(domain, entry);
	return (error);
}
