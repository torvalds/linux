/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#ifndef _DMA_HEAPS_H
#define _DMA_HEAPS_H

#include <linux/cdev.h>
#include <linux/types.h>

struct dma_heap;

/**
 * struct dma_heap_ops - ops to operate on a given heap
 * @allocate:		allocate dmabuf and return struct dma_buf ptr
 * @get_pool_size:	if heap maintains memory pools, get pool size in bytes
 *
 * allocate returns dmabuf on success, ERR_PTR(-errno) on error.
 */
struct dma_heap_ops {
	struct dma_buf *(*allocate)(struct dma_heap *heap,
				    unsigned long len,
				    unsigned long fd_flags,
				    unsigned long heap_flags);
	long (*get_pool_size)(struct dma_heap *heap);
};

/**
 * struct dma_heap_export_info - information needed to export a new dmabuf heap
 * @name:	used for debugging/device-node name
 * @ops:	ops struct for this heap
 * @priv:	heap exporter private data
 *
 * Information needed to export a new dmabuf heap.
 */
struct dma_heap_export_info {
	const char *name;
	const struct dma_heap_ops *ops;
	void *priv;
};

/**
 * dma_heap_get_drvdata() - get per-heap driver data
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-heap data for the heap.
 */
void *dma_heap_get_drvdata(struct dma_heap *heap);

/**
 * dma_heap_get_dev() - get device struct for the heap
 * @heap: DMA-Heap to retrieve device struct from
 *
 * Returns:
 * The device struct for the heap.
 */
struct device *dma_heap_get_dev(struct dma_heap *heap);

/**
 * dma_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *dma_heap_get_name(struct dma_heap *heap);

/**
 * dma_heap_add - adds a heap to dmabuf heaps
 * @exp_info:		information needed to register this heap
 */
struct dma_heap *dma_heap_add(const struct dma_heap_export_info *exp_info);

/**
 * dma_heap_put - drops a reference to a dmabuf heaps, potentially freeing it
 * @heap:		heap pointer
 */
void dma_heap_put(struct dma_heap *heap);

/**
 * dma_heap_find - Returns the registered dma_heap with the specified name
 * @name: Name of the heap to find
 *
 * NOTE: dma_heaps returned from this function MUST be released
 * using dma_heap_put() when the user is done.
 */
struct dma_heap *dma_heap_find(const char *name);

/**
 * dma_heap_buffer_alloc - Allocate dma-buf from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 *
 * This is for internal dma-buf allocations only.
 */
struct dma_buf *dma_heap_buffer_alloc(struct dma_heap *heap, size_t len,
				      unsigned int fd_flags,
				      unsigned int heap_flags);

/** dma_heap_buffer_free - Free dma_buf allocated by dma_heap_buffer_alloc
 * @dma_buf:	dma_buf to free
 *
 * This is really only a simple wrapper to dma_buf_put()
 */
void dma_heap_buffer_free(struct dma_buf *);

/**
 * dma_heap_bufferfd_alloc - Allocate dma-buf fd from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 */
int dma_heap_bufferfd_alloc(struct dma_heap *heap, size_t len,
			    unsigned int fd_flags,
			    unsigned int heap_flags);
#endif /* _DMA_HEAPS_H */
