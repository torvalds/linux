#ifndef _DRM_SYSFS_H_
#define _DRM_SYSFS_H_

struct drm_device;
struct device;

int drm_class_device_register(struct device *dev);
void drm_class_device_unregister(struct device *dev);

void drm_sysfs_hotplug_event(struct drm_device *dev);

#endif
