// SPDX-License-Identifier: GPL-2.0

#ifndef DRM_KUNIT_HELPERS_H_
#define DRM_KUNIT_HELPERS_H_

#include <drm/drm_drv.h>

#include <linux/device.h>

#include <kunit/test.h>

struct drm_connector;
struct drm_crtc_funcs;
struct drm_crtc_helper_funcs;
struct drm_device;
struct drm_plane_funcs;
struct drm_plane_helper_funcs;
struct kunit;

struct device *drm_kunit_helper_alloc_device(struct kunit *test);
void drm_kunit_helper_free_device(struct kunit *test, struct device *dev);

struct drm_device *
__drm_kunit_helper_alloc_drm_device_with_driver(struct kunit *test,
						struct device *dev,
						size_t size, size_t offset,
						const struct drm_driver *driver);

/**
 * drm_kunit_helper_alloc_drm_device_with_driver - Allocates a mock DRM device for KUnit tests
 * @_test: The test context object
 * @_dev: The parent device object
 * @_type: the type of the struct which contains struct &drm_device
 * @_member: the name of the &drm_device within @_type.
 * @_drv: Mocked DRM device driver features
 *
 * This function creates a struct &drm_device from @_dev and @_drv.
 *
 * @_dev should be allocated using drm_kunit_helper_alloc_device().
 *
 * The driver is tied to the @_test context and will get cleaned at the
 * end of the test. The drm_device is allocated through
 * devm_drm_dev_alloc() and will thus be freed through a device-managed
 * resource.
 *
 * Returns:
 * A pointer to the new drm_device, or an ERR_PTR() otherwise.
 */
#define drm_kunit_helper_alloc_drm_device_with_driver(_test, _dev, _type, _member, _drv)	\
	((_type *)__drm_kunit_helper_alloc_drm_device_with_driver(_test, _dev,			\
						       sizeof(_type),				\
						       offsetof(_type, _member),		\
						       _drv))

static inline struct drm_device *
__drm_kunit_helper_alloc_drm_device(struct kunit *test,
				    struct device *dev,
				    size_t size, size_t offset,
				    u32 features)
{
	struct drm_driver *driver;

	driver = devm_kzalloc(dev, sizeof(*driver), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, driver);

	driver->driver_features = features;

	return __drm_kunit_helper_alloc_drm_device_with_driver(test, dev,
							       size, offset,
							       driver);
}

/**
 * drm_kunit_helper_alloc_drm_device - Allocates a mock DRM device for KUnit tests
 * @_test: The test context object
 * @_dev: The parent device object
 * @_type: the type of the struct which contains struct &drm_device
 * @_member: the name of the &drm_device within @_type.
 * @_feat: Mocked DRM device driver features
 *
 * This function creates a struct &drm_driver and will create a struct
 * &drm_device from @_dev and that driver.
 *
 * @_dev should be allocated using drm_kunit_helper_alloc_device().
 *
 * The driver is tied to the @_test context and will get cleaned at the
 * end of the test. The drm_device is allocated through
 * devm_drm_dev_alloc() and will thus be freed through a device-managed
 * resource.
 *
 * Returns:
 * A pointer to the new drm_device, or an ERR_PTR() otherwise.
 */
#define drm_kunit_helper_alloc_drm_device(_test, _dev, _type, _member, _feat)	\
	((_type *)__drm_kunit_helper_alloc_drm_device(_test, _dev,		\
						      sizeof(_type),		\
						      offsetof(_type, _member),	\
						      _feat))

struct drm_atomic_state *
drm_kunit_helper_atomic_state_alloc(struct kunit *test,
				    struct drm_device *drm,
				    struct drm_modeset_acquire_ctx *ctx);

struct drm_plane *
drm_kunit_helper_create_primary_plane(struct kunit *test,
				      struct drm_device *drm,
				      const struct drm_plane_funcs *funcs,
				      const struct drm_plane_helper_funcs *helper_funcs,
				      const uint32_t *formats,
				      unsigned int num_formats,
				      const uint64_t *modifiers);

struct drm_crtc *
drm_kunit_helper_create_crtc(struct kunit *test,
			     struct drm_device *drm,
			     struct drm_plane *primary,
			     struct drm_plane *cursor,
			     const struct drm_crtc_funcs *funcs,
			     const struct drm_crtc_helper_funcs *helper_funcs);

int drm_kunit_helper_enable_crtc_connector(struct kunit *test,
					   struct drm_device *drm,
					   struct drm_crtc *crtc,
					   struct drm_connector *connector,
					   const struct drm_display_mode *mode,
					   struct drm_modeset_acquire_ctx *ctx);

int drm_kunit_add_mode_destroy_action(struct kunit *test,
				      struct drm_display_mode *mode);

struct drm_display_mode *
drm_kunit_display_mode_from_cea_vic(struct kunit *test, struct drm_device *dev,
				    u8 video_code);

#endif // DRM_KUNIT_HELPERS_H_
