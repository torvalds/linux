/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Nathan Whitehorn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/uma.h>
#include <vm/vm.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>

#include <machine/md_var.h>
#include <machine/platform.h>
#include <machine/vmparam.h>

uintptr_t moea64_get_unique_vsid(void);
void moea64_release_vsid(uint64_t vsid);
static void slb_zone_init(void *);

static uma_zone_t slbt_zone;
static uma_zone_t slb_cache_zone;
int n_slbs = 64;

SYSINIT(slb_zone_init, SI_SUB_KMEM, SI_ORDER_ANY, slb_zone_init, NULL);

struct slbtnode {
	uint16_t	ua_alloc;
	uint8_t		ua_level;
	/* Only 36 bits needed for full 64-bit address space. */
	uint64_t	ua_base;
	union {
		struct slbtnode	*ua_child[16];
		struct slb	slb_entries[16];
	} u;
};

/*
 * For a full 64-bit address space, there are 36 bits in play in an
 * esid, so 8 levels, with the leaf being at level 0.
 *
 * |3333|3322|2222|2222|1111|1111|11  |    |    |  esid
 * |5432|1098|7654|3210|9876|5432|1098|7654|3210|  bits
 * +----+----+----+----+----+----+----+----+----+--------
 * | 8  | 7  | 6  | 5  | 4  | 3  | 2  | 1  | 0  | level
 */
#define UAD_ROOT_LEVEL  8
#define UAD_LEAF_LEVEL  0

static inline int
esid2idx(uint64_t esid, int level)
{
	int shift;

	shift = level * 4;
	return ((esid >> shift) & 0xF);
}

/*
 * The ua_base field should have 0 bits after the first 4*(level+1)
 * bits; i.e. only
 */
#define uad_baseok(ua)                          \
	(esid2base(ua->ua_base, ua->ua_level) == ua->ua_base)


static inline uint64_t
esid2base(uint64_t esid, int level)
{
	uint64_t mask;
	int shift;

	shift = (level + 1) * 4;
	mask = ~((1ULL << shift) - 1);
	return (esid & mask);
}

/*
 * Allocate a new leaf node for the specified esid/vmhandle from the
 * parent node.
 */
static struct slb *
make_new_leaf(uint64_t esid, uint64_t slbv, struct slbtnode *parent)
{
	struct slbtnode *child;
	struct slb *retval;
	int idx;

	idx = esid2idx(esid, parent->ua_level);
	KASSERT(parent->u.ua_child[idx] == NULL, ("Child already exists!"));

	/* unlock and M_WAITOK and loop? */
	child = uma_zalloc(slbt_zone, M_NOWAIT | M_ZERO);
	KASSERT(child != NULL, ("unhandled NULL case"));

	child->ua_level = UAD_LEAF_LEVEL;
	child->ua_base = esid2base(esid, child->ua_level);
	idx = esid2idx(esid, child->ua_level);
	child->u.slb_entries[idx].slbv = slbv;
	child->u.slb_entries[idx].slbe = (esid << SLBE_ESID_SHIFT) | SLBE_VALID;
	setbit(&child->ua_alloc, idx);

	retval = &child->u.slb_entries[idx];

	/*
	 * The above stores must be visible before the next one, so
	 * that a lockless searcher always sees a valid path through
	 * the tree.
	 */
	powerpc_lwsync();

	idx = esid2idx(esid, parent->ua_level);
	parent->u.ua_child[idx] = child;
	setbit(&parent->ua_alloc, idx);

	return (retval);
}

/*
 * Allocate a new intermediate node to fit between the parent and
 * esid.
 */
