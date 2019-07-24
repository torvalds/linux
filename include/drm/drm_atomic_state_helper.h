/*
 * Copyright (C) 2018 Intel Corp.
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

#include <linux/types.h>

struct drm_crtc;
struct drm_crtc_state;
struct drm_plane;
struct drm_plane_state;
struct drm_connector;
struct drm_connector_state;
struct drm_private_obj;
struct drm_private_state;
struct drm_modeset_acquire_ctx;
struct drm_device;

void __drm_atomic_helper_crtc_reset(struct drm_crtc *crtc,
				    struct drm_crtc_state *state);
void drm_atomic_helper_crtc_reset(struct drm_crtc *crtc);
void __drm_atomic_helper_crtc_duplicate_state(struct drm_crtc *crtc,
					      struct drm_crtc_state *state);
struct drm_crtc_state *
drm_atomic_helper_crtc_duplicate_state(struct drm_crtc *crtc);
void __drm_atomic_helper_crtc_destroy_state(struct drm_crtc_state *state);
void drm_atomic_helper_crtc_destroy_state(struct drm_crtc *crtc,
					  struct drm_crtc_state *state);

void __drm_atomic_helper_plane_reset(struct drm_plane *plane,
				     struct drm_plane_state *state);
void drm_atomic_helper_plane_reset(struct drm_plane *plane);
void __drm_atomic_helper_plane_duplicate_state(struct drm_plane *plane,
					       struct drm_plane_state *state);
struct drm_plane_state *
drm_atomic_helper_plane_duplicate_state(struct drm_plane *plane);
void __drm_atomic_helper_plane_destroy_state(struct drm_plane_state *state);
void drm_atomic_helper_plane_destroy_state(struct drm_plane *plane,
					  struct drm_plane_state *state);

void __drm_atomic_helper_connector_reset(struct drm_connector *connector,
					 struct drm_connector_state *conn_state);
void drm_atomic_helper_connector_reset(struct drm_connector *connector);
void drm_atomic_helper_connector_tv_reset(struct drm_connector *connector);
void
__drm_atomic_helper_connector_duplicate_state(struct drm_connector *connector,
					   struct drm_connector_state *state);
struct drm_connector_state *
drm_atomic_helper_connector_duplicate_state(struct drm_connector *connector);
void
__drm_atomic_helper_connector_destroy_state(struct drm_connector_state *state);
void drm_atomic_helper_connector_destroy_state(struct drm_connector *connector,
					  struct drm_connector_state *state);
void __drm_atomic_helper_private_obj_duplicate_state(struct drm_private_obj *obj,
						     struct drm_private_state *state);
