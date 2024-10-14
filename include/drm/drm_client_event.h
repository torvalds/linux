/* SPDX-License-Identifier: GPL-2.0 or MIT */

#ifndef _DRM_CLIENT_EVENT_H_
#define _DRM_CLIENT_EVENT_H_

struct drm_device;

void drm_client_dev_unregister(struct drm_device *dev);
void drm_client_dev_hotplug(struct drm_device *dev);
void drm_client_dev_restore(struct drm_device *dev);

#endif
