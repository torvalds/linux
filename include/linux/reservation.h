/*
 * Header file for reservations for dma-buf and ttm
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Copyright (C) 2012-2013 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 * Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/ww_mutex.h>
#include <linux/fence.h>
#include <linux/slab.h>

extern struct ww_class reservation_ww_class;

struct reservation_object_list {
	u32 shared_count, shared_max;
	struct fence *shared[];
};

struct reservation_object {
	struct ww_mutex lock;

	struct fence *fence_excl;
	struct reservation_object_list *fence;
	struct reservation_object_list *staged;
};

#define reservation_object_assert_held(obj) \
	lockdep_assert_held(&(obj)->lock.base)

static inline void
reservation_object_init(struct reservation_object *obj)
{
	ww_mutex_init(&obj->lock, &reservation_ww_class);

	obj->fence_excl = NULL;
	obj->fence = NULL;
	obj->staged = NULL;
}

static inline void
reservation_object_fini(struct reservation_object *obj)
{
	int i;
	struct reservation_object_list *fobj;

	/*
	 * This object should be dead and all references must have
	 * been released to it.
	 */
	if (obj->fence_excl)
		fence_put(obj->fence_excl);

	fobj = obj->fence;
	if (fobj) {
		for (i = 0; i < fobj->shared_count; ++i)
			fence_put(fobj->shared[i]);

		kfree(fobj);
	}
	kfree(obj->staged);

	ww_mutex_destroy(&obj->lock);
}

static inline struct reservation_object_list *
reservation_object_get_list(struct reservation_object *obj)
{
	reservation_object_assert_held(obj);

	return obj->fence;
}

static inline struct fence *
reservation_object_get_excl(struct reservation_object *obj)
{
	reservation_object_assert_held(obj);

	return obj->fence_excl;
}

int reservation_object_reserve_shared(struct reservation_object *obj);
void reservation_object_add_shared_fence(struct reservation_object *obj,
					 struct fence *fence);

void reservation_object_add_excl_fence(struct reservation_object *obj,
				       struct fence *fence);

#endif /* _LINUX_RESERVATION_H */
