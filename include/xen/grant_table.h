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

#include <xen/features.h>
#include <xen/page.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/kernel.h>

/*
 * Technically there's no reliably invalid grant reference or grant handle,
 * so pick the value that is the most unlikely one to be observed valid.
 */
#define INVALID_GRANT_REF          ((grant_ref_t)-1)
#define INVALID_GRANT_HANDLE       ((grant_handle_t)-1)

/* NR_GRANT_FRAMES must be less than or equal to that configured in Xen */
#define NR_GRANT_FRAMES 4

struct gnttab_free_callback {
	struct gnttab_free_callback *next;
	void (*fn)(void *);
	void *arg;
	u16 count;
};

struct gntab_unmap_queue_data;

typedef void (*gnttab_unmap_refs_done)(int result, struct gntab_unmap_queue_data *data);

struct gntab_unmap_queue_data
{
	struct delayed_work	gnttab_work;
	void *data;
	gnttab_unmap_refs_done	done;
	struct gnttab_unmap_grant_ref *unmap_ops;
	struct gnttab_unmap_grant_ref *kunmap_ops;
	struct page **pages;
	unsigned int count;
	unsigned int age;
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
int gnttab_end_foreign_access_ref(grant_ref_t ref);

/*
 * Eventually end access through the given grant reference, and once that
 * access has been ended, free the given page too.  Access will be ended
 * immediately iff the grant entry is not in use, otherwise it will happen
 * some time later.  page may be NULL, in which case no freeing will occur.
 * Note that the granted page might still be accessed (read or write) by the
 * other side after gnttab_end_foreign_access() returns, so even if page was
 * specified as NULL it is not allowed to just reuse the page for other
 * purposes immediately. gnttab_end_foreign_access() will take an additional
 * reference to the granted page in this case, which is dropped only after
 * the grant is no longer in use.
 * This requires that multi page allocations for areas subject to
 * gnttab_end_foreign_access() are done via alloc_pages_exact() (and freeing
 * via free_pages_exact()) in order to avoid high order pages.
 */
void gnttab_end_foreign_access(grant_ref_t ref, struct page *page);

/*
 * End access through the given grant reference, iff the grant entry is
 * no longer in use.  In case of success ending foreign access, the
 * grant reference is deallocated.
 * Return 1 if the grant entry was freed, 0 if it is still in use.
 */
int gnttab_try_end_foreign_access(grant_ref_t ref);

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

/* Give access to the first 4K of the page */
static inline void gnttab_page_grant_foreign_access_ref_one(
	grant_ref_t ref, domid_t domid,
	struct page *page, int readonly)
{
	gnttab_grant_foreign_access_ref(ref, domid, xen_page_to_gfn(page),
					readonly);
}

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
	map->status = 1; /* arbitrary positive value */
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

int arch_gnttab_init(unsigned long nr_shared, unsigned long nr_status);
int arch_gnttab_map_shared(xen_pfn_t *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   void **__shared);
int arch_gnttab_map_status(uint64_t *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   grant_status_t **__shared);
void arch_gnttab_unmap(void *shared, unsigned long nr_gframes);

struct grant_frames {
	xen_pfn_t *pfn;
	unsigned int count;
	void *vaddr;
};
extern struct grant_frames xen_auto_xlat_grant_frames;
unsigned int gnttab_max_grant_frames(void);
int gnttab_setup_auto_xlat_frames(phys_addr_t addr);
void gnttab_free_auto_xlat_frames(void);

#define gnttab_map_vaddr(map) ((void *)(map.host_virt_addr))

int gnttab_alloc_pages(int nr_pages, struct page **pages);
void gnttab_free_pages(int nr_pages, struct page **pages);

struct gnttab_page_cache {
	spinlock_t		lock;
#ifdef CONFIG_XEN_UNPOPULATED_ALLOC
	struct page		*pages;
#else
	struct list_head	pages;
#endif
	unsigned int		num_pages;
};

void gnttab_page_cache_init(struct gnttab_page_cache *cache);
int gnttab_page_cache_get(struct gnttab_page_cache *cache, struct page **page);
void gnttab_page_cache_put(struct gnttab_page_cache *cache, struct page **page,
			   unsigned int num);
void gnttab_page_cache_shrink(struct gnttab_page_cache *cache,
			      unsigned int num);

#ifdef CONFIG_XEN_GRANT_DMA_ALLOC
struct gnttab_dma_alloc_args {
	/* Device for which DMA memory will be/was allocated. */
	struct device *dev;
	/* If set then DMA buffer is coherent and write-combine otherwise. */
	bool coherent;

