/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DRM_GEM_ATOMIC_HELPER_H__
#define __DRM_GEM_ATOMIC_HELPER_H__

#include <linux/dma-buf-map.h>

#include <drm/drm_plane.h>

struct drm_simple_display_pipe;

/*
 * Plane Helpers
 */

int drm_gem_plane_helper_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state);
int drm_gem_simple_display_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
					   struct drm_plane_state *plane_state);

/*
 * Helpers for planes with shadow buffers
 */

/**
 * struct drm_shadow_plane_state - plane state for planes with shadow buffers
 *
 * For planes that use a shadow buffer, struct drm_shadow_plane_state
 * provides the regular plane state plus mappings of the shadow buffer
 * into kernel address space.
 */
struct drm_shadow_plane_state {
	/** @base: plane state */
	struct drm_plane_state base;

	/* Transitional state - do not export or duplicate */

	/**
	 * @map: Mappings of the plane's framebuffer BOs in to kernel address space
	 *
	 * The memory mappings stored in map should be established in the plane's
	 * prepare_fb callback and removed in the cleanup_fb callback.
	 */
	struct dma_buf_map map[4];
};

/**
 * to_drm_shadow_plane_state - upcasts from struct drm_plane_state
 * @state: the plane state
 */
static inline struct drm_shadow_plane_state *
to_drm_shadow_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct drm_shadow_plane_state, base);
}

void drm_gem_reset_shadow_plane(struct drm_plane *plane);
struct drm_plane_state *drm_gem_duplicate_shadow_plane_state(struct drm_plane *plane);
void drm_gem_destroy_shadow_plane_state(struct drm_plane *plane,
					struct drm_plane_state *plane_state);

/**
 * DRM_GEM_SHADOW_PLANE_FUNCS -
 *	Initializes struct drm_plane_funcs for shadow-buffered planes
 *
 * Drivers may use GEM BOs as shadow buffers over the framebuffer memory. This
 * macro initializes struct drm_plane_funcs to use the rsp helper functions.
 */
#define DRM_GEM_SHADOW_PLANE_FUNCS \
	.reset = drm_gem_reset_shadow_plane, \
	.atomic_duplicate_state = drm_gem_duplicate_shadow_plane_state, \
	.atomic_destroy_state = drm_gem_destroy_shadow_plane_state

int drm_gem_prepare_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state);
void drm_gem_cleanup_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state);

/**
 * DRM_GEM_SHADOW_PLANE_HELPER_FUNCS -
 *	Initializes struct drm_plane_helper_funcs for shadow-buffered planes
 *
 * Drivers may use GEM BOs as shadow buffers over the framebuffer memory. This
 * macro initializes struct drm_plane_helper_funcs to use the rsp helper
 * functions.
 */
#define DRM_GEM_SHADOW_PLANE_HELPER_FUNCS \
	.prepare_fb = drm_gem_prepare_shadow_fb, \
	.cleanup_fb = drm_gem_cleanup_shadow_fb

int drm_gem_simple_kms_prepare_shadow_fb(struct drm_simple_display_pipe *pipe,
					 struct drm_plane_state *plane_state);
void drm_gem_simple_kms_cleanup_shadow_fb(struct drm_simple_display_pipe *pipe,
					  struct drm_plane_state *plane_state);
void drm_gem_simple_kms_reset_shadow_plane(struct drm_simple_display_pipe *pipe);
struct drm_plane_state *
drm_gem_simple_kms_duplicate_shadow_plane_state(struct drm_simple_display_pipe *pipe);
void drm_gem_simple_kms_destroy_shadow_plane_state(struct drm_simple_display_pipe *pipe,
						   struct drm_plane_state *plane_state);

/**
 * DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS -
 *	Initializes struct drm_simple_display_pipe_funcs for shadow-buffered planes
 *
 * Drivers may use GEM BOs as shadow buffers over the framebuffer memory. This
 * macro initializes struct drm_simple_display_pipe_funcs to use the rsp helper
 * functions.
 */
#define DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS \
	.prepare_fb = drm_gem_simple_kms_prepare_shadow_fb, \
	.cleanup_fb = drm_gem_simple_kms_cleanup_shadow_fb, \
	.reset_plane = drm_gem_simple_kms_reset_shadow_plane, \
	.duplicate_plane_state = drm_gem_simple_kms_duplicate_shadow_plane_state, \
	.destroy_plane_state = drm_gem_simple_kms_destroy_shadow_plane_state

#endif /* __DRM_GEM_ATOMIC_HELPER_H__ */
