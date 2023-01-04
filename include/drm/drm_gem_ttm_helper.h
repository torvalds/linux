/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_GEM_TTM_HELPER_H
#define DRM_GEM_TTM_HELPER_H

#include <linux/container_of.h>

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo.h>

struct iosys_map;

#define drm_gem_ttm_of_gem(gem_obj) \
	container_of(gem_obj, struct ttm_buffer_object, base)

void drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
			    const struct drm_gem_object *gem);
int drm_gem_ttm_vmap(struct drm_gem_object *gem,
		     struct iosys_map *map);
void drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			struct iosys_map *map);
int drm_gem_ttm_mmap(struct drm_gem_object *gem,
		     struct vm_area_struct *vma);

int drm_gem_ttm_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				uint32_t handle, uint64_t *offset);

#endif
