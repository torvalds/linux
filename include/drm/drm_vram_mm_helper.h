/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_VRAM_MM_HELPER_H
#define DRM_VRAM_MM_HELPER_H

#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/ttm/ttm_bo_driver.h>

struct drm_device;

/**
 * struct drm_vram_mm_funcs - Callback functions for &struct drm_vram_mm
 * @evict_flags:	Provides an implementation for struct \
	&ttm_bo_driver.evict_flags
 * @verify_access:	Provides an implementation for \
	struct &ttm_bo_driver.verify_access
 * @move_notify:	Provides an implementation for
 *			struct &ttm_bo_driver.move_notify
 *
 * These callback function integrate VRAM MM with TTM buffer objects. New
 * functions can be added if necessary.
 */
struct drm_vram_mm_funcs {
	void (*evict_flags)(struct ttm_buffer_object *bo,
			    struct ttm_placement *placement);
	int (*verify_access)(struct ttm_buffer_object *bo, struct file *filp);
	void (*move_notify)(struct ttm_buffer_object *bo, bool evict,
			    struct ttm_mem_reg *new_mem);
};

#endif
