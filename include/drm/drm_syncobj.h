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

/* Move the define here for the moment to avoid exposing the UAPI just yet */
#define DRM_SYNCOBJ_CREATE_TYPE_TIMELINE (1 << 1)

enum drm_syncobj_type {
	DRM_SYNCOBJ_TYPE_BINARY,
	DRM_SYNCOBJ_TYPE_TIMELINE
};

/**
 * struct drm_syncobj - sync object.
 *
 * This structure defines a generic sync object which is timeline based.
 */
struct drm_syncobj {
	/**
	 * @refcount: Reference count of this object.
	 */
	struct kref refcount;
	/**
	 * @type: indicate syncobj type
	 */
	enum drm_syncobj_type type;
	/**
	 * @wq: wait signal operation work queue
	 */
	wait_queue_head_t	wq;
	/**
	 * @timeline_context: fence context used by timeline
	 */
	u64 timeline_context;
	/**
	 * @timeline: syncobj timeline value, which indicates point is signaled.
	 */
	u64 timeline;
	/**
	 * @signal_point: which indicates the latest signaler point.
	 */
	u64 signal_point;
	/**
	 * @signal_pt_list: signaler point list.
	 */
	struct list_head signal_pt_list;

	/**
         * @cb_list: List of callbacks to call when the &fence gets replaced.
         */
	struct list_head cb_list;
	/**
	 * @pt_lock: Protects pt list.
	 */
	spinlock_t pt_lock;
	/**
	 * @cb_mutex: Protects syncobj cb list.
	 */
	struct mutex cb_mutex;
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
 *       &drm_syncobj.cb_list
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
int drm_syncobj_search_fence(struct drm_syncobj *syncobj, u64 point, u64 flags,
			     struct dma_fence **fence);

#endif
