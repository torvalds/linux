/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_VRAM_MM_HELPER_H
#define DRM_VRAM_MM_HELPER_H

#include <drm/ttm/ttm_bo_driver.h>

struct drm_device;

/**
 * struct drm_vram_mm_funcs - Callback functions for &struct drm_vram_mm
 * @evict_flags:	Provides an implementation for struct \
	&ttm_bo_driver.evict_flags
 * @verify_access:	Provides an implementation for \
	struct &ttm_bo_driver.verify_access
 *
 * These callback function integrate VRAM MM with TTM buffer objects. New
 * functions can be added if necessary.
 */
struct drm_vram_mm_funcs {
	void (*evict_flags)(struct ttm_buffer_object *bo,
			    struct ttm_placement *placement);
	int (*verify_access)(struct ttm_buffer_object *bo, struct file *filp);
};

/**
 * struct drm_vram_mm - An instance of VRAM MM
 * @vram_base:	Base address of the managed video memory
 * @vram_size:	Size of the managed video memory in bytes
 * @bdev:	The TTM BO device.
 * @funcs:	TTM BO functions
 *
 * The fields &struct drm_vram_mm.vram_base and
 * &struct drm_vram_mm.vrm_size are managed by VRAM MM, but are
 * available for public read access. Use the field
 * &struct drm_vram_mm.bdev to access the TTM BO device.
 */
struct drm_vram_mm {
	uint64_t vram_base;
	size_t vram_size;

	struct ttm_bo_device bdev;

	const struct drm_vram_mm_funcs *funcs;
};

/**
 * drm_vram_mm_of_bdev() - \
	Returns the container of type &struct ttm_bo_device for field bdev.
 * @bdev:	the TTM BO device
 *
 * Returns:
 * The containing instance of &struct drm_vram_mm
 */
static inline struct drm_vram_mm *drm_vram_mm_of_bdev(
	struct ttm_bo_device *bdev)
{
	return container_of(bdev, struct drm_vram_mm, bdev);
}

int drm_vram_mm_init(struct drm_vram_mm *vmm, struct drm_device *dev,
		     uint64_t vram_base, size_t vram_size,
		     const struct drm_vram_mm_funcs *funcs);
void drm_vram_mm_cleanup(struct drm_vram_mm *vmm);

int drm_vram_mm_mmap(struct file *filp, struct vm_area_struct *vma,
		     struct drm_vram_mm *vmm);

/*
 * Helpers for integration with struct drm_device
 */

struct drm_vram_mm *drm_vram_helper_alloc_mm(
	struct drm_device *dev, uint64_t vram_base, size_t vram_size,
	const struct drm_vram_mm_funcs *funcs);
void drm_vram_helper_release_mm(struct drm_device *dev);

/*
 * Helpers for &struct file_operations
 */

int drm_vram_mm_file_operations_mmap(
	struct file *filp, struct vm_area_struct *vma);

/**
 * define DRM_VRAM_MM_FILE_OPERATIONS - default callback functions for \
	&struct file_operations
 *
 * Drivers that use VRAM MM can use this macro to initialize
 * &struct file_operations with default functions.
 */
#define DRM_VRAM_MM_FILE_OPERATIONS \
	.llseek		= no_llseek, \
	.read		= drm_read, \
	.poll		= drm_poll, \
	.unlocked_ioctl = drm_ioctl, \
	.compat_ioctl	= drm_compat_ioctl, \
	.mmap		= drm_vram_mm_file_operations_mmap, \
	.open		= drm_open, \
	.release	= drm_release \

#endif
