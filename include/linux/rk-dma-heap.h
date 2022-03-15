/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#ifndef _RK_DMA_HEAPS_H_
#define _RK_DMA_HEAPS_H_
#include <linux/dma-buf.h>

struct rk_dma_heap;

#if defined(CONFIG_DMABUF_HEAPS_ROCKCHIP)
int rk_dma_heap_cma_setup(void);

/**
 * rk_dma_heap_set_dev - set heap dev dma param
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * Zero on success, ERR_PTR(-errno) on error
 */
int rk_dma_heap_set_dev(struct device *heap_dev);

/**
 * rk_dma_heap_find - Returns the registered dma_heap with the specified name
 * @name: Name of the heap to find
 *
 * NOTE: dma_heaps returned from this function MUST be released
 * using rk_dma_heap_put() when the user is done.
 */
struct rk_dma_heap *rk_dma_heap_find(const char *name);

/** rk_dma_heap_buffer_free - Free dma_buf allocated by rk_dma_heap_buffer_alloc
 * @dma_buf:	dma_buf to free
 *
 * This is really only a simple wrapper to dma_buf_put()
 */
void rk_dma_heap_buffer_free(struct dma_buf *dmabuf);

/**
 * rk_dma_heap_buffer_alloc - Allocate dma-buf from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 *
 * This is for internal dma-buf allocations only.
 */
struct dma_buf *rk_dma_heap_buffer_alloc(struct rk_dma_heap *heap, size_t len,
					 unsigned int fd_flags,
					 unsigned int heap_flags,
					 const char *name);

/**
 * rk_dma_heap_bufferfd_alloc - Allocate dma-buf fd from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 */
int rk_dma_heap_bufferfd_alloc(struct rk_dma_heap *heap, size_t len,
			       unsigned int fd_flags,
			       unsigned int heap_flags,
			       const char *name);

/**
 * rk_dma_heap_alloc_contig_pages - Allocate contiguous pages from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @name:	the name who allocate
 */
struct page *rk_dma_heap_alloc_contig_pages(struct rk_dma_heap *heap,
					    size_t len, const char *name);

/**
 * rk_dma_heap_free_contig_pages - Free contiguous pages to a dma_heap
 * @heap:	dma_heap to free to
 * @pages:	pages to free to
 * @len:	size to free
 * @name:	the name who allocate
 */
void rk_dma_heap_free_contig_pages(struct rk_dma_heap *heap, struct page *pages,
				   size_t len, const char *name);

#else
static inline int rk_dma_heap_cma_setup(void)
{
	return -ENODEV;
}

static inline int rk_dma_heap_set_dev(struct device *heap_dev)
{
	return -ENODEV;
}

static inline struct rk_dma_heap *rk_dma_heap_find(const char *name)
{
	return NULL;
}

static inline void rk_dma_heap_buffer_free(struct dma_buf *dmabuf)
{
}

static inline struct dma_buf *rk_dma_heap_buffer_alloc(struct rk_dma_heap *heap, size_t len,
						       unsigned int fd_flags,
						       unsigned int heap_flags,
						       const char *name)
{
	return NULL;
}

static inline int rk_dma_heap_bufferfd_alloc(struct rk_dma_heap *heap, size_t len,
					     unsigned int fd_flags,
					     unsigned int heap_flags,
					     const char *name)
{
	return -ENODEV;
}

static inline struct page *rk_dma_heap_alloc_contig_pages(struct rk_dma_heap *heap,
							  size_t len, const char *name)
{
	return NULL;
}

static inline void rk_dma_heap_free_contig_pages(struct rk_dma_heap *heap, struct page *pages,
						 size_t len, const char *name)
{
}
#endif
#endif /* _DMA_HEAPS_H */
