/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include <drm/drm_cache.h>

#include "i915_config.h"
#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "i915_gem_object_frontbuffer.h"
#include "i915_sw_fence_work.h"
#include "i915_trace.h"

struct clflush {
	struct dma_fence_work base;
	struct drm_i915_gem_object *obj;
};

static void __do_clflush(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	drm_clflush_sg(obj->mm.pages);

	i915_gem_object_flush_frontbuffer(obj, ORIGIN_CPU);
}

static void clflush_work(struct dma_fence_work *base)
{
	struct clflush *clflush = container_of(base, typeof(*clflush), base);

	__do_clflush(clflush->obj);
}

static void clflush_release(struct dma_fence_work *base)
{
	struct clflush *clflush = container_of(base, typeof(*clflush), base);

	i915_gem_object_unpin_pages(clflush->obj);
	i915_gem_object_put(clflush->obj);
}

static const struct dma_fence_work_ops clflush_ops = {
	.name = "clflush",
	.work = clflush_work,
	.release = clflush_release,
};

static struct clflush *clflush_work_create(struct drm_i915_gem_object *obj)
{
	struct clflush *clflush;

	GEM_BUG_ON(!obj->cache_dirty);

	clflush = kmalloc(sizeof(*clflush), GFP_KERNEL);
	if (!clflush)
		return NULL;

	if (__i915_gem_object_get_pages(obj) < 0) {
		kfree(clflush);
		return NULL;
	}

	dma_fence_work_init(&clflush->base, &clflush_ops);
	clflush->obj = i915_gem_object_get(obj); /* obj <-> clflush cycle */

	return clflush;
}

bool i915_gem_clflush_object(struct drm_i915_gem_object *obj,
			     unsigned int flags)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct clflush *clflush;

	assert_object_held(obj);

	if (IS_DGFX(i915)) {
		WARN_ON_ONCE(obj->cache_dirty);
		return false;
	}

	/*
	 * Stolen memory is always coherent with the GPU as it is explicitly
	 * marked as wc by the system, or the system is cache-coherent.
	 * Similarly, we only access struct pages through the CPU cache, so
	 * anything not backed by physical memory we consider to be always
	 * coherent and not need clflushing.
	 */
	if (!i915_gem_object_has_struct_page(obj)) {
		obj->cache_dirty = false;
		return false;
	}

	/* If the GPU is snooping the contents of the CPU cache,
	 * we do not need to manually clear the CPU cache lines.  However,
	 * the caches are only snooped when the render cache is
	 * flushed/invalidated.  As we always have to emit invalidations
	 * and flushes when moving into and out of the RENDER domain, correct
	 * snooping behaviour occurs naturally as the result of our domain
	 * tracking.
	 */
	if (!(flags & I915_CLFLUSH_FORCE) &&
	    obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ)
		return false;

	trace_i915_gem_object_clflush(obj);

	clflush = NULL;
	if (!(flags & I915_CLFLUSH_SYNC) &&
	    dma_resv_reserve_fences(obj->base.resv, 1) == 0)
		clflush = clflush_work_create(obj);
	if (clflush) {
		i915_sw_fence_await_reservation(&clflush->base.chain,
						obj->base.resv, true,
						i915_fence_timeout(i915),
						I915_FENCE_GFP);
		dma_resv_add_fence(obj->base.resv, &clflush->base.dma,
				   DMA_RESV_USAGE_KERNEL);
		dma_fence_work_commit(&clflush->base);
		/*
		 * We must have successfully populated the pages(since we are
		 * holding a pin on the pages as per the flush worker) to reach
		 * this point, which must mean we have already done the required
		 * flush-on-acquire, hence resetting cache_dirty here should be
		 * safe.
		 */
		obj->cache_dirty = false;
	} else if (obj->mm.pages) {
		__do_clflush(obj);
		obj->cache_dirty = false;
	} else {
		GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);
	}

	return true;
}
