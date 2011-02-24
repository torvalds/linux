/******************************************************************************
 * grant_table.h
 *
 * Two sets of functionality:
 * 1. Granting foreign access to our memory reservation.
 * 2. Accessing others' memory reservations via grant references.
 * (i.e., mechanisms for both sender and recipient of grant references)
 *
 * Copyright (c) 2004-2005, K A Fraser
 * Copyright (c) 2005, Christopher Clark
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __ASM_GNTTAB_H__
#define __ASM_GNTTAB_H__

#include <asm/page.h>

#include <xen/interface/xen.h>
#include <xen/interface/grant_table.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/grant_table.h>

#include <xen/features.h>

/* NR_GRANT_FRAMES must be less than or equal to that configured in Xen */
#define NR_GRANT_FRAMES 4

struct gnttab_free_callback {
	struct gnttab_free_callback *next;
	void (*fn)(void *);
	void *arg;
	u16 count;
};

int gnttab_init(void);
int gnttab_suspend(void);
int gnttab_resume(void);

int gnttab_grant_foreign_access(domid_t domid, unsigned long frame,
				int readonly);

/*
 * End access through the given grant reference, iff the grant entry is no
 * longer in use.  Return 1 if the grant entry was freed, 0 if it is still in
 * use.
 */
int gnttab_end_foreign_access_ref(grant_ref_t ref, int readonly);

/*
 * Eventually end access through the given grant reference, and once that
 * access has been ended, free the given page too.  Access will be ended
 * immediately iff the grant entry is not in use, otherwise it will happen
 * some time later.  page may be 0, in which case no freeing will occur.
 */
void gnttab_end_foreign_access(grant_ref_t ref, int readonly,
			       unsigned long page);

int gnttab_grant_foreign_transfer(domid_t domid, unsigned long pfn);

unsigned long gnttab_end_foreign_transfer_ref(grant_ref_t ref);
unsigned long gnttab_end_foreign_transfer(grant_ref_t ref);

int gnttab_query_foreign_access(grant_ref_t ref);

/*
 * operations on reserved batches of grant references
 */
int gnttab_alloc_grant_references(u16 count, grant_ref_t *pprivate_head);

void gnttab_free_grant_reference(grant_ref_t ref);

void gnttab_free_grant_references(grant_ref_t head);

int gnttab_empty_grant_references(const grant_ref_t *pprivate_head);

int gnttab_claim_grant_reference(grant_ref_t *pprivate_head);

void gnttab_release_grant_reference(grant_ref_t *private_head,
				    grant_ref_t release);

void gnttab_request_free_callback(struct gnttab_free_callback *callback,
				  void (*fn)(void *), void *arg, u16 count);
void gnttab_cancel_free_callback(struct gnttab_free_callback *callback);

void gnttab_grant_foreign_access_ref(grant_ref_t ref, domid_t domid,
				     unsigned long frame, int readonly);

void gnttab_grant_foreign_transfer_ref(grant_ref_t, domid_t domid,
				       unsigned long pfn);

static inline void
gnttab_set_map_op(struct gnttab_map_grant_ref *map, phys_addr_t addr,
		  uint32_t flags, grant_ref_t ref, domid_t domid)
{
	if (flags & GNTMAP_contains_pte)
		map->host_addr = addr;
	else if (xen_feature(XENFEAT_auto_translated_physmap))
		map->host_addr = __pa(addr);
	else
		map->host_addr = addr;

	map->flags = flags;
	map->ref = ref;
	map->dom = domid;
}

static inline void
gnttab_set_unmap_op(struct gnttab_unmap_grant_ref *unmap, phys_addr_t addr,
		    uint32_t flags, grant_handle_t handle)
{
	if (flags & GNTMAP_contains_pte)
		unmap->host_addr = addr;
	else if (xen_feature(XENFEAT_auto_translated_physmap))
		unmap->host_addr = __pa(addr);
	else
		unmap->host_addr = addr;

	unmap->handle = handle;
	unmap->dev_bus_addr = 0;
}

int arch_gnttab_map_shared(unsigned long *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   struct grant_entry **__shared);
void arch_gnttab_unmap_shared(struct grant_entry *shared,
			      unsigned long nr_gframes);

extern unsigned long xen_hvm_resume_frames;
unsigned int gnttab_max_grant_frames(void);

#define gnttab_map_vaddr(map) ((void *)(map.host_virt_addr))

int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops,
		    struct page **pages, unsigned int count);
int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops,
		      struct page **pages, unsigned int count);

#endif /* __ASM_GNTTAB_H__ */
