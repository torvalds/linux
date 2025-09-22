/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_DEBUGFS_H__
#define __I915_DEBUGFS_H__

struct drm_connector;
struct drm_i915_gem_object;
struct drm_i915_private;
struct seq_file;

#ifdef CONFIG_DEBUG_FS
void i915_debugfs_register(struct drm_i915_private *dev_priv);
void i915_debugfs_describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj);
#else
static inline void i915_debugfs_register(struct drm_i915_private *dev_priv) {}
static inline void i915_debugfs_describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj) {}
#endif

#endif /* __I915_DEBUGFS_H__ */
