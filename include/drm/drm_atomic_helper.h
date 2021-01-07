/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#ifndef DRM_ATOMIC_HELPER_H_
#define DRM_ATOMIC_HELPER_H_

#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_util.h>

struct drm_atomic_state;
struct drm_private_obj;
struct drm_private_state;

int drm_atomic_helper_check_modeset(struct drm_device *dev,
				struct drm_atomic_state *state);
int drm_atomic_helper_check_plane_state(struct drm_plane_state *plane_state,
					const struct drm_crtc_state *crtc_state,
					int min_scale,
					int max_scale,
					bool can_position,
					bool can_update_disabled);
int drm_atomic_helper_check_planes(struct drm_device *dev,
			       struct drm_atomic_state *state);
int drm_atomic_helper_check(struct drm_device *dev,
			    struct drm_atomic_state *state);
void drm_atomic_helper_commit_tail(struct drm_atomic_state *state);
void drm_atomic_helper_commit_tail_rpm(struct drm_atomic_state *state);
int drm_atomic_helper_commit(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool nonblock);
int drm_atomic_helper_async_check(struct drm_device *dev,
				  struct drm_atomic_state *state);
void drm_atomic_helper_async_commit(struct drm_device *dev,
				    struct drm_atomic_state *state);

int drm_atomic_helper_wait_for_fences(struct drm_device *dev,
					struct drm_atomic_state *state,
					bool pre_swap);

void drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
					struct drm_atomic_state *old_state);

void drm_atomic_helper_wait_for_flip_done(struct drm_device *dev,
					  struct drm_atomic_state *old_state);

void
drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

void
drm_atomic_helper_calc_timestamping_constants(struct drm_atomic_state *state);

void drm_atomic_helper_commit_modeset_disables(struct drm_device *dev,
					       struct drm_atomic_state *state);
void drm_atomic_helper_commit_modeset_enables(struct drm_device *dev,
					  struct drm_atomic_state *old_state);

int drm_atomic_helper_prepare_planes(struct drm_device *dev,
				     struct drm_atomic_state *state);

#define DRM_PLANE_COMMIT_ACTIVE_ONLY			BIT(0)
#define DRM_PLANE_COMMIT_NO_DISABLE_AFTER_MODESET	BIT(1)

void drm_atomic_helper_commit_planes(struct drm_device *dev,
				     struct drm_atomic_state *state,
				     uint32_t flags);
void drm_atomic_helper_cleanup_planes(struct drm_device *dev,
				      struct drm_atomic_state *old_state);
void drm_atomic_helper_commit_planes_on_crtc(struct drm_crtc_state *old_crtc_state);
void
drm_atomic_helper_disable_planes_on_crtc(struct drm_crtc_state *old_crtc_state,
					 bool atomic);

int __must_check drm_atomic_helper_swap_state(struct drm_atomic_state *state,
					      bool stall);

/* nonblocking commit helpers */
int drm_atomic_helper_setup_commit(struct drm_atomic_state *state,
				   bool nonblock);
void drm_atomic_helper_wait_for_dependencies(struct drm_atomic_state *state);
void drm_atomic_helper_fake_vblank(struct drm_atomic_state *state);
void drm_atomic_helper_commit_hw_done(struct drm_atomic_state *state);
void drm_atomic_helper_commit_cleanup_done(struct drm_atomic_state *state);

