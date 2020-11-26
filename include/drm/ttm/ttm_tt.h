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

struct ttm_tt;
struct ttm_resource;
struct ttm_buffer_object;
struct ttm_operation_ctx;

#define TTM_PAGE_FLAG_WRITE           (1 << 3)
#define TTM_PAGE_FLAG_SWAPPED         (1 << 4)
#define TTM_PAGE_FLAG_PERSISTENT_SWAP (1 << 5)
#define TTM_PAGE_FLAG_ZERO_ALLOC      (1 << 6)
#define TTM_PAGE_FLAG_DMA32           (1 << 7)
#define TTM_PAGE_FLAG_SG              (1 << 8)
#define TTM_PAGE_FLAG_NO_RETRY	      (1 << 9)

#define TTM_PAGE_FLAG_PRIV_POPULATED  (1 << 31)

enum ttm_caching_state {
	tt_uncached,
	tt_wc,
	tt_cached
};

/**
 * struct ttm_tt
 *
 * @pages: Array of pages backing the data.
 * @num_pages: Number of pages in the page array.
 * @bdev: Pointer to the current struct ttm_bo_device.
 * @be: Pointer to the ttm backend.
 * @swap_storage: Pointer to shmem struct file for swap storage.
 * @caching_state: The current caching state of the pages.
 * @state: The current binding state of the pages.
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */
struct ttm_tt {
	struct page **pages;
	uint32_t page_flags;
	unsigned long num_pages;
	struct sg_table *sg; /* for SG objects via dma-buf */
	struct file *swap_storage;
	enum ttm_caching_state caching_state;
};

static inline bool ttm_tt_is_populated(struct ttm_tt *tt)
{
	return tt->page_flags & TTM_PAGE_FLAG_PRIV_POPULATED;
}

static inline void ttm_tt_set_unpopulated(struct ttm_tt *tt)
{
	tt->page_flags &= ~TTM_PAGE_FLAG_PRIV_POPULATED;
}

static inline void ttm_tt_set_populated(struct ttm_tt *tt)
{
	tt->page_flags |= TTM_PAGE_FLAG_PRIV_POPULATED;
}

/**
 * struct ttm_dma_tt
 *
 * @ttm: Base ttm_tt struct.
 * @dma_address: The DMA (bus) addresses of the pages
 * @pages_list: used by some page allocation backend
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */
struct ttm_dma_tt {
	struct ttm_tt ttm;
	dma_addr_t *dma_address;
	struct list_head pages_list;
};

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
 *
 * Create a struct ttm_tt to back data with system memory pages.
 * No pages are actually allocated.
 * Returns:
 * NULL: Out of memory.
 */
int ttm_tt_init(struct ttm_tt *ttm, struct ttm_buffer_object *bo,
		uint32_t page_flags);
int ttm_dma_tt_init(struct ttm_dma_tt *ttm_dma, struct ttm_buffer_object *bo,
		    uint32_t page_flags);
int ttm_sg_tt_init(struct ttm_dma_tt *ttm_dma, struct ttm_buffer_object *bo,
		   uint32_t page_flags);

/**
 * ttm_tt_fini
 *
 * @ttm: the ttm_tt structure.
 *
 * Free memory of ttm_tt structure
 */
void ttm_tt_fini(struct ttm_tt *ttm);
void ttm_dma_tt_fini(struct ttm_dma_tt *ttm_dma);

/**
 * ttm_ttm_destroy:
 *
 * @ttm: The struct ttm_tt.
 *
 * Unbind, unpopulate and destroy common struct ttm_tt.
 */
void ttm_tt_destroy(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

/**
 * ttm_tt_destroy_common:
 *
 * Called from driver to destroy common path.
 */
void ttm_tt_destroy_common(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

/**
 * ttm_tt_swapin:
 *
 * @ttm: The struct ttm_tt.
 *
 * Swap in a previously swap out ttm_tt.
 */
int ttm_tt_swapin(struct ttm_tt *ttm);

/**
 * ttm_tt_set_placement_caching:
 *
 * @ttm A struct ttm_tt the backing pages of which will change caching policy.
 * @placement: Flag indicating the desired caching policy.
 *
 * This function will change caching policy of any default kernel mappings of
 * the pages backing @ttm. If changing from cached to uncached or
 * write-combined,
 * all CPU caches will first be flushed to make sure the data of the pages
 * hit RAM. This function may be very costly as it involves global TLB
 * and cache flushes and potential page splitting / combining.
 */
int ttm_tt_set_placement_caching(struct ttm_tt *ttm, uint32_t placement);
int ttm_tt_swapout(struct ttm_bo_device *bdev, struct ttm_tt *ttm, struct file *persistent_swap_storage);

/**
 * ttm_tt_populate - allocate pages for a ttm
 *
 * @ttm: Pointer to the ttm_tt structure
 *
 * Calls the driver method to allocate pages for a ttm
 */
int ttm_tt_populate(struct ttm_bo_device *bdev, struct ttm_tt *ttm, struct ttm_operation_ctx *ctx);

/**
 * ttm_tt_unpopulate - free pages from a ttm
 *
 * @ttm: Pointer to the ttm_tt structure
 *
 * Calls the driver method to free all pages from a ttm
 */
void ttm_tt_unpopulate(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

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
