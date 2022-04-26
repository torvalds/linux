/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fence-chain: chain fences together in a timeline
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 */

#ifndef __LINUX_DMA_FENCE_UNWRAP_H
#define __LINUX_DMA_FENCE_UNWRAP_H

#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-array.h>

/**
 * struct dma_fence_unwrap - cursor into the container structure
 *
 * Should be used with dma_fence_unwrap_for_each() iterator macro.
 */
struct dma_fence_unwrap {
	/**
	 * @chain: potential dma_fence_chain, but can be other fence as well
	 */
	struct dma_fence *chain;
	/**
	 * @array: potential dma_fence_array, but can be other fence as well
	 */
	struct dma_fence *array;
	/**
	 * @index: last returned index if @array is really a dma_fence_array
	 */
	unsigned int index;
};

/* Internal helper to start new array iteration, don't use directly */
static inline struct dma_fence *
__dma_fence_unwrap_array(struct dma_fence_unwrap * cursor)
{
	cursor->array = dma_fence_chain_contained(cursor->chain);
	cursor->index = 0;
	return dma_fence_array_first(cursor->array);
}

/**
 * dma_fence_unwrap_first - return the first fence from fence containers
 * @head: the entrypoint into the containers
 * @cursor: current position inside the containers
 *
 * Unwraps potential dma_fence_chain/dma_fence_array containers and return the
 * first fence.
 */
static inline struct dma_fence *
dma_fence_unwrap_first(struct dma_fence *head, struct dma_fence_unwrap *cursor)
{
	cursor->chain = dma_fence_get(head);
	return __dma_fence_unwrap_array(cursor);
}

/**
 * dma_fence_unwrap_next - return the next fence from a fence containers
 * @cursor: current position inside the containers
 *
 * Continue unwrapping the dma_fence_chain/dma_fence_array containers and return
 * the next fence from them.
 */
static inline struct dma_fence *
dma_fence_unwrap_next(struct dma_fence_unwrap *cursor)
{
	struct dma_fence *tmp;

	++cursor->index;
	tmp = dma_fence_array_next(cursor->array, cursor->index);
	if (tmp)
		return tmp;

	cursor->chain = dma_fence_chain_walk(cursor->chain);
	return __dma_fence_unwrap_array(cursor);
}

/**
 * dma_fence_unwrap_for_each - iterate over all fences in containers
 * @fence: current fence
 * @cursor: current position inside the containers
 * @head: starting point for the iterator
 *
 * Unwrap dma_fence_chain and dma_fence_array containers and deep dive into all
 * potential fences in them. If @head is just a normal fence only that one is
 * returned.
 */
#define dma_fence_unwrap_for_each(fence, cursor, head)			\
	for (fence = dma_fence_unwrap_first(head, cursor); fence;	\
	     fence = dma_fence_unwrap_next(cursor))

#endif