static struct slbtnode*
make_intermediate(uint64_t esid, struct slbtnode *parent)
{
	struct slbtnode *child, *inter;
	int idx, level;

	idx = esid2idx(esid, parent->ua_level);
	child = parent->u.ua_child[idx];
	KASSERT(esid2base(esid, child->ua_level) != child->ua_base,
	    ("No need for an intermediate node?"));

	/*
	 * Find the level where the existing child and our new esid
	 * meet.  It must be lower than parent->ua_level or we would
	 * have chosen a different index in parent.
	 */
	level = child->ua_level + 1;
	while (esid2base(esid, level) !=
	    esid2base(child->ua_base, level))
		level++;
	KASSERT(level < parent->ua_level,
	    ("Found splitting level %d for %09jx and %09jx, "
	    "but it's the same as %p's",
	    level, esid, child->ua_base, parent));

	/* unlock and M_WAITOK and loop? */
	inter = uma_zalloc(slbt_zone, M_NOWAIT | M_ZERO);
	KASSERT(inter != NULL, ("unhandled NULL case"));

	/* Set up intermediate node to point to child ... */
	inter->ua_level = level;
	inter->ua_base = esid2base(esid, inter->ua_level);
	idx = esid2idx(child->ua_base, inter->ua_level);
	inter->u.ua_child[idx] = child;
	setbit(&inter->ua_alloc, idx);
	powerpc_lwsync();

	/* Set up parent to point to intermediate node ... */
	idx = esid2idx(inter->ua_base, parent->ua_level);
	parent->u.ua_child[idx] = inter;
	setbit(&parent->ua_alloc, idx);

	return (inter);
}

uint64_t
kernel_va_to_slbv(vm_offset_t va)
{
	uint64_t slbv;

	/* Set kernel VSID to deterministic value */
	slbv = (KERNEL_VSID((uintptr_t)va >> ADDR_SR_SHFT)) << SLBV_VSID_SHIFT;

	/* 
	 * Figure out if this is a large-page mapping.
	 */
	if (hw_direct_map && va > DMAP_BASE_ADDRESS && va < DMAP_MAX_ADDRESS) {
		/*
		 * XXX: If we have set up a direct map, assumes
		 * all physical memory is mapped with large pages.
		 */

		if (mem_valid(DMAP_TO_PHYS(va), 0) == 0)
			slbv |= SLBV_L;
	}
		
	return (slbv);
}

struct slb *
user_va_to_slb_entry(pmap_t pm, vm_offset_t va)
{
	uint64_t esid = va >> ADDR_SR_SHFT;
	struct slbtnode *ua;
	int idx;

	ua = pm->pm_slb_tree_root;

	for (;;) {
		KASSERT(uad_baseok(ua), ("uad base %016jx level %d bad!",
		    ua->ua_base, ua->ua_level));
		idx = esid2idx(esid, ua->ua_level);

		/*
		 * This code is specific to ppc64 where a load is
		 * atomic, so no need for atomic_load macro.
		 */
		if (ua->ua_level == UAD_LEAF_LEVEL)
			return ((ua->u.slb_entries[idx].slbe & SLBE_VALID) ?
			    &ua->u.slb_entries[idx] : NULL);

		/*
		 * The following accesses are implicitly ordered under the POWER
		 * ISA by load dependencies (the store ordering is provided by
		 * the powerpc_lwsync() calls elsewhere) and so are run without
		 * barriers.
		 */
		ua = ua->u.ua_child[idx];
		if (ua == NULL ||
		    esid2base(esid, ua->ua_level) != ua->ua_base)
			return (NULL);
	}

	return (NULL);
}

uint64_t
va_to_vsid(pmap_t pm, vm_offset_t va)
{
	struct slb *entry;

	/* Shortcut kernel case */
	if (pm == kernel_pmap)
		return (KERNEL_VSID((uintptr_t)va >> ADDR_SR_SHFT));

	/*
	 * If there is no vsid for this VA, we need to add a new entry
	 * to the PMAP's segment table.
	 */

	entry = user_va_to_slb_entry(pm, va);

	if (entry == NULL)
		return (allocate_user_vsid(pm,
		    (uintptr_t)va >> ADDR_SR_SHFT, 0));

	return ((entry->slbv & SLBV_VSID_MASK) >> SLBV_VSID_SHIFT);
}

