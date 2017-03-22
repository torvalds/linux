#ifndef _DRM_SYSFS_H_
#define _DRM_SYSFS_H_

/**
 * This minimalistic include file is intended for users (read TTM) that
 * don't want to include the full drmP.h file.
 */

int drm_class_device_register(struct device *dev);
void drm_class_device_unregister(struct device *dev);

#endif
