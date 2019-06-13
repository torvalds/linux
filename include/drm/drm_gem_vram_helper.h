/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_GEM_VRAM_HELPER_H
#define DRM_GEM_VRAM_HELPER_H

#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/kernel.h> /* for container_of() */

struct drm_mode_create_dumb;
struct drm_vram_mm_funcs;
struct filp;
struct vm_area_struct;

#define DRM_GEM_VRAM_PL_FLAG_VRAM	TTM_PL_FLAG_VRAM
#define DRM_GEM_VRAM_PL_FLAG_SYSTEM	TTM_PL_FLAG_SYSTEM

/*
 * Buffer-object helpers
 */

/**
 * struct drm_gem_vram_object - GEM object backed by VRAM
 * @gem:	GEM object
 * @bo:		TTM buffer object
 * @kmap:	Mapping information for @bo
 * @placement:	TTM placement information. Supported placements are \
	%TTM_PL_VRAM and %TTM_PL_SYSTEM
 * @placements:	TTM placement information.
 * @pin_count:	Pin counter
 *
 * The type struct drm_gem_vram_object represents a GEM object that is
 * backed by VRAM. It can be used for simple framebuffer devices with
 * dedicated memory. The buffer object can be evicted to system memory if
 * video memory becomes scarce.
 */
struct drm_gem_vram_object {
	struct drm_gem_object gem;
	struct ttm_buffer_object bo;
	struct ttm_bo_kmap_obj kmap;

	/* Supported placements are %TTM_PL_VRAM and %TTM_PL_SYSTEM */
	struct ttm_placement placement;
	struct ttm_place placements[2];

	int pin_count;
};

/**
 * Returns the container of type &struct drm_gem_vram_object
 * for field bo.
 * @bo:		the VRAM buffer object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_bo(
	struct ttm_buffer_object *bo)
{
	return container_of(bo, struct drm_gem_vram_object, bo);
}

/**
 * Returns the container of type &struct drm_gem_vram_object
 * for field gem.
 * @gem:	the GEM object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_gem(
	struct drm_gem_object *gem)
{
	return container_of(gem, struct drm_gem_vram_object, gem);
}

struct drm_gem_vram_object *drm_gem_vram_create(struct drm_device *dev,
						struct ttm_bo_device *bdev,
						size_t size,
						unsigned long pg_align,
						bool interruptible);
void drm_gem_vram_put(struct drm_gem_vram_object *gbo);
u64 drm_gem_vram_mmap_offset(struct drm_gem_vram_object *gbo);
s64 drm_gem_vram_offset(struct drm_gem_vram_object *gbo);
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag);
int drm_gem_vram_unpin(struct drm_gem_vram_object *gbo);
void *drm_gem_vram_kmap_at(struct drm_gem_vram_object *gbo, bool map,
			   bool *is_iomem, struct ttm_bo_kmap_obj *kmap);
void *drm_gem_vram_kmap(struct drm_gem_vram_object *gbo, bool map,
			bool *is_iomem);
void drm_gem_vram_kunmap_at(struct drm_gem_vram_object *gbo,
			    struct ttm_bo_kmap_obj *kmap);
void drm_gem_vram_kunmap(struct drm_gem_vram_object *gbo);

int drm_gem_vram_fill_create_dumb(struct drm_file *file,
				  struct drm_device *dev,
				  struct ttm_bo_device *bdev,
				  unsigned long pg_align,
				  bool interruptible,
				  struct drm_mode_create_dumb *args);

/*
 * Helpers for struct ttm_bo_driver
 */

void drm_gem_vram_bo_driver_evict_flags(struct ttm_buffer_object *bo,
					struct ttm_placement *pl);

int drm_gem_vram_bo_driver_verify_access(struct ttm_buffer_object *bo,
					 struct file *filp);

extern const struct drm_vram_mm_funcs drm_gem_vram_mm_funcs;

/*
 * Helpers for struct drm_driver
 */

void drm_gem_vram_driver_gem_free_object_unlocked(struct drm_gem_object *gem);
int drm_gem_vram_driver_dumb_create(struct drm_file *file,
				    struct drm_device *dev,
				    struct drm_mode_create_dumb *args);
int drm_gem_vram_driver_dumb_mmap_offset(struct drm_file *file,
					 struct drm_device *dev,
					 uint32_t handle, uint64_t *offset);

/**
 * define DRM_GEM_VRAM_DRIVER - default callback functions for \
	&struct drm_driver
 *
 * Drivers that use VRAM MM and GEM VRAM can use this macro to initialize
 * &struct drm_driver with default functions.
 */
#define DRM_GEM_VRAM_DRIVER \
	.gem_free_object_unlocked = \
		drm_gem_vram_driver_gem_free_object_unlocked, \
	.dumb_create		  = drm_gem_vram_driver_dumb_create, \
	.dumb_map_offset	  = drm_gem_vram_driver_dumb_mmap_offset

/*
 * PRIME helpers for struct drm_driver
 */

int drm_gem_vram_driver_gem_prime_pin(struct drm_gem_object *obj);
void drm_gem_vram_driver_gem_prime_unpin(struct drm_gem_object *obj);
void *drm_gem_vram_driver_gem_prime_vmap(struct drm_gem_object *obj);
void drm_gem_vram_driver_gem_prime_vunmap(struct drm_gem_object *obj,
					  void *vaddr);
int drm_gem_vram_driver_gem_prime_mmap(struct drm_gem_object *obj,
				       struct vm_area_struct *vma);

#define DRM_GEM_VRAM_DRIVER_PRIME \
	.gem_prime_export = drm_gem_prime_export, \
	.gem_prime_import = drm_gem_prime_import, \
	.gem_prime_pin	  = drm_gem_vram_driver_gem_prime_pin, \
	.gem_prime_unpin  = drm_gem_vram_driver_gem_prime_unpin, \
	.gem_prime_vmap	  = drm_gem_vram_driver_gem_prime_vmap, \
	.gem_prime_vunmap = drm_gem_vram_driver_gem_prime_vunmap, \
	.gem_prime_mmap	  = drm_gem_vram_driver_gem_prime_mmap

#endif
