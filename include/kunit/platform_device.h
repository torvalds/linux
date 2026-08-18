/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KUNIT_PLATFORM_DRIVER_H
#define _KUNIT_PLATFORM_DRIVER_H

struct completion;
struct kunit;
struct platform_device;
struct platform_driver;
struct platform_device_info;

struct platform_device *
kunit_platform_device_alloc(struct kunit *test, const char *name, int id);
int kunit_platform_device_add(struct kunit *test, struct platform_device *pdev);
struct platform_device *
kunit_platform_device_register_full(struct kunit *test,
				    const struct platform_device_info *pdevinfo);
void kunit_platform_device_unregister(struct kunit *test,
				      struct platform_device *pdev);

int kunit_platform_device_prepare_wait_for_probe(struct kunit *test,
						 struct platform_device *pdev,
						 struct completion *x);

int kunit_platform_driver_register(struct kunit *test,
				   struct platform_driver *drv);

#endif
