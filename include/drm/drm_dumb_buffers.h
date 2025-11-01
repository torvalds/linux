/* SPDX-License-Identifier: MIT */

#ifndef __DRM_DUMB_BUFFERS_H__
#define __DRM_DUMB_BUFFERS_H__

struct drm_device;
struct drm_mode_create_dumb;

int drm_mode_size_dumb(struct drm_device *dev,
		       struct drm_mode_create_dumb *args,
		       unsigned long hw_pitch_align,
		       unsigned long hw_size_align);

#endif
