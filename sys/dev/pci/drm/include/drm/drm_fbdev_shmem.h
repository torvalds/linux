/* SPDX-License-Identifier: MIT */

#ifndef DRM_FBDEV_SHMEM_H
#define DRM_FBDEV_SHMEM_H

struct drm_device;

#ifdef CONFIG_DRM_FBDEV_EMULATION
void drm_fbdev_shmem_setup(struct drm_device *dev, unsigned int preferred_bpp);
#else
static inline void drm_fbdev_shmem_setup(struct drm_device *dev, unsigned int preferred_bpp)
{ }
#endif

#endif
