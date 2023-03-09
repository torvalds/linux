/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 */

/*
 * The DRM mode setting helper functions are common code for drivers to use if
 * they wish.  Drivers are not forced to use this code in their
 * implementations but it would be useful if they code they do use at least
 * provides a consistent interface and operation to userspace
 */

#ifndef __DRM_CRTC_HELPER_H__
#define __DRM_CRTC_HELPER_H__

#include <linux/types.h>

struct drm_atomic_state;
struct drm_connector;
struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_encoder;
struct drm_framebuffer;
struct drm_mode_set;
struct drm_modeset_acquire_ctx;

void drm_helper_disable_unused_functions(struct drm_device *dev);
int drm_crtc_helper_set_config(struct drm_mode_set *set,
			       struct drm_modeset_acquire_ctx *ctx);
bool drm_crtc_helper_set_mode(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      int x, int y,
			      struct drm_framebuffer *old_fb);
int drm_crtc_helper_atomic_check(struct drm_crtc *crtc,
				 struct drm_atomic_state *state);
bool drm_helper_crtc_in_use(struct drm_crtc *crtc);
bool drm_helper_encoder_in_use(struct drm_encoder *encoder);

int drm_helper_connector_dpms(struct drm_connector *connector, int mode);

void drm_helper_resume_force_mode(struct drm_device *dev);
int drm_helper_force_disable_all(struct drm_device *dev);

#endif
