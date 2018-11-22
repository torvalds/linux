/*
 * Copyright © 2017 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *
 */
#ifndef __DRM_SYNCOBJ_H__
#define __DRM_SYNCOBJ_H__

#include "linux/dma-fence.h"

struct drm_syncobj_cb;

/**
 * struct drm_syncobj - sync object.
 *
 * This structure defines a generic sync object which wraps a &dma_fence.
 */
struct drm_syncobj {
	/**
	 * @refcount: Reference count of this object.
	 */
	struct kref refcount;
	/**
	 * @fence:
	 * NULL or a pointer to the fence bound to this object.
	 *
	 * This field should not be used directly. Use drm_syncobj_fence_get()
	 * and drm_syncobj_replace_fence() instead.
	 */
	struct dma_fence __rcu *fence;
	/**
	 * @cb_list: List of callbacks to call when the &fence gets replaced.
	 */
	struct list_head cb_list;
	/**
	 * @lock: Protects &cb_list and write-locks &fence.
	 */
	spinlock_t lock;
	/**
	 * @file: A file backing for this syncobj.
	 */
	struct file *file;
};

typedef void (*drm_syncobj_func_t)(struct drm_syncobj *syncobj,
				   struct drm_syncobj_cb *cb);

/**
 * struct drm_syncobj_cb - callback for drm_syncobj_add_callback
 * @node: used by drm_syncob_add_callback to append this struct to
 *	  &drm_syncobj.cb_list
 * @func: drm_syncobj_func_t to call
 *
 * This struct will be initialized by drm_syncobj_add_callback, additional
 * data can be passed along by embedding drm_syncobj_cb in another struct.
 * The callback will get called the next time drm_syncobj_replace_fence is
 * called.
 */
struct drm_syncobj_cb {
	struct list_head node;
	drm_syncobj_func_t func;
};

void drm_syncobj_free(struct kref *kref);

/**
 * drm_syncobj_get - acquire a syncobj reference
 * @obj: sync object
 *
 * This acquires an additional reference to @obj. It is illegal to call this
 * without already holding a reference. No locks required.
 */
static inline void
drm_syncobj_get(struct drm_syncobj *obj)
{
	kref_get(&obj->refcount);
}

/**
 * drm_syncobj_put - release a reference to a sync object.
 * @obj: sync object.
 */
static inline void
drm_syncobj_put(struct drm_syncobj *obj)
{
	kref_put(&obj->refcount, drm_syncobj_free);
}

/**
 * drm_syncobj_fence_get - get a reference to a fence in a sync object
 * @syncobj: sync object.
 *
 * This acquires additional reference to &drm_syncobj.fence contained in @obj,
 * if not NULL. It is illegal to call this without already holding a reference.
 * No locks required.
 *
 * Returns:
 * Either the fence of @obj or NULL if there's none.
 */
static inline struct dma_fence *
drm_syncobj_fence_get(struct drm_syncobj *syncobj)
{
	struct dma_fence *fence;

	rcu_read_lock();
	fence = dma_fence_get_rcu_safe(&syncobj->fence);
	rcu_read_unlock();

	return fence;
}

struct drm_syncobj *drm_syncobj_find(struct drm_file *file_private,
				     u32 handle);
void drm_syncobj_replace_fence(struct drm_syncobj *syncobj, u64 point,
			       struct dma_fence *fence);
int drm_syncobj_find_fence(struct drm_file *file_private,
			   u32 handle, u64 point, u64 flags,
			   struct dma_fence **fence);
void drm_syncobj_free(struct kref *kref);
int drm_syncobj_create(struct drm_syncobj **out_syncobj, uint32_t flags,
		       struct dma_fence *fence);
int drm_syncobj_get_handle(struct drm_file *file_private,
			   struct drm_syncobj *syncobj, u32 *handle);
int drm_syncobj_get_fd(struct drm_syncobj *syncobj, int *p_fd);

#endif
