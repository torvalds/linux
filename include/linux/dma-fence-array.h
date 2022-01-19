/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fence-array: aggregates fence to be waited together
 *
 * Copyright (C) 2016 Collabora Ltd
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 * Authors:
 *	Gustavo Padovan <gustavo@padovan.org>
 *	Christian König <christian.koenig@amd.com>
 */

#ifndef __LINUX_DMA_FENCE_ARRAY_H
#define __LINUX_DMA_FENCE_ARRAY_H

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

/**
 * struct dma_fence_array_cb - callback helper for fence array
 * @cb: fence callback structure for signaling
 * @array: reference to the parent fence array object
 */
struct dma_fence_array_cb {
	struct dma_fence_cb cb;
	struct dma_fence_array *array;
};

/**
 * struct dma_fence_array - fence to represent an array of fences
 * @base: fence base class
 * @lock: spinlock for fence handling
 * @num_fences: number of fences in the array
 * @num_pending: fences in the array still pending
 * @fences: array of the fences
 * @work: internal irq_work function
 */
struct dma_fence_array {
	struct dma_fence base;

	spinlock_t lock;
	unsigned num_fences;
	atomic_t num_pending;
	struct dma_fence **fences;

	struct irq_work work;
};

/**
 * to_dma_fence_array - cast a fence to a dma_fence_array
 * @fence: fence to cast to a dma_fence_array
 *
 * Returns NULL if the fence is not a dma_fence_array,
 * or the dma_fence_array otherwise.
 */
static inline struct dma_fence_array *
to_dma_fence_array(struct dma_fence *fence)
{
	if (!fence || !dma_fence_is_array(fence))
		return NULL;

	return container_of(fence, struct dma_fence_array, base);
}

struct dma_fence_array *dma_fence_array_create(int num_fences,
					       struct dma_fence **fences,
					       u64 context, unsigned seqno,
					       bool signal_on_any);

bool dma_fence_match_context(struct dma_fence *fence, u64 context);

#endif /* __LINUX_DMA_FENCE_ARRAY_H */
