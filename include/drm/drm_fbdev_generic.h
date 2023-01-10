/* SPDX-License-Identifier: MIT */

#ifndef DRM_FBDEV_GENERIC_H
#define DRM_FBDEV_GENERIC_H

struct drm_device;

#ifdef CONFIG_DRM_FBDEV_EMULATION
void drm_fbdev_generic_setup(struct drm_device *dev, unsigned int preferred_bpp);
#else
static inline void drm_fbdev_generic_setup(struct drm_device *dev, unsigned int preferred_bpp)
{ }
#endif

#endif
