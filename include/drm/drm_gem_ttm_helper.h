/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_GEM_TTM_HELPER_H
#define DRM_GEM_TTM_HELPER_H

#include <linux/kernel.h>

#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>

#define drm_gem_ttm_of_gem(gem_obj) \
	container_of(gem_obj, struct ttm_buffer_object, base)

void drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
			    const struct drm_gem_object *gem);

#endif
