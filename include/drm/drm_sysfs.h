/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DRM_SYSFS_H_
#define _DRM_SYSFS_H_

struct drm_device;
struct drm_connector;
struct drm_property;

void drm_sysfs_hotplug_event(struct drm_device *dev);
void drm_sysfs_connector_hotplug_event(struct drm_connector *connector);
void drm_sysfs_connector_property_event(struct drm_connector *connector,
					struct drm_property *property);
#endif
