/* SPDX-License-Identifier: MIT */

#ifndef DRM_CLIENT_SETUP_H
#define DRM_CLIENT_SETUP_H

#include <linux/types.h>

struct drm_device;
struct drm_format_info;

#if defined(CONFIG_DRM_CLIENT_SETUP)
void drm_client_setup(struct drm_device *dev, const struct drm_format_info *format);
void drm_client_setup_with_fourcc(struct drm_device *dev, u32 fourcc);
void drm_client_setup_with_color_mode(struct drm_device *dev, unsigned int color_mode);
#else
static inline void drm_client_setup(struct drm_device *dev,
				    const struct drm_format_info *format)
{ }
static inline void drm_client_setup_with_fourcc(struct drm_device *dev, u32 fourcc)
{ }
static inline void drm_client_setup_with_color_mode(struct drm_device *dev,
						    unsigned int color_mode)
{ }
#endif

#endif
