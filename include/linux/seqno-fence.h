/*
 * seqno-fence, using a dma-buf to synchronize fencing
 *
 * Copyright (C) 2012 Texas Instruments
 * Copyright (C) 2012 Canonical Ltd
 * Authors:
 *   Rob Clark <robdclark@gmail.com>
 *   Maarten Lankhorst <maarten.lankhorst@canonical.com>
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

#ifndef __LINUX_SEQNO_FENCE_H
#define __LINUX_SEQNO_FENCE_H

#include <linux/fence.h>
#include <linux/dma-buf.h>

enum seqno_fence_condition {
	SEQNO_FENCE_WAIT_GEQUAL,
	SEQNO_FENCE_WAIT_NONZERO
};

struct seqno_fence {
	struct fence base;

	const struct fence_ops *ops;
	struct dma_buf *sync_buf;
	uint32_t seqno_ofs;
	enum seqno_fence_condition condition;
};

extern const struct fence_ops seqno_fence_ops;

/**
 * to_seqno_fence - cast a fence to a seqno_fence
 * @fence: fence to cast to a seqno_fence
 *
 * Returns NULL if the fence is not a seqno_fence,
 * or the seqno_fence otherwise.
 */
static inline struct seqno_fence *
to_seqno_fence(struct fence *fence)
{
	if (fence->ops != &seqno_fence_ops)
		return NULL;
	return container_of(fence, struct seqno_fence, base);
}

/**
 * seqno_fence_init - initialize a seqno fence
 * @fence: seqno_fence to initialize
 * @lock: pointer to spinlock to use for fence
 * @sync_buf: buffer containing the memory location to signal on
 * @context: the execution context this fence is a part of
 * @seqno_ofs: the offset within @sync_buf
 * @seqno: the sequence # to signal on
 * @ops: the fence_ops for operations on this seqno fence
 *
 * This function initializes a struct seqno_fence with passed parameters,
 * and takes a reference on sync_buf which is released on fence destruction.
 *
 * A seqno_fence is a dma_fence which can complete in software when
 * enable_signaling is called, but it also completes when
 * (s32)((sync_buf)[seqno_ofs] - seqno) >= 0 is true
 *
 * The seqno_fence will take a refcount on the sync_buf until it's
 * destroyed, but actual lifetime of sync_buf may be longer if one of the
 * callers take a reference to it.
 *
 * Certain hardware have instructions to insert this type of wait condition
 * in the command stream, so no intervention from software would be needed.
 * This type of fence can be destroyed before completed, however a reference
 * on the sync_buf dma-buf can be taken. It is encouraged to re-use the same
 * dma-buf for sync_buf, since mapping or unmapping the sync_buf to the
 * device's vm can be expensive.
 *
 * It is recommended for creators of seqno_fence to call fence_signal
 * before destruction. This will prevent possible issues from wraparound at
 * time of issue vs time of check, since users can check fence_is_signaled
 * before submitting instructions for the hardware to wait on the fence.
 * However, when ops.enable_signaling is not called, it doesn't have to be
 * done as soon as possible, just before there's any real danger of seqno
 * wraparound.
 */
static inline void
seqno_fence_init(struct seqno_fence *fence, spinlock_t *lock,
		 struct dma_buf *sync_buf,  uint32_t context,
		 uint32_t seqno_ofs, uint32_t seqno,
		 enum seqno_fence_condition cond,
		 const struct fence_ops *ops)
{
	BUG_ON(!fence || !sync_buf || !ops);
	BUG_ON(!ops->wait || !ops->enable_signaling ||
	       !ops->get_driver_name || !ops->get_timeline_name);

	/*
	 * ops is used in fence_init for get_driver_name, so needs to be
	 * initialized first
	 */
	fence->ops = ops;
	fence_init(&fence->base, &seqno_fence_ops, lock, context, seqno);
	get_dma_buf(sync_buf);
	fence->sync_buf = sync_buf;
	fence->seqno_ofs = seqno_ofs;
	fence->condition = cond;
}

#endif /* __LINUX_SEQNO_FENCE_H */
