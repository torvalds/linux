/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _DRM_PAGEMAP_UTIL_H_
#define _DRM_PAGEMAP_UTIL_H_

struct drm_device;
struct drm_pagemap;
struct drm_pagemap_cache;
struct drm_pagemap_shrinker;

void drm_pagemap_shrinker_add(struct drm_pagemap *dpagemap);

int drm_pagemap_cache_lock_lookup(struct drm_pagemap_cache *cache);

void drm_pagemap_cache_unlock_lookup(struct drm_pagemap_cache *cache);

struct drm_pagemap_shrinker *drm_pagemap_shrinker_create_devm(struct drm_device *drm);

struct drm_pagemap_cache *drm_pagemap_cache_create_devm(struct drm_pagemap_shrinker *shrinker);

struct drm_pagemap *drm_pagemap_get_from_cache(struct drm_pagemap_cache *cache);

void drm_pagemap_cache_set_pagemap(struct drm_pagemap_cache *cache, struct drm_pagemap *dpagemap);

struct drm_pagemap *drm_pagemap_get_from_cache_if_active(struct drm_pagemap_cache *cache);

#ifdef CONFIG_PROVE_LOCKING

void drm_pagemap_shrinker_might_lock(struct drm_pagemap *dpagemap);

#else

static inline void drm_pagemap_shrinker_might_lock(struct drm_pagemap *dpagemap)
{
}

#endif /* CONFIG_PROVE_LOCKING */

#endif
