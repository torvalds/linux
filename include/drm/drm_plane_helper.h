/*
 * Copyright (C) 2011-2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DRM_PLANE_HELPER_H
#define DRM_PLANE_HELPER_H

#include <drm/drm_rect.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_modeset_helper.h>

/*
 * Drivers that don't allow primary plane scaling may pass this macro in place
 * of the min/max scale parameters of the update checker function.
 *
 * Due to src being in 16.16 fixed point and dest being in integer pixels,
 * 1<<16 represents no scaling.
 */
#define DRM_PLANE_HELPER_NO_SCALING (1<<16)

int drm_plane_helper_check_state(struct drm_plane_state *plane_state,
				 const struct drm_crtc_state *crtc_state,
				 const struct drm_rect *clip,
				 int min_scale, int max_scale,
				 bool can_position,
				 bool can_update_disabled);
int drm_plane_helper_check_update(struct drm_plane *plane,
				  struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  struct drm_rect *src,
				  struct drm_rect *dest,
				  const struct drm_rect *clip,
				  unsigned int rotation,
				  int min_scale,
				  int max_scale,
				  bool can_position,
				  bool can_update_disabled,
				  bool *visible);
int drm_primary_helper_update(struct drm_plane *plane,
			      struct drm_crtc *crtc,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h,
			      struct drm_modeset_acquire_ctx *ctx);
int drm_primary_helper_disable(struct drm_plane *plane,
			       struct drm_modeset_acquire_ctx *ctx);
void drm_primary_helper_destroy(struct drm_plane *plane);
extern const struct drm_plane_funcs drm_primary_helper_funcs;

int drm_plane_helper_update(struct drm_plane *plane, struct drm_crtc *crtc,
			    struct drm_framebuffer *fb,
			    int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h);
int drm_plane_helper_disable(struct drm_plane *plane);

/* For use by drm_crtc_helper.c */
int drm_plane_helper_commit(struct drm_plane *plane,
			    struct drm_plane_state *plane_state,
			    struct drm_framebuffer *old_fb);
#endif
