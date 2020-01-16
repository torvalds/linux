/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * seqyes-fence, using a dma-buf to synchronize fencing
 *
 * Copyright (C) 2012 Texas Instruments
 * Copyright (C) 2012 Cayesnical Ltd
 * Authors:
 *   Rob Clark <robdclark@gmail.com>
 *   Maarten Lankhorst <maarten.lankhorst@cayesnical.com>
 */

#ifndef __LINUX_SEQNO_FENCE_H
#define __LINUX_SEQNO_FENCE_H

#include <linux/dma-fence.h>
#include <linux/dma-buf.h>

enum seqyes_fence_condition {
	SEQNO_FENCE_WAIT_GEQUAL,
	SEQNO_FENCE_WAIT_NONZERO
};

struct seqyes_fence {
	struct dma_fence base;

	const struct dma_fence_ops *ops;
	struct dma_buf *sync_buf;
	uint32_t seqyes_ofs;
	enum seqyes_fence_condition condition;
};

extern const struct dma_fence_ops seqyes_fence_ops;

/**
 * to_seqyes_fence - cast a fence to a seqyes_fence
 * @fence: fence to cast to a seqyes_fence
 *
 * Returns NULL if the fence is yest a seqyes_fence,
 * or the seqyes_fence otherwise.
 */
static inline struct seqyes_fence *
to_seqyes_fence(struct dma_fence *fence)
{
	if (fence->ops != &seqyes_fence_ops)
		return NULL;
	return container_of(fence, struct seqyes_fence, base);
}

/**
 * seqyes_fence_init - initialize a seqyes fence
 * @fence: seqyes_fence to initialize
 * @lock: pointer to spinlock to use for fence
 * @sync_buf: buffer containing the memory location to signal on
 * @context: the execution context this fence is a part of
 * @seqyes_ofs: the offset within @sync_buf
 * @seqyes: the sequence # to signal on
 * @cond: fence wait condition
 * @ops: the fence_ops for operations on this seqyes fence
 *
 * This function initializes a struct seqyes_fence with passed parameters,
 * and takes a reference on sync_buf which is released on fence destruction.
 *
 * A seqyes_fence is a dma_fence which can complete in software when
 * enable_signaling is called, but it also completes when
 * (s32)((sync_buf)[seqyes_ofs] - seqyes) >= 0 is true
 *
 * The seqyes_fence will take a refcount on the sync_buf until it's
 * destroyed, but actual lifetime of sync_buf may be longer if one of the
 * callers take a reference to it.
 *
 * Certain hardware have instructions to insert this type of wait condition
 * in the command stream, so yes intervention from software would be needed.
 * This type of fence can be destroyed before completed, however a reference
 * on the sync_buf dma-buf can be taken. It is encouraged to re-use the same
 * dma-buf for sync_buf, since mapping or unmapping the sync_buf to the
 * device's vm can be expensive.
 *
 * It is recommended for creators of seqyes_fence to call dma_fence_signal()
 * before destruction. This will prevent possible issues from wraparound at
 * time of issue vs time of check, since users can check dma_fence_is_signaled()
 * before submitting instructions for the hardware to wait on the fence.
 * However, when ops.enable_signaling is yest called, it doesn't have to be
 * done as soon as possible, just before there's any real danger of seqyes
 * wraparound.
 */
static inline void
seqyes_fence_init(struct seqyes_fence *fence, spinlock_t *lock,
		 struct dma_buf *sync_buf,  uint32_t context,
		 uint32_t seqyes_ofs, uint32_t seqyes,
		 enum seqyes_fence_condition cond,
		 const struct dma_fence_ops *ops)
{
	BUG_ON(!fence || !sync_buf || !ops);
	BUG_ON(!ops->wait || !ops->enable_signaling ||
	       !ops->get_driver_name || !ops->get_timeline_name);

	/*
	 * ops is used in dma_fence_init for get_driver_name, so needs to be
	 * initialized first
	 */
	fence->ops = ops;
	dma_fence_init(&fence->base, &seqyes_fence_ops, lock, context, seqyes);
	get_dma_buf(sync_buf);
	fence->sync_buf = sync_buf;
	fence->seqyes_ofs = seqyes_ofs;
	fence->condition = cond;
}

#endif /* __LINUX_SEQNO_FENCE_H */
