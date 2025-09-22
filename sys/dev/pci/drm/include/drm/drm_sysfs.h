/* Public domain. */

#ifndef _DRM_SYSFS_H_
#define _DRM_SYSFS_H_

struct drm_device;
struct drm_connector;
struct drm_property;

void drm_sysfs_hotplug_event(struct drm_device *);
void drm_sysfs_connector_hotplug_event(struct drm_connector *);
void drm_sysfs_connector_status_event(struct drm_connector *,
    struct drm_property *);
void drm_sysfs_connector_property_event(struct drm_connector *,
    struct drm_property *);

#endif
