/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2011 Red Hat Inc.
 * Copyright © 2022 Intel Corporation
 */
#ifndef _DRM_SUBALLOC_H_
#define _DRM_SUBALLOC_H_

#include <drm/drm_mm.h>

#include <linux/dma-fence.h>
#include <linux/types.h>

#define DRM_SUBALLOC_MAX_QUEUES 32
/**
 * struct drm_suballoc_manager - fenced range allocations
 * @wq: Wait queue for sleeping allocations on contention.
 * @hole: Pointer to first hole node.
 * @olist: List of allocated ranges.
 * @flist: Array[fence context hash] of queues of fenced allocated ranges.
 * @size: Size of the managed range.
 * @align: Default alignment for the managed range.
 */
struct drm_suballoc_manager {
	wait_queue_head_t wq;
	struct list_head *hole;
	struct list_head olist;
	struct list_head flist[DRM_SUBALLOC_MAX_QUEUES];
	size_t size;
	size_t align;
};

/**
 * struct drm_suballoc - Sub-allocated range
 * @olist: List link for list of allocated ranges.
 * @flist: List linkk for the manager fenced allocated ranges queues.
 * @manager: The drm_suballoc_manager.
 * @soffset: Start offset.
 * @eoffset: End offset + 1 so that @eoffset - @soffset = size.
 * @dma_fence: The fence protecting the allocation.
 */
struct drm_suballoc {
	struct list_head olist;
	struct list_head flist;
	struct drm_suballoc_manager *manager;
	size_t soffset;
	size_t eoffset;
	struct dma_fence *fence;
};

void drm_suballoc_manager_init(struct drm_suballoc_manager *sa_manager,
			       size_t size, size_t align);

void drm_suballoc_manager_fini(struct drm_suballoc_manager *sa_manager);

struct drm_suballoc *
drm_suballoc_new(struct drm_suballoc_manager *sa_manager, size_t size,
		 gfp_t gfp, bool intr, size_t align);

void drm_suballoc_free(struct drm_suballoc *sa, struct dma_fence *fence);

/**
 * drm_suballoc_soffset - Range start.
 * @sa: The struct drm_suballoc.
 *
 * Return: The start of the allocated range.
 */
static inline size_t drm_suballoc_soffset(struct drm_suballoc *sa)
{
	return sa->soffset;
}

/**
 * drm_suballoc_eoffset - Range end.
 * @sa: The struct drm_suballoc.
 *
 * Return: The end of the allocated range + 1.
 */
static inline size_t drm_suballoc_eoffset(struct drm_suballoc *sa)
{
	return sa->eoffset;
}

/**
 * drm_suballoc_size - Range size.
 * @sa: The struct drm_suballoc.
 *
 * Return: The size of the allocated range.
 */
static inline size_t drm_suballoc_size(struct drm_suballoc *sa)
{
	return sa->eoffset - sa->soffset;
}

#ifdef CONFIG_DEBUG_FS
void drm_suballoc_dump_debug_info(struct drm_suballoc_manager *sa_manager,
				  struct drm_printer *p,
				  unsigned long long suballoc_base);
#else
static inline void
drm_suballoc_dump_debug_info(struct drm_suballoc_manager *sa_manager,
			     struct drm_printer *p,
			     unsigned long long suballoc_base)
{ }

#endif

#endif /* _DRM_SUBALLOC_H_ */