/* implementations for legacy interfaces */
int drm_atomic_helper_update_plane(struct drm_plane *plane,
				   struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   uint32_t src_x, uint32_t src_y,
				   uint32_t src_w, uint32_t src_h,
				   struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_disable_plane(struct drm_plane *plane,
				    struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_set_config(struct drm_mode_set *set,
				 struct drm_modeset_acquire_ctx *ctx);

int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
void drm_atomic_helper_shutdown(struct drm_device *dev);
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
int drm_atomic_helper_commit_duplicated_state(struct drm_atomic_state *state,
					      struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);

int drm_atomic_helper_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags,
				struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_page_flip_target(
				struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags,
				uint32_t target,
				struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_legacy_gamma_set(struct drm_crtc *crtc,
				       u16 *red, u16 *green, u16 *blue,
				       uint32_t size,
				       struct drm_modeset_acquire_ctx *ctx);

/**
 * drm_atomic_crtc_for_each_plane - iterate over planes currently attached to CRTC
 * @plane: the loop cursor
 * @crtc:  the CRTC whose planes are iterated
 *
 * This iterates over the current state, useful (for example) when applying
 * atomic state after it has been checked and swapped.  To iterate over the
 * planes which *will* be attached (more useful in code called from
 * &drm_mode_config_funcs.atomic_check) see
 * drm_atomic_crtc_state_for_each_plane().
 */
#define drm_atomic_crtc_for_each_plane(plane, crtc) \
	drm_for_each_plane_mask(plane, (crtc)->dev, (crtc)->state->plane_mask)

/**
 * drm_atomic_crtc_state_for_each_plane - iterate over attached planes in new state
 * @plane: the loop cursor
 * @crtc_state: the incoming CRTC state
 *
 * Similar to drm_crtc_for_each_plane(), but iterates the planes that will be
 * attached if the specified state is applied.  Useful during for example
 * in code called from &drm_mode_config_funcs.atomic_check operations, to
 * validate the incoming state.
 */
#define drm_atomic_crtc_state_for_each_plane(plane, crtc_state) \
	drm_for_each_plane_mask(plane, (crtc_state)->state->dev, (crtc_state)->plane_mask)

/**
 * drm_atomic_crtc_state_for_each_plane_state - iterate over attached planes in new state
 * @plane: the loop cursor
 * @plane_state: loop cursor for the plane's state, must be const
 * @crtc_state: the incoming CRTC state
 *
 * Similar to drm_crtc_for_each_plane(), but iterates the planes that will be
 * attached if the specified state is applied.  Useful during for example
 * in code called from &drm_mode_config_funcs.atomic_check operations, to
 * validate the incoming state.
 *
 * Compared to just drm_atomic_crtc_state_for_each_plane() this also fills in a
 * const plane_state. This is useful when a driver just wants to peek at other
 * active planes on this CRTC, but does not need to change it.
 */
#define drm_atomic_crtc_state_for_each_plane_state(plane, plane_state, crtc_state) \
	drm_for_each_plane_mask(plane, (crtc_state)->state->dev, (crtc_state)->plane_mask) \
		for_each_if ((plane_state = \
			      __drm_atomic_get_current_plane_state((crtc_state)->state, \
								   plane)))

/**
 * drm_atomic_plane_disabling - check whether a plane is being disabled
 * @old_plane_state: old atomic plane state
 * @new_plane_state: new atomic plane state
 *
 * Checks the atomic state of a plane to determine whether it's being disabled
 * or not. This also WARNs if it detects an invalid state (both CRTC and FB
 * need to either both be NULL or both be non-NULL).
 *
 * RETURNS:
 * True if the plane is being disabled, false otherwise.
 */
static inline bool
drm_atomic_plane_disabling(struct drm_plane_state *old_plane_state,
			   struct drm_plane_state *new_plane_state)
{
	/*
	 * When disabling a plane, CRTC and FB should always be NULL together.
	 * Anything else should be considered a bug in the atomic core, so we
	 * gently warn about it.
	 */
	WARN_ON((new_plane_state->crtc == NULL && new_plane_state->fb != NULL) ||
		(new_plane_state->crtc != NULL && new_plane_state->fb == NULL));

	return old_plane_state->crtc && !new_plane_state->crtc;
}

u32 *
drm_atomic_helper_bridge_propagate_bus_fmt(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts);

#endif /* DRM_ATOMIC_HELPER_H_ */