	int nr_pages;
	struct page **pages;
	xen_pfn_t *frames;
	void *vaddr;
	dma_addr_t dev_bus_addr;
};

int gnttab_dma_alloc_pages(struct gnttab_dma_alloc_args *args);
int gnttab_dma_free_pages(struct gnttab_dma_alloc_args *args);
#endif

int gnttab_pages_set_private(int nr_pages, struct page **pages);
void gnttab_pages_clear_private(int nr_pages, struct page **pages);

int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops,
		    struct gnttab_map_grant_ref *kmap_ops,
		    struct page **pages, unsigned int count);
int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops,
		      struct gnttab_unmap_grant_ref *kunmap_ops,
		      struct page **pages, unsigned int count);
void gnttab_unmap_refs_async(struct gntab_unmap_queue_data* item);
int gnttab_unmap_refs_sync(struct gntab_unmap_queue_data *item);


/* Perform a batch of grant map/copy operations. Retry every batch slot
 * for which the hypervisor returns GNTST_eagain. This is typically due
 * to paged out target frames.
 *
 * Will retry for 1, 2, ... 255 ms, i.e. 256 times during 32 seconds.
 *
 * Return value in each iand every status field of the batch guaranteed
 * to not be GNTST_eagain.
 */
void gnttab_batch_map(struct gnttab_map_grant_ref *batch, unsigned count);
void gnttab_batch_copy(struct gnttab_copy *batch, unsigned count);


struct xen_page_foreign {
	domid_t domid;
	grant_ref_t gref;
};

static inline struct xen_page_foreign *xen_page_foreign(struct page *page)
{
	if (!PageForeign(page))
		return NULL;
#if BITS_PER_LONG < 64
	return (struct xen_page_foreign *)page->private;
#else
	BUILD_BUG_ON(sizeof(struct xen_page_foreign) > BITS_PER_LONG);
	return (struct xen_page_foreign *)&page->private;
#endif
}

/* Split Linux page in chunk of the size of the grant and call fn
 *
 * Parameters of fn:
 *	gfn: guest frame number
 *	offset: offset in the grant
 *	len: length of the data in the grant.
 *	data: internal information
 */
typedef void (*xen_grant_fn_t)(unsigned long gfn, unsigned int offset,
			       unsigned int len, void *data);

void gnttab_foreach_grant_in_range(struct page *page,
				   unsigned int offset,
				   unsigned int len,
				   xen_grant_fn_t fn,
				   void *data);

/* Helper to get to call fn only on the first "grant chunk" */
static inline void gnttab_for_one_grant(struct page *page, unsigned int offset,
					unsigned len, xen_grant_fn_t fn,
					void *data)
{
	/* The first request is limited to the size of one grant */
	len = min_t(unsigned int, XEN_PAGE_SIZE - (offset & ~XEN_PAGE_MASK),
		    len);

	gnttab_foreach_grant_in_range(page, offset, len, fn, data);
}

/* Get @nr_grefs grants from an array of page and call fn for each grant */
void gnttab_foreach_grant(struct page **pages,
			  unsigned int nr_grefs,
			  xen_grant_fn_t fn,
			  void *data);

/* Get the number of grant in a specified region
 *
 * start: Offset from the beginning of the first page
 * len: total length of data (can cross multiple page)
 */
static inline unsigned int gnttab_count_grant(unsigned int start,
					      unsigned int len)
{
	return XEN_PFN_UP(xen_offset_in_page(start) + len);
}

#endif /* __ASM_GNTTAB_H__ */
