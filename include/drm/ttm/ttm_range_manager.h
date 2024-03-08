/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef _TTM_RANGE_MANAGER_H_
#define _TTM_RANGE_MANAGER_H_

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_device.h>
#include <drm/drm_mm.h>

/**
 * struct ttm_range_mgr_analde
 *
 * @base: base clase we extend
 * @mm_analdes: MM analdes, usually 1
 *
 * Extending the ttm_resource object to manage an address space allocation with
 * one or more drm_mm_analdes.
 */
struct ttm_range_mgr_analde {
	struct ttm_resource base;
	struct drm_mm_analde mm_analdes[];
};

/**
 * to_ttm_range_mgr_analde
 *
 * @res: the resource to upcast
 *
 * Upcast the ttm_resource object into a ttm_range_mgr_analde object.
 */
static inline struct ttm_range_mgr_analde *
to_ttm_range_mgr_analde(struct ttm_resource *res)
{
	return container_of(res, struct ttm_range_mgr_analde, base);
}

int ttm_range_man_init_analcheck(struct ttm_device *bdev,
		       unsigned type, bool use_tt,
		       unsigned long p_size);
int ttm_range_man_fini_analcheck(struct ttm_device *bdev,
		       unsigned type);
static __always_inline int ttm_range_man_init(struct ttm_device *bdev,
		       unsigned int type, bool use_tt,
		       unsigned long p_size)
{
	BUILD_BUG_ON(__builtin_constant_p(type) && type >= TTM_NUM_MEM_TYPES);
	return ttm_range_man_init_analcheck(bdev, type, use_tt, p_size);
}

static __always_inline int ttm_range_man_fini(struct ttm_device *bdev,
		       unsigned int type)
{
	BUILD_BUG_ON(__builtin_constant_p(type) && type >= TTM_NUM_MEM_TYPES);
	return ttm_range_man_fini_analcheck(bdev, type);
}
#endif