uint64_t
allocate_user_vsid(pmap_t pm, uint64_t esid, int large)
{
	uint64_t vsid, slbv;
	struct slbtnode *ua, *next, *inter;
	struct slb *slb;
	int idx;

	KASSERT(pm != kernel_pmap, ("Attempting to allocate a kernel VSID"));

	PMAP_LOCK_ASSERT(pm, MA_OWNED);
	vsid = moea64_get_unique_vsid();

	slbv = vsid << SLBV_VSID_SHIFT;
	if (large)
		slbv |= SLBV_L;

	ua = pm->pm_slb_tree_root;

	/* Descend to the correct leaf or NULL pointer. */
	for (;;) {
		KASSERT(uad_baseok(ua),
		   ("uad base %09jx level %d bad!", ua->ua_base, ua->ua_level));
		idx = esid2idx(esid, ua->ua_level);

		if (ua->ua_level == UAD_LEAF_LEVEL) {
			ua->u.slb_entries[idx].slbv = slbv;
			eieio();
			ua->u.slb_entries[idx].slbe = (esid << SLBE_ESID_SHIFT)
			    | SLBE_VALID;
			setbit(&ua->ua_alloc, idx);
			slb = &ua->u.slb_entries[idx];
			break;
		}

		next = ua->u.ua_child[idx];
		if (next == NULL) {
			slb = make_new_leaf(esid, slbv, ua);
			break;
                }

		/*
		 * Check if the next item down has an okay ua_base.
		 * If not, we need to allocate an intermediate node.
		 */
		if (esid2base(esid, next->ua_level) != next->ua_base) {
			inter = make_intermediate(esid, ua);
			slb = make_new_leaf(esid, slbv, inter);
			break;
		}

		ua = next;
	}

	/*
	 * Someone probably wants this soon, and it may be a wired
	 * SLB mapping, so pre-spill this entry.
	 */
	eieio();
	slb_insert_user(pm, slb);

	return (vsid);
}

void
free_vsid(pmap_t pm, uint64_t esid, int large)
{
	struct slbtnode *ua;
	int idx;

	PMAP_LOCK_ASSERT(pm, MA_OWNED);

	ua = pm->pm_slb_tree_root;
	/* Descend to the correct leaf. */
	for (;;) {
		KASSERT(uad_baseok(ua),
		   ("uad base %09jx level %d bad!", ua->ua_base, ua->ua_level));
		
		idx = esid2idx(esid, ua->ua_level);
		if (ua->ua_level == UAD_LEAF_LEVEL) {
			ua->u.slb_entries[idx].slbv = 0;
			eieio();
			ua->u.slb_entries[idx].slbe = 0;
			clrbit(&ua->ua_alloc, idx);
			return;
		}

		ua = ua->u.ua_child[idx];
		if (ua == NULL ||
		    esid2base(esid, ua->ua_level) != ua->ua_base) {
			/* Perhaps just return instead of assert? */
			KASSERT(0,
			    ("Asked to remove an entry that was never inserted!"));
			return;
		}
	}
}

static void
free_slb_tree_node(struct slbtnode *ua)
{
	int idx;

	for (idx = 0; idx < 16; idx++) {
		if (ua->ua_level != UAD_LEAF_LEVEL) {
			if (ua->u.ua_child[idx] != NULL)
				free_slb_tree_node(ua->u.ua_child[idx]);
		} else {
			if (ua->u.slb_entries[idx].slbv != 0)
				moea64_release_vsid(ua->u.slb_entries[idx].slbv
				    >> SLBV_VSID_SHIFT);
		}
	}

	uma_zfree(slbt_zone, ua);
}

void
slb_free_tree(pmap_t pm)
{

	free_slb_tree_node(pm->pm_slb_tree_root);
}

