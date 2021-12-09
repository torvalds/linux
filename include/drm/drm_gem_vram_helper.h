/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_GEM_VRAM_HELPER_H
#define DRM_GEM_VRAM_HELPER_H

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_modes.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>

#include <linux/dma-buf-map.h>
#include <linux/kernel.h> /* for container_of() */

struct drm_mode_create_dumb;
struct drm_plane;
struct drm_plane_state;
struct drm_simple_display_pipe;
struct filp;
struct vm_area_struct;

#define DRM_GEM_VRAM_PL_FLAG_SYSTEM	(1 << 0)
#define DRM_GEM_VRAM_PL_FLAG_VRAM	(1 << 1)
#define DRM_GEM_VRAM_PL_FLAG_TOPDOWN	(1 << 2)

/*
 * Buffer-object helpers
 */

/**
 * struct drm_gem_vram_object - GEM object backed by VRAM
 * @bo:		TTM buffer object
 * @map:	Mapping information for @bo
 * @placement:	TTM placement information. Supported placements are \
	%TTM_PL_VRAM and %TTM_PL_SYSTEM
 * @placements:	TTM placement information.
 *
 * The type struct drm_gem_vram_object represents a GEM object that is
 * backed by VRAM. It can be used for simple framebuffer devices with
 * dedicated memory. The buffer object can be evicted to system memory if
 * video memory becomes scarce.
 *
 * GEM VRAM objects perform reference counting for pin and mapping
 * operations. So a buffer object that has been pinned N times with
 * drm_gem_vram_pin() must be unpinned N times with
 * drm_gem_vram_unpin(). The same applies to pairs of
 * drm_gem_vram_kmap() and drm_gem_vram_kunmap(), as well as pairs of
 * drm_gem_vram_vmap() and drm_gem_vram_vunmap().
 */
struct drm_gem_vram_object {
	struct ttm_buffer_object bo;
	struct dma_buf_map map;

	/**
	 * @vmap_use_count:
	 *
	 * Reference count on the virtual address.
	 * The address are un-mapped when the count reaches zero.
	 */
	unsigned int vmap_use_count;

	/* Supported placements are %TTM_PL_VRAM and %TTM_PL_SYSTEM */
	struct ttm_placement placement;
	struct ttm_place placements[2];
};

/**
 * drm_gem_vram_of_bo - Returns the container of type
 * &struct drm_gem_vram_object for field bo.
 * @bo:		the VRAM buffer object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_bo(
	struct ttm_buffer_object *bo)
{
	return container_of(bo, struct drm_gem_vram_object, bo);
}

/**
 * drm_gem_vram_of_gem - Returns the container of type
 * &struct drm_gem_vram_object for field gem.
 * @gem:	the GEM object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_gem(
	struct drm_gem_object *gem)
{
	return container_of(gem, struct drm_gem_vram_object, bo.base);
}

struct drm_gem_vram_object *drm_gem_vram_create(struct drm_device *dev,
						size_t size,
						unsigned long pg_align);
void drm_gem_vram_put(struct drm_gem_vram_object *gbo);
s64 drm_gem_vram_offset(struct drm_gem_vram_object *gbo);
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag);
int drm_gem_vram_unpin(struct drm_gem_vram_object *gbo);
int drm_gem_vram_vmap(struct drm_gem_vram_object *gbo, struct dma_buf_map *map);
void drm_gem_vram_vunmap(struct drm_gem_vram_object *gbo, struct dma_buf_map *map);

int drm_gem_vram_fill_create_dumb(struct drm_file *file,
				  struct drm_device *dev,
				  unsigned long pg_align,
				  unsigned long pitch_align,
				  struct drm_mode_create_dumb *args);

/*
 * Helpers for struct drm_driver
 */

int drm_gem_vram_driver_dumb_create(struct drm_file *file,
				    struct drm_device *dev,
				    struct drm_mode_create_dumb *args);

/*
 * Helpers for struct drm_plane_helper_funcs
 */
int
drm_gem_vram_plane_helper_prepare_fb(struct drm_plane *plane,
				     struct drm_plane_state *new_state);
void
drm_gem_vram_plane_helper_cleanup_fb(struct drm_plane *plane,
				     struct drm_plane_state *old_state);

/**
 * DRM_GEM_VRAM_PLANE_HELPER_FUNCS -
 *	Initializes struct drm_plane_helper_funcs for VRAM handling
 *
 * Drivers may use GEM BOs as VRAM helpers for the framebuffer memory. This
 * macro initializes struct drm_plane_helper_funcs to use the respective helper
 * functions.
 */
#define DRM_GEM_VRAM_PLANE_HELPER_FUNCS \
	.prepare_fb = drm_gem_vram_plane_helper_prepare_fb, \
	.cleanup_fb = drm_gem_vram_plane_helper_cleanup_fb

/*
 * Helpers for struct drm_simple_display_pipe_funcs
 */

int drm_gem_vram_simple_display_pipe_prepare_fb(
	struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *new_state);

void drm_gem_vram_simple_display_pipe_cleanup_fb(
	struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *old_state);

/**
 * define DRM_GEM_VRAM_DRIVER - default callback functions for \
	&struct drm_driver
 *
 * Drivers that use VRAM MM and GEM VRAM can use this macro to initialize
 * &struct drm_driver with default functions.
 */
#define DRM_GEM_VRAM_DRIVER \
	.debugfs_init             = drm_vram_mm_debugfs_init, \
	.dumb_create		  = drm_gem_vram_driver_dumb_create, \
	.dumb_map_offset	  = drm_gem_ttm_dumb_map_offset, \
	.gem_prime_mmap		  = drm_gem_prime_mmap

/*
 *  VRAM memory manager
 */

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

	struct ttm_device bdev;
};

/**
 * drm_vram_mm_of_bdev() - \
	Returns the container of type &struct ttm_device for field bdev.
 * @bdev:	the TTM BO device
 *
 * Returns:
 * The containing instance of &struct drm_vram_mm
 */
static inline struct drm_vram_mm *drm_vram_mm_of_bdev(
	struct ttm_device *bdev)
{
	return container_of(bdev, struct drm_vram_mm, bdev);
}

void drm_vram_mm_debugfs_init(struct drm_minor *minor);

/*
 * Helpers for integration with struct drm_device
 */

int drmm_vram_helper_init(struct drm_device *dev, uint64_t vram_base,
			  size_t vram_size);

/*
 * Mode-config helpers
 */

enum drm_mode_status
drm_vram_helper_mode_valid(struct drm_device *dev,
			   const struct drm_display_mode *mode);

#endif
