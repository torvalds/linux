/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _DRM_PAGEMAP_UTIL_H_
#define _DRM_PAGEMAP_UTIL_H_

#include <linux/list.h>
#include <linux/mutex.h>

struct drm_device;
struct drm_pagemap;
struct drm_pagemap_cache;
struct drm_pagemap_owner;
struct drm_pagemap_shrinker;

/**
 * struct drm_pagemap_peer - Structure representing a fast interconnect peer
 * @list: Pointer to a &struct drm_pagemap_owner_list used to keep track of peers
 * @link: List link for @list's list of peers.
 * @owner: Pointer to a &struct drm_pagemap_owner, common for a set of peers having
 * fast interconnects.
 * @private: Pointer private to the struct embedding this struct.
 */
struct drm_pagemap_peer {
	struct drm_pagemap_owner_list *list;
	struct list_head link;
	struct drm_pagemap_owner *owner;
	void *private;
};

/**
 * struct drm_pagemap_owner_list - Keeping track of peers and owners
 * @peer: List of peers.
 *
 * The owner list defines the scope where we identify peers having fast interconnects
 * and a common owner. Typically a driver has a single global owner list to
 * keep track of common owners for the driver's pagemaps.
 */
struct drm_pagemap_owner_list {
	/** @lock: Mutex protecting the @peers list. */
	struct mutex lock;
	/** @peers: List of peers. */
	struct list_head peers;
};

/*
 * Convenience macro to define an owner list.
 * Typically the owner list statically declared
 * driver-wide.
 */
#define DRM_PAGEMAP_OWNER_LIST_DEFINE(_name)	\
	struct drm_pagemap_owner_list _name = {	\
	  .lock = __MUTEX_INITIALIZER((_name).lock),	\
	  .peers = LIST_HEAD_INIT((_name).peers) }

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

void drm_pagemap_release_owner(struct drm_pagemap_peer *peer);

int drm_pagemap_acquire_owner(struct drm_pagemap_peer *peer,
			      struct drm_pagemap_owner_list *owner_list,
			      bool (*has_interconnect)(struct drm_pagemap_peer *peer1,
						       struct drm_pagemap_peer *peer2));
#endif
