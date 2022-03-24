/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fence-chain: chain fences together in a timeline
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 * Authors:
 *	Christian König <christian.koenig@amd.com>
 */

#ifndef __LINUX_DMA_FENCE_CHAIN_H
#define __LINUX_DMA_FENCE_CHAIN_H

#include <linux/dma-fence.h>
#include <linux/irq_work.h>
#include <linux/slab.h>

/**
 * struct dma_fence_chain - fence to represent an node of a fence chain
 * @base: fence base class
 * @prev: previous fence of the chain
 * @prev_seqno: original previous seqno before garbage collection
 * @fence: encapsulated fence
 * @lock: spinlock for fence handling
 */
struct dma_fence_chain {
	struct dma_fence base;
	struct dma_fence __rcu *prev;
	u64 prev_seqno;
	struct dma_fence *fence;
	union {
		/**
		 * @cb: callback for signaling
		 *
		 * This is used to add the callback for signaling the
		 * complection of the fence chain. Never used at the same time
		 * as the irq work.
		 */
		struct dma_fence_cb cb;

		/**
		 * @work: irq work item for signaling
		 *
		 * Irq work structure to allow us to add the callback without
		 * running into lock inversion. Never used at the same time as
		 * the callback.
		 */
		struct irq_work work;
	};
	spinlock_t lock;
};


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
	if (!fence || !dma_fence_is_chain(fence))
		return NULL;

	return container_of(fence, struct dma_fence_chain, base);
}

/**
 * dma_fence_chain_contained - return the contained fence
 * @fence: the fence to test
 *
 * If the fence is a dma_fence_chain the function returns the fence contained
 * inside the chain object, otherwise it returns the fence itself.
 */
static inline struct dma_fence *
dma_fence_chain_contained(struct dma_fence *fence)
{
	struct dma_fence_chain *chain = to_dma_fence_chain(fence);

	return chain ? chain->fence : fence;
}

/**
 * dma_fence_chain_alloc
 *
 * Returns a new struct dma_fence_chain object or NULL on failure.
 */
static inline struct dma_fence_chain *dma_fence_chain_alloc(void)
{
	return kmalloc(sizeof(struct dma_fence_chain), GFP_KERNEL);
};

/**
 * dma_fence_chain_free
 * @chain: chain node to free
 *
 * Frees up an allocated but not used struct dma_fence_chain object. This
 * doesn't need an RCU grace period since the fence was never initialized nor
 * published. After dma_fence_chain_init() has been called the fence must be
 * released by calling dma_fence_put(), and not through this function.
 */
static inline void dma_fence_chain_free(struct dma_fence_chain *chain)
{
	kfree(chain);
};

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
