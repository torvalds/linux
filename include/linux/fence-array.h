/*
 * fence-array: aggregates fence to be waited together
 *
 * Copyright (C) 2016 Collabora Ltd
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 * Authors:
 *	Gustavo Padovan <gustavo@padovan.org>
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

#ifndef __LINUX_FENCE_ARRAY_H
#define __LINUX_FENCE_ARRAY_H

#include <linux/fence.h>

/**
 * struct fence_array_cb - callback helper for fence array
 * @cb: fence callback structure for signaling
 * @array: reference to the parent fence array object
 */
struct fence_array_cb {
	struct fence_cb cb;
	struct fence_array *array;
};

/**
 * struct fence_array - fence to represent an array of fences
 * @base: fence base class
 * @lock: spinlock for fence handling
 * @num_fences: number of fences in the array
 * @num_pending: fences in the array still pending
 * @fences: array of the fences
 */
struct fence_array {
	struct fence base;

	spinlock_t lock;
	unsigned num_fences;
	atomic_t num_pending;
	struct fence **fences;
};

extern const struct fence_ops fence_array_ops;

/**
 * to_fence_array - cast a fence to a fence_array
 * @fence: fence to cast to a fence_array
 *
 * Returns NULL if the fence is not a fence_array,
 * or the fence_array otherwise.
 */
static inline struct fence_array *to_fence_array(struct fence *fence)
{
	if (fence->ops != &fence_array_ops)
		return NULL;

	return container_of(fence, struct fence_array, base);
}

struct fence_array *fence_array_create(int num_fences, struct fence **fences,
				       u64 context, unsigned seqno,
				       bool signal_on_any);

#endif /* __LINUX_FENCE_ARRAY_H */
