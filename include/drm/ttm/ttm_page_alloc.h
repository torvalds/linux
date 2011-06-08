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

#include "ttm_bo_driver.h"
#include "ttm_memory.h"

/**
 * Get count number of pages from pool to pages list.
 *
 * @pages: head of empty linked list where pages are filled.
 * @flags: ttm flags for page allocation.
 * @cstate: ttm caching state for the page.
 * @count: number of pages to allocate.
 * @dma_address: The DMA (bus) address of pages (if TTM_PAGE_FLAG_DMA32 set).
 */
int ttm_get_pages(struct list_head *pages,
		  int flags,
		  enum ttm_caching_state cstate,
		  unsigned count,
		  dma_addr_t *dma_address);
/**
 * Put linked list of pages to pool.
 *
 * @pages: list of pages to free.
 * @page_count: number of pages in the list. Zero can be passed for unknown
 * count.
 * @flags: ttm flags for page allocation.
 * @cstate: ttm caching state.
 * @dma_address: The DMA (bus) address of pages (if TTM_PAGE_FLAG_DMA32 set).
 */
void ttm_put_pages(struct list_head *pages,
		   unsigned page_count,
		   int flags,
		   enum ttm_caching_state cstate,
		   dma_addr_t *dma_address);
/**
 * Initialize pool allocator.
 */
int ttm_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages);
/**
 * Free pool allocator.
 */
void ttm_page_alloc_fini(void);

/**
 * Output the state of pools to debugfs file
 */
extern int ttm_page_alloc_debugfs(struct seq_file *m, void *data);
#endif