struct slbtnode *
slb_alloc_tree(void)
{
	struct slbtnode *root;

	root = uma_zalloc(slbt_zone, M_NOWAIT | M_ZERO);
	root->ua_level = UAD_ROOT_LEVEL;

	return (root);
}

/* Lock entries mapping kernel text and stacks */

void
slb_insert_kernel(uint64_t slbe, uint64_t slbv)
{
	struct slb *slbcache;
	int i;

	/* We don't want to be preempted while modifying the kernel map */
	critical_enter();

	slbcache = PCPU_GET(aim.slb);

	/* Check for an unused slot, abusing the user slot as a full flag */
	if (slbcache[USER_SLB_SLOT].slbe == 0) {
		for (i = 0; i < n_slbs; i++) {
			if (i == USER_SLB_SLOT)
				continue;
			if (!(slbcache[i].slbe & SLBE_VALID)) 
				goto fillkernslb;
		}

		if (i == n_slbs)
			slbcache[USER_SLB_SLOT].slbe = 1;
	}

	i = mftb() % n_slbs;
	if (i == USER_SLB_SLOT)
			i = (i+1) % n_slbs;

fillkernslb:
	KASSERT(i != USER_SLB_SLOT,
	    ("Filling user SLB slot with a kernel mapping"));
	slbcache[i].slbv = slbv;
	slbcache[i].slbe = slbe | (uint64_t)i;

	/* If it is for this CPU, put it in the SLB right away */
	if (pmap_bootstrapped) {
		/* slbie not required */
		__asm __volatile ("slbmte %0, %1" :: 
		    "r"(slbcache[i].slbv), "r"(slbcache[i].slbe)); 
	}

	critical_exit();
}

void
slb_insert_user(pmap_t pm, struct slb *slb)
{
	int i;

	PMAP_LOCK_ASSERT(pm, MA_OWNED);

	if (pm->pm_slb_len < n_slbs) {
		i = pm->pm_slb_len;
		pm->pm_slb_len++;
	} else {
		i = mftb() % n_slbs;
	}

	/* Note that this replacement is atomic with respect to trap_subr */
	pm->pm_slb[i] = slb;
}

static void *
slb_uma_real_alloc(uma_zone_t zone, vm_size_t bytes, int domain,
    u_int8_t *flags, int wait)
{
	static vm_offset_t realmax = 0;
	void *va;
	vm_page_t m;

	if (realmax == 0)
		realmax = platform_real_maxaddr();

	*flags = UMA_SLAB_PRIV;
	m = vm_page_alloc_contig_domain(NULL, 0, domain,
	    malloc2vm_flags(wait) | VM_ALLOC_NOOBJ | VM_ALLOC_WIRED,
	    1, 0, realmax, PAGE_SIZE, PAGE_SIZE, VM_MEMATTR_DEFAULT);
	if (m == NULL)
		return (NULL);

	va = (void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));

	if (!hw_direct_map)
		pmap_kenter((vm_offset_t)va, VM_PAGE_TO_PHYS(m));

	if ((wait & M_ZERO) && (m->flags & PG_ZERO) == 0)
		bzero(va, PAGE_SIZE);

	return (va);
}

static void
slb_zone_init(void *dummy)
{

	slbt_zone = uma_zcreate("SLB tree node", sizeof(struct slbtnode),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_VM);
	slb_cache_zone = uma_zcreate("SLB cache",
	    (n_slbs + 1)*sizeof(struct slb *), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_VM);

	if (platform_real_maxaddr() != VM_MAX_ADDRESS) {
		uma_zone_set_allocf(slb_cache_zone, slb_uma_real_alloc);
		uma_zone_set_allocf(slbt_zone, slb_uma_real_alloc);
	}
}

struct slb **
slb_alloc_user_cache(void)
{
	return (uma_zalloc(slb_cache_zone, M_ZERO));
}

void
slb_free_user_cache(struct slb **slb)
{
	uma_zfree(slb_cache_zone, slb);
}
