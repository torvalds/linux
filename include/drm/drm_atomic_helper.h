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

int drm_atomic_helper_check(struct drm_device *dev,
			    struct drm_atomic_state *state);
int drm_atomic_helper_commit(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool async);

void drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
					struct drm_atomic_state *old_state);

void drm_atomic_helper_commit_pre_planes(struct drm_device *dev,
					 struct drm_atomic_state *state);
void drm_atomic_helper_commit_post_planes(struct drm_device *dev,
					  struct drm_atomic_state *old_state);

int drm_atomic_helper_prepare_planes(struct drm_device *dev,
				     struct drm_atomic_state *state);
void drm_atomic_helper_commit_planes(struct drm_device *dev,
				     struct drm_atomic_state *state);
void drm_atomic_helper_cleanup_planes(struct drm_device *dev,
				      struct drm_atomic_state *old_state);

void drm_atomic_helper_swap_state(struct drm_device *dev,
				  struct drm_atomic_state *state);

/* implementations for legacy interfaces */
int drm_atomic_helper_update_plane(struct drm_plane *plane,
				   struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   uint32_t src_x, uint32_t src_y,
				   uint32_t src_w, uint32_t src_h);
int drm_atomic_helper_disable_plane(struct drm_plane *plane);
int drm_atomic_helper_set_config(struct drm_mode_set *set);

int drm_atomic_helper_crtc_set_property(struct drm_crtc *crtc,
					struct drm_property *property,
					uint64_t val);
int drm_atomic_helper_plane_set_property(struct drm_plane *plane,
					struct drm_property *property,
					uint64_t val);
int drm_atomic_helper_connector_set_property(struct drm_connector *connector,
					struct drm_property *property,
					uint64_t val);
int drm_atomic_helper_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags);

/* default implementations for state handling */
void drm_atomic_helper_crtc_reset(struct drm_crtc *crtc);
struct drm_crtc_state *
drm_atomic_helper_crtc_duplicate_state(struct drm_crtc *crtc);
void drm_atomic_helper_crtc_destroy_state(struct drm_crtc *crtc,
					  struct drm_crtc_state *state);

void drm_atomic_helper_plane_reset(struct drm_plane *plane);
struct drm_plane_state *
drm_atomic_helper_plane_duplicate_state(struct drm_plane *plane);
void drm_atomic_helper_plane_destroy_state(struct drm_plane *plane,
					  struct drm_plane_state *state);

void drm_atomic_helper_connector_reset(struct drm_connector *connector);
struct drm_connector_state *
drm_atomic_helper_connector_duplicate_state(struct drm_connector *connector);
void drm_atomic_helper_connector_destroy_state(struct drm_connector *connector,
					  struct drm_connector_state *state);


#endif /* DRM_ATOMIC_HELPER_H_ */
