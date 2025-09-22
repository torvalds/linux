// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/dma-resv.h>
#include "i915_gem_ww.h"
#include "gem/i915_gem_object.h"

void i915_gem_ww_ctx_init(struct i915_gem_ww_ctx *ww, bool intr)
{
	ww_acquire_init(&ww->ctx, &reservation_ww_class);
	INIT_LIST_HEAD(&ww->obj_list);
	ww->intr = intr;
	ww->contended = NULL;
}

static void i915_gem_ww_ctx_unlock_all(struct i915_gem_ww_ctx *ww)
{
	struct drm_i915_gem_object *obj;

	while ((obj = list_first_entry_or_null(&ww->obj_list, struct drm_i915_gem_object, obj_link))) {
		list_del(&obj->obj_link);
		i915_gem_object_unlock(obj);
		i915_gem_object_put(obj);
	}
}

void i915_gem_ww_unlock_single(struct drm_i915_gem_object *obj)
{
	list_del(&obj->obj_link);
	i915_gem_object_unlock(obj);
	i915_gem_object_put(obj);
}

void i915_gem_ww_ctx_fini(struct i915_gem_ww_ctx *ww)
{
	i915_gem_ww_ctx_unlock_all(ww);
	WARN_ON(ww->contended);
	ww_acquire_fini(&ww->ctx);
}

int __must_check i915_gem_ww_ctx_backoff(struct i915_gem_ww_ctx *ww)
{
	int ret = 0;

	if (WARN_ON(!ww->contended))
		return -EINVAL;

	i915_gem_ww_ctx_unlock_all(ww);
	if (ww->intr)
		ret = dma_resv_lock_slow_interruptible(ww->contended->base.resv, &ww->ctx);
	else
		dma_resv_lock_slow(ww->contended->base.resv, &ww->ctx);

	if (!ret)
		list_add_tail(&ww->contended->obj_link, &ww->obj_list);
	else
		i915_gem_object_put(ww->contended);

	ww->contended = NULL;

	return ret;
}
