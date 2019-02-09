/*
 * Copyright (c) Red Hat Inc.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Jerome Glisse <jglisse@redhat.com>
 */
#ifndef TTM_PAGE_ALLOC
#define TTM_PAGE_ALLOC

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_memory.h>

struct device;

/**
 * Initialize pool allocator.
 */
int ttm_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages);
/**
 * Free pool allocator.
 */
void ttm_page_alloc_fini(void);

/**
 * ttm_pool_populate:
 *
 * @ttm: The struct ttm_tt to contain the backing pages.
 *
 * Add backing pages to all of @ttm
 */
int ttm_pool_populate(struct ttm_tt *ttm, struct ttm_operation_ctx *ctx);

/**
 * ttm_pool_unpopulate:
 *
 * @ttm: The struct ttm_tt which to free backing pages.
 *
 * Free all pages of @ttm
 */
void ttm_pool_unpopulate(struct ttm_tt *ttm);

/**
 * Populates and DMA maps pages to fullfil a ttm_dma_populate() request
 */
int ttm_populate_and_map_pages(struct device *dev, struct ttm_dma_tt *tt,
				struct ttm_operation_ctx *ctx);

/**
 * Unpopulates and DMA unmaps pages as part of a
 * ttm_dma_unpopulate() request */
void ttm_unmap_and_unpopulate_pages(struct device *dev, struct ttm_dma_tt *tt);

/**
 * Output the state of pools to debugfs file
 */
int ttm_page_alloc_debugfs(struct seq_file *m, void *data);

#if defined(CONFIG_SWIOTLB) || defined(CONFIG_INTEL_IOMMU)
/**
 * Initialize pool allocator.
 */
int ttm_dma_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages);

/**
 * Free pool allocator.
 */
void ttm_dma_page_alloc_fini(void);

/**
 * Output the state of pools to debugfs file
 */
int ttm_dma_page_alloc_debugfs(struct seq_file *m, void *data);

int ttm_dma_populate(struct ttm_dma_tt *ttm_dma, struct device *dev,
			struct ttm_operation_ctx *ctx);
void ttm_dma_unpopulate(struct ttm_dma_tt *ttm_dma, struct device *dev);

#else
static inline int ttm_dma_page_alloc_init(struct ttm_mem_global *glob,
					  unsigned max_pages)
{
	return -ENODEV;
}

static inline void ttm_dma_page_alloc_fini(void) { return; }

static inline int ttm_dma_page_alloc_debugfs(struct seq_file *m, void *data)
{
	return 0;
}
static inline int ttm_dma_populate(struct ttm_dma_tt *ttm_dma,
				struct device *dev,
				struct ttm_operation_ctx *ctx)
{
	return -ENOMEM;
}
static inline void ttm_dma_unpopulate(struct ttm_dma_tt *ttm_dma,
				      struct device *dev)
{
}
#endif

#endif
