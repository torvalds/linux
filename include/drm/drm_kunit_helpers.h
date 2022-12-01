// SPDX-License-Identifier: GPL-2.0

#ifndef DRM_KUNIT_HELPERS_H_
#define DRM_KUNIT_HELPERS_H_

struct drm_device;
struct kunit;

struct device *drm_kunit_helper_alloc_device(struct kunit *test);
void drm_kunit_helper_free_device(struct kunit *test, struct device *dev);

struct drm_device *
drm_kunit_helper_alloc_drm_device(struct kunit *test, struct device *dev,
				  u32 features);

#endif // DRM_KUNIT_HELPERS_H_
