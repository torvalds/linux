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

/**
 * DOC: plane helpers
 *
 * Helper functions to assist with creation and handling of CRTC primary
 * planes.
 */

extern int drm_primary_helper_update(struct drm_plane *plane,
				     struct drm_crtc *crtc,
				     struct drm_framebuffer *fb,
				     int crtc_x, int crtc_y,
				     unsigned int crtc_w, unsigned int crtc_h,
				     uint32_t src_x, uint32_t src_y,
				     uint32_t src_w, uint32_t src_h);
extern int drm_primary_helper_disable(struct drm_plane *plane);
extern void drm_primary_helper_destroy(struct drm_plane *plane);
extern const struct drm_plane_funcs drm_primary_helper_funcs;
extern struct drm_plane *drm_primary_helper_create_plane(struct drm_device *dev,
							 const uint32_t *formats,
							 int num_formats);


#endif
