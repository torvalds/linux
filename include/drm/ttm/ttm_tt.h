/**************************************************************************
 *
 * Copyright (c) 2006-2009 Vmware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#ifndef _TTM_TT_H_
#define _TTM_TT_H_

#include <linux/types.h>
#include <drm/ttm/ttm_caching.h>
#include <drm/ttm/ttm_kmap_iter.h>

struct ttm_bo_device;
struct ttm_tt;
struct ttm_resource;
struct ttm_buffer_object;
struct ttm_operation_ctx;

#define TTM_PAGE_FLAG_SWAPPED         (1 << 4)
#define TTM_PAGE_FLAG_ZERO_ALLOC      (1 << 6)
#define TTM_PAGE_FLAG_SG              (1 << 8)
#define TTM_PAGE_FLAG_NO_RETRY	      (1 << 9)

#define TTM_PAGE_FLAG_PRIV_POPULATED  (1 << 31)

/**
 * struct ttm_tt
 *
 * @pages: Array of pages backing the data.
 * @page_flags: see TTM_PAGE_FLAG_*
 * @num_pages: Number of pages in the page array.
 * @sg: for SG objects via dma-buf
 * @dma_address: The DMA (bus) addresses of the pages
 * @swap_storage: Pointer to shmem struct file for swap storage.
 * @pages_list: used by some page allocation backend
 * @caching: The current caching state of the pages, see enum ttm_caching.
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */
struct ttm_tt {
	struct page **pages;
	uint32_t page_flags;
	uint32_t num_pages;
	struct sg_table *sg;
	dma_addr_t *dma_address;
	struct file *swap_storage;
	enum ttm_caching caching;
};

/**
 * struct ttm_kmap_iter_tt - Specialization of a mappig iterator for a tt.
 * @base: Embedded struct ttm_kmap_iter providing the usage interface
 * @tt: Cached struct ttm_tt.
 * @prot: Cached page protection for mapping.
 */
struct ttm_kmap_iter_tt {
	struct ttm_kmap_iter base;
	struct ttm_tt *tt;
	pgprot_t prot;
};

static inline bool ttm_tt_is_populated(struct ttm_tt *tt)
{
	return tt->page_flags & TTM_PAGE_FLAG_PRIV_POPULATED;
}

/**
 * ttm_tt_create
 *
 * @bo: pointer to a struct ttm_buffer_object
 * @zero_alloc: true if allocated pages needs to be zeroed
 *
 * Make sure we have a TTM structure allocated for the given BO.
 * No pages are actually allocated.
 */
int ttm_tt_create(struct ttm_buffer_object *bo, bool zero_alloc);

/**
 * ttm_tt_init
 *
 * @ttm: The struct ttm_tt.
 * @bo: The buffer object we create the ttm for.
 * @page_flags: Page flags as identified by TTM_PAGE_FLAG_XX flags.
 * @caching: the desired caching state of the pages
 *
 * Create a struct ttm_tt to back data with system memory pages.
 * No pages are actually allocated.
 * Returns:
 * NULL: Out of memory.
 */
int ttm_tt_init(struct ttm_tt *ttm, struct ttm_buffer_object *bo,
		uint32_t page_flags, enum ttm_caching caching);
int ttm_sg_tt_init(struct ttm_tt *ttm_dma, struct ttm_buffer_object *bo,
		   uint32_t page_flags, enum ttm_caching caching);

/**
 * ttm_tt_fini
 *
 * @ttm: the ttm_tt structure.
 *
 * Free memory of ttm_tt structure
 */
void ttm_tt_fini(struct ttm_tt *ttm);

/**
 * ttm_tt_destroy:
 *
 * @bdev: the ttm_device this object belongs to
 * @ttm: The struct ttm_tt.
 *
 * Unbind, unpopulate and destroy common struct ttm_tt.
 */
void ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *ttm);

/**
 * ttm_tt_swapin:
 *
 * @ttm: The struct ttm_tt.
 *
 * Swap in a previously swap out ttm_tt.
 */
int ttm_tt_swapin(struct ttm_tt *ttm);
int ttm_tt_swapout(struct ttm_device *bdev, struct ttm_tt *ttm,
		   gfp_t gfp_flags);

/**
 * ttm_tt_populate - allocate pages for a ttm
 *
 * @bdev: the ttm_device this object belongs to
 * @ttm: Pointer to the ttm_tt structure
 * @ctx: operation context for populating the tt object.
 *
 * Calls the driver method to allocate pages for a ttm
 */
int ttm_tt_populate(struct ttm_device *bdev, struct ttm_tt *ttm,
		    struct ttm_operation_ctx *ctx);

/**
 * ttm_tt_unpopulate - free pages from a ttm
 *
 * @bdev: the ttm_device this object belongs to
 * @ttm: Pointer to the ttm_tt structure
 *
 * Calls the driver method to free all pages from a ttm
 */
void ttm_tt_unpopulate(struct ttm_device *bdev, struct ttm_tt *ttm);

/**
 * ttm_tt_mark_for_clear - Mark pages for clearing on populate.
 *
 * @ttm: Pointer to the ttm_tt structure
 *
 * Marks pages for clearing so that the next time the page vector is
 * populated, the pages will be cleared.
 */
static inline void ttm_tt_mark_for_clear(struct ttm_tt *ttm)
{
	ttm->page_flags |= TTM_PAGE_FLAG_ZERO_ALLOC;
}

void ttm_tt_mgr_init(unsigned long num_pages, unsigned long num_dma32_pages);

struct ttm_kmap_iter *ttm_kmap_iter_tt_init(struct ttm_kmap_iter_tt *iter_tt,
					    struct ttm_tt *tt);

#if IS_ENABLED(CONFIG_AGP)
#include <linux/agp_backend.h>

/**
 * ttm_agp_tt_create
 *
 * @bo: Buffer object we allocate the ttm for.
 * @bridge: The agp bridge this device is sitting on.
 * @page_flags: Page flags as identified by TTM_PAGE_FLAG_XX flags.
 *
 *
 * Create a TTM backend that uses the indicated AGP bridge as an aperture
 * for TT memory. This function uses the linux agpgart interface to
 * bind and unbind memory backing a ttm_tt.
 */
struct ttm_tt *ttm_agp_tt_create(struct ttm_buffer_object *bo,
				 struct agp_bridge_data *bridge,
				 uint32_t page_flags);
int ttm_agp_bind(struct ttm_tt *ttm, struct ttm_resource *bo_mem);
void ttm_agp_unbind(struct ttm_tt *ttm);
void ttm_agp_destroy(struct ttm_tt *ttm);
bool ttm_agp_is_bound(struct ttm_tt *ttm);
#endif

#endif
