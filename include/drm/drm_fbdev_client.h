/* SPDX-License-Identifier: MIT */

#ifndef DRM_FBDEV_CLIENT_H
#define DRM_FBDEV_CLIENT_H

struct drm_device;
struct drm_format_info;

#ifdef CONFIG_DRM_FBDEV_EMULATION
int drm_fbdev_client_setup(struct drm_device *dev, const struct drm_format_info *format);
#else
static inline int drm_fbdev_client_setup(struct drm_device *dev,
					 const struct drm_format_info *format)
{
	return 0;
}
#endif

#endif
