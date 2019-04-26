/*
 * fence-chain: chain fences together in a timeline
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __LINUX_DMA_FENCE_CHAIN_H
#define __LINUX_DMA_FENCE_CHAIN_H

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

/**
 * struct dma_fence_chain - fence to represent an node of a fence chain
 * @base: fence base class
 * @lock: spinlock for fence handling
 * @prev: previous fence of the chain
 * @prev_seqno: original previous seqno before garbage collection
 * @fence: encapsulated fence
 * @cb: callback structure for signaling
 * @work: irq work item for signaling
 */
struct dma_fence_chain {
	struct dma_fence base;
	spinlock_t lock;
	struct dma_fence __rcu *prev;
	u64 prev_seqno;
	struct dma_fence *fence;
	struct dma_fence_cb cb;
	struct irq_work work;
};

extern const struct dma_fence_ops dma_fence_chain_ops;

/**
 * to_dma_fence_chain - cast a fence to a dma_fence_chain
 * @fence: fence to cast to a dma_fence_array
 *
 * Returns NULL if the fence is not a dma_fence_chain,
 * or the dma_fence_chain otherwise.
 */
static inline struct dma_fence_chain *
to_dma_fence_chain(struct dma_fence *fence)
{
	if (!fence || fence->ops != &dma_fence_chain_ops)
		return NULL;

	return container_of(fence, struct dma_fence_chain, base);
}

/**
 * dma_fence_chain_for_each - iterate over all fences in chain
 * @iter: current fence
 * @head: starting point
 *
 * Iterate over all fences in the chain. We keep a reference to the current
 * fence while inside the loop which must be dropped when breaking out.
 */
#define dma_fence_chain_for_each(iter, head)	\
	for (iter = dma_fence_get(head); iter; \
	     iter = dma_fence_chain_walk(iter))

struct dma_fence *dma_fence_chain_walk(struct dma_fence *fence);
int dma_fence_chain_find_seqno(struct dma_fence **pfence, uint64_t seqno);
void dma_fence_chain_init(struct dma_fence_chain *chain,
			  struct dma_fence *prev,
			  struct dma_fence *fence,
			  uint64_t seqno);

#endif /* __LINUX_DMA_FENCE_CHAIN_H */
