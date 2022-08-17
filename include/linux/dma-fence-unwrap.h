/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 */

#ifndef __LINUX_DMA_FENCE_UNWRAP_H
#define __LINUX_DMA_FENCE_UNWRAP_H

struct dma_fence;

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

struct dma_fence *dma_fence_unwrap_first(struct dma_fence *head,
					 struct dma_fence_unwrap *cursor);
struct dma_fence *dma_fence_unwrap_next(struct dma_fence_unwrap *cursor);

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

struct dma_fence *__dma_fence_unwrap_merge(unsigned int num_fences,
					   struct dma_fence **fences,
					   struct dma_fence_unwrap *cursors);

/**
 * dma_fence_unwrap_merge - unwrap and merge fences
 *
 * All fences given as parameters are unwrapped and merged back together as flat
 * dma_fence_array. Useful if multiple containers need to be merged together.
 *
 * Implemented as a macro to allocate the necessary arrays on the stack and
 * account the stack frame size to the caller.
 *
 * Returns NULL on memory allocation failure, a dma_fence object representing
 * all the given fences otherwise.
 */
#define dma_fence_unwrap_merge(...)					\
	({								\
		struct dma_fence *__f[] = { __VA_ARGS__ };		\
		struct dma_fence_unwrap __c[ARRAY_SIZE(__f)];		\
									\
		__dma_fence_unwrap_merge(ARRAY_SIZE(__f), __f, __c);	\
	})

#endif
