/* SPDX-License-Identifier: GPL-2.0 or MIT */

#ifndef _DRM_CLIENT_EVENT_H_
#define _DRM_CLIENT_EVENT_H_

#include <linux/types.h>

struct drm_device;

#if defined(CONFIG_DRM_CLIENT)
void drm_client_dev_unregister(struct drm_device *dev);
void drm_client_dev_hotplug(struct drm_device *dev);
void drm_client_dev_restore(struct drm_device *dev, bool force);
void drm_client_dev_suspend(struct drm_device *dev);
void drm_client_dev_resume(struct drm_device *dev);
#else
static inline void drm_client_dev_unregister(struct drm_device *dev)
{ }
static inline void drm_client_dev_hotplug(struct drm_device *dev)
{ }
static inline void drm_client_dev_restore(struct drm_device *dev, bool force)
{ }
static inline void drm_client_dev_suspend(struct drm_device *dev)
{ }
static inline void drm_client_dev_resume(struct drm_device *dev)
{ }
#endif

#endif
