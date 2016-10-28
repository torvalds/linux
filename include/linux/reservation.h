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
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>

extern struct ww_class reservation_ww_class;
extern struct lock_class_key reservation_seqcount_class;
extern const char reservation_seqcount_string[];

/**
 * struct reservation_object_list - a list of shared fences
 * @rcu: for internal use
 * @shared_count: table of shared fences
 * @shared_max: for growing shared fence table
 * @shared: shared fence table
 */
struct reservation_object_list {
	struct rcu_head rcu;
	u32 shared_count, shared_max;
	struct dma_fence __rcu *shared[];
};

/**
 * struct reservation_object - a reservation object manages fences for a buffer
 * @lock: update side lock
 * @seq: sequence count for managing RCU read-side synchronization
 * @fence_excl: the exclusive fence, if there is one currently
 * @fence: list of current shared fences
 * @staged: staged copy of shared fences for RCU updates
 */
struct reservation_object {
	struct ww_mutex lock;
	seqcount_t seq;

	struct dma_fence __rcu *fence_excl;
	struct reservation_object_list __rcu *fence;
	struct reservation_object_list *staged;
};

#define reservation_object_held(obj) lockdep_is_held(&(obj)->lock.base)
#define reservation_object_assert_held(obj) \
	lockdep_assert_held(&(obj)->lock.base)

/**
 * reservation_object_init - initialize a reservation object
 * @obj: the reservation object
 */
static inline void
reservation_object_init(struct reservation_object *obj)
{
	ww_mutex_init(&obj->lock, &reservation_ww_class);

	__seqcount_init(&obj->seq, reservation_seqcount_string, &reservation_seqcount_class);
	RCU_INIT_POINTER(obj->fence, NULL);
	RCU_INIT_POINTER(obj->fence_excl, NULL);
	obj->staged = NULL;
}

/**
 * reservation_object_fini - destroys a reservation object
 * @obj: the reservation object
 */
static inline void
reservation_object_fini(struct reservation_object *obj)
{
	int i;
	struct reservation_object_list *fobj;
	struct dma_fence *excl;

	/*
	 * This object should be dead and all references must have
	 * been released to it, so no need to be protected with rcu.
	 */
	excl = rcu_dereference_protected(obj->fence_excl, 1);
	if (excl)
		dma_fence_put(excl);

	fobj = rcu_dereference_protected(obj->fence, 1);
	if (fobj) {
		for (i = 0; i < fobj->shared_count; ++i)
			dma_fence_put(rcu_dereference_protected(fobj->shared[i], 1));

		kfree(fobj);
	}
	kfree(obj->staged);

	ww_mutex_destroy(&obj->lock);
}

/**
 * reservation_object_get_list - get the reservation object's
 * shared fence list, with update-side lock held
 * @obj: the reservation object
 *
 * Returns the shared fence list.  Does NOT take references to
 * the fence.  The obj->lock must be held.
 */
static inline struct reservation_object_list *
reservation_object_get_list(struct reservation_object *obj)
{
	return rcu_dereference_protected(obj->fence,
					 reservation_object_held(obj));
}

/**
 * reservation_object_get_excl - get the reservation object's
 * exclusive fence, with update-side lock held
 * @obj: the reservation object
 *
 * Returns the exclusive fence (if any).  Does NOT take a
 * reference.  The obj->lock must be held.
 *
 * RETURNS
 * The exclusive fence or NULL
 */
static inline struct dma_fence *
reservation_object_get_excl(struct reservation_object *obj)
{
	return rcu_dereference_protected(obj->fence_excl,
					 reservation_object_held(obj));
}

/**
 * reservation_object_get_excl_rcu - get the reservation object's
 * exclusive fence, without lock held.
 * @obj: the reservation object
 *
 * If there is an exclusive fence, this atomically increments it's
 * reference count and returns it.
 *
 * RETURNS
 * The exclusive fence or NULL if none
 */
static inline struct dma_fence *
reservation_object_get_excl_rcu(struct reservation_object *obj)
{
	struct dma_fence *fence;
	unsigned seq;
retry:
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();
	fence = rcu_dereference(obj->fence_excl);
	if (read_seqcount_retry(&obj->seq, seq)) {
		rcu_read_unlock();
		goto retry;
	}
	fence = dma_fence_get(fence);
	rcu_read_unlock();
	return fence;
}

int reservation_object_reserve_shared(struct reservation_object *obj);
void reservation_object_add_shared_fence(struct reservation_object *obj,
					 struct dma_fence *fence);

void reservation_object_add_excl_fence(struct reservation_object *obj,
				       struct dma_fence *fence);

int reservation_object_get_fences_rcu(struct reservation_object *obj,
				      struct dma_fence **pfence_excl,
				      unsigned *pshared_count,
				      struct dma_fence ***pshared);

long reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout);

bool reservation_object_test_signaled_rcu(struct reservation_object *obj,
					  bool test_all);

#endif /* _LINUX_RESERVATION_H */
