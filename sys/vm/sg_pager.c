/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Hudson River Trading LLC
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
 * This pager manages OBJT_SG objects.  These objects are backed by
 * a scatter/gather list of physical address ranges.
 */

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sglist.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/uma.h>

static vm_object_t sg_pager_alloc(void *, vm_ooffset_t, vm_prot_t,
    vm_ooffset_t, struct ucred *);
static void sg_pager_dealloc(vm_object_t);
static int sg_pager_getpages(vm_object_t, vm_page_t *, int, int *, int *);
static void sg_pager_putpages(vm_object_t, vm_page_t *, int, 
		boolean_t, int *);
static boolean_t sg_pager_haspage(vm_object_t, vm_pindex_t, int *,
		int *);

struct pagerops sgpagerops = {
	.pgo_alloc =	sg_pager_alloc,
	.pgo_dealloc =	sg_pager_dealloc,
	.pgo_getpages =	sg_pager_getpages,
	.pgo_putpages =	sg_pager_putpages,
	.pgo_haspage =	sg_pager_haspage,
};

static vm_object_t
sg_pager_alloc(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred)
{
	struct sglist *sg;
	vm_object_t object;
	vm_pindex_t npages, pindex;
	int i;

	/*
	 * Offset should be page aligned.
	 */
	if (foff & PAGE_MASK)
		return (NULL);

	/*
	 * The scatter/gather list must only include page-aligned
	 * ranges.
	 */
	npages = 0;
	sg = handle;
	for (i = 0; i < sg->sg_nseg; i++) {
		if ((sg->sg_segs[i].ss_paddr % PAGE_SIZE) != 0 ||
		    (sg->sg_segs[i].ss_len % PAGE_SIZE) != 0)
			return (NULL);
		npages += sg->sg_segs[i].ss_len / PAGE_SIZE;
	}

	/*
	 * The scatter/gather list has a fixed size.  Refuse requests
	 * to map beyond that.
	 */
	size = round_page(size);
	pindex = OFF_TO_IDX(foff) + OFF_TO_IDX(size);
	if (pindex > npages || pindex < OFF_TO_IDX(foff) ||
	    pindex < OFF_TO_IDX(size))
		return (NULL);

	/*
	 * Allocate a new object and associate it with the
	 * scatter/gather list.  It is ok for our purposes to have
	 * multiple VM objects associated with the same scatter/gather
	 * list because scatter/gather lists are static.  This is also
	 * simpler than ensuring a unique object per scatter/gather
	 * list.
	 */
	object = vm_object_allocate(OBJT_SG, npages);
	object->handle = sglist_hold(sg);
	TAILQ_INIT(&object->un_pager.sgp.sgp_pglist);
	return (object);
}

static void
sg_pager_dealloc(vm_object_t object)
{
	struct sglist *sg;
	vm_page_t m;

	/*
	 * Free up our fake pages.
	 */
	while ((m = TAILQ_FIRST(&object->un_pager.sgp.sgp_pglist)) != 0) {
		TAILQ_REMOVE(&object->un_pager.sgp.sgp_pglist, m, plinks.q);
		vm_page_putfake(m);
	}
	
	sg = object->handle;
	sglist_free(sg);
	object->handle = NULL;
	object->type = OBJT_DEAD;
}

static int
sg_pager_getpages(vm_object_t object, vm_page_t *m, int count, int *rbehind,
    int *rahead)
{
	struct sglist *sg;
	vm_page_t m_paddr, page;
	vm_pindex_t offset;
	vm_paddr_t paddr;
	vm_memattr_t memattr;
	size_t space;
	int i;

	/* Since our haspage reports zero after/before, the count is 1. */
	KASSERT(count == 1, ("%s: count %d", __func__, count));
	VM_OBJECT_ASSERT_WLOCKED(object);
	sg = object->handle;
	memattr = object->memattr;
	VM_OBJECT_WUNLOCK(object);
	offset = m[0]->pindex;

	/*
	 * Lookup the physical address of the requested page.  An initial
	 * value of '1' instead of '0' is used so we can assert that the
	 * page is found since '0' can be a valid page-aligned physical
	 * address.
	 */
	space = 0;
	paddr = 1;
	for (i = 0; i < sg->sg_nseg; i++) {
		if (space + sg->sg_segs[i].ss_len <= (offset * PAGE_SIZE)) {
			space += sg->sg_segs[i].ss_len;
			continue;
		}
		paddr = sg->sg_segs[i].ss_paddr + offset * PAGE_SIZE - space;
		break;
	}
	KASSERT(paddr != 1, ("invalid SG page index"));

	/* If "paddr" is a real page, perform a sanity check on "memattr". */
	if ((m_paddr = vm_phys_paddr_to_vm_page(paddr)) != NULL &&
	    pmap_page_get_memattr(m_paddr) != memattr) {
		memattr = pmap_page_get_memattr(m_paddr);
		printf(
	    "WARNING: A device driver has set \"memattr\" inconsistently.\n");
	}

	/* Return a fake page for the requested page. */
	KASSERT(!(m[0]->flags & PG_FICTITIOUS),
	    ("backing page for SG is fake"));

	/* Construct a new fake page. */
	page = vm_page_getfake(paddr, memattr);
	VM_OBJECT_WLOCK(object);
	TAILQ_INSERT_TAIL(&object->un_pager.sgp.sgp_pglist, page, plinks.q);
	vm_page_replace_checked(page, object, offset, m[0]);
	vm_page_lock(m[0]);
	vm_page_free(m[0]);
	vm_page_unlock(m[0]);
	m[0] = page;
	page->valid = VM_PAGE_BITS_ALL;

	if (rbehind)
		*rbehind = 0;
	if (rahead)
		*rahead = 0;

	return (VM_PAGER_OK);
}

static void
sg_pager_putpages(vm_object_t object, vm_page_t *m, int count,
    boolean_t sync, int *rtvals)
{

	panic("sg_pager_putpage called");
}

static boolean_t
sg_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *before,
    int *after)
{

	if (before != NULL)
		*before = 0;
	if (after != NULL)
		*after = 0;
	return (TRUE);
}
