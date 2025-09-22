/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I915_CONFIG_H__
#define __I915_CONFIG_H__

#include <linux/types.h>
#include <linux/limits.h>

struct drm_i915_private;

unsigned long i915_fence_context_timeout(const struct drm_i915_private *i915,
					 u64 context);

static inline unsigned long
i915_fence_timeout(const struct drm_i915_private *i915)
{
	return i915_fence_context_timeout(i915, U64_MAX);
}

#endif /* __I915_CONFIG_H__ */
