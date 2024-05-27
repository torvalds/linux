// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit-managed device implementation
 *
 * Implementation of struct kunit_device helpers for fake devices whose
 * lifecycle is managed by KUnit.
 *
 * Copyright (C) 2023, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <kunit/test.h>
#include <kunit/device.h>
#include <kunit/resource.h>

#include "device-impl.h"

/* Wrappers for use with kunit_add_action() */
KUNIT_DEFINE_ACTION_WRAPPER(device_unregister_wrapper, device_unregister, struct device *);
KUNIT_DEFINE_ACTION_WRAPPER(driver_unregister_wrapper, driver_unregister, struct device_driver *);

/* The root device for the KUnit bus, parent of all kunit_devices. */
static struct device *kunit_bus_device;

/* A device owned by a KUnit test. */
struct kunit_device {
	struct device dev;
	/* The KUnit test which owns this device. */
	struct kunit *owner;
	/* If the driver is managed by KUnit and unique to this device. */
	const struct device_driver *driver;
};

#define to_kunit_device(d) container_of_const(d, struct kunit_device, dev)

static const struct bus_type kunit_bus_type = {
	.name		= "kunit",
};

/* Register the 'kunit_bus' used for fake devices. */
int kunit_bus_init(void)
{
	int error;

	kunit_bus_device = root_device_register("kunit");
	if (IS_ERR(kunit_bus_device))
		return PTR_ERR(kunit_bus_device);

	error = bus_register(&kunit_bus_type);
	if (error)
		root_device_unregister(kunit_bus_device);
	return error;
}

/* Unregister the 'kunit_bus' in case the KUnit module is unloaded. */
void kunit_bus_shutdown(void)
{
	/* Make sure the bus exists before we unregister it. */
	if (IS_ERR_OR_NULL(kunit_bus_device))
		return;

	bus_unregister(&kunit_bus_type);

	root_device_unregister(kunit_bus_device);

	kunit_bus_device = NULL;
}

/* Release a 'fake' KUnit device. */
static void kunit_device_release(struct device *d)
{
	kfree(to_kunit_device(d));
}

/*
 * Create and register a KUnit-managed struct device_driver on the kunit_bus.
 * Returns an error pointer on failure.
 */
struct device_driver *kunit_driver_create(struct kunit *test, const char *name)
{
	struct device_driver *driver;
	int err = -ENOMEM;

	driver = kunit_kzalloc(test, sizeof(*driver), GFP_KERNEL);

	if (!driver)
		return ERR_PTR(err);

	driver->name = name;
	driver->bus = &kunit_bus_type;
	driver->owner = THIS_MODULE;

	err = driver_register(driver);
	if (err) {
		kunit_kfree(test, driver);
		return ERR_PTR(err);
	}

	kunit_add_action(test, driver_unregister_wrapper, driver);
	return driver;
}
EXPORT_SYMBOL_GPL(kunit_driver_create);

/* Helper which creates a kunit_device, attaches it to the kunit_bus*/
static struct kunit_device *kunit_device_register_internal(struct kunit *test,
							   const char *name,
							   const struct device_driver *drv)
{
	struct kunit_device *kunit_dev;
	int err = -ENOMEM;

	kunit_dev = kzalloc(sizeof(*kunit_dev), GFP_KERNEL);
	if (!kunit_dev)
		return ERR_PTR(err);

	kunit_dev->owner = test;

	err = dev_set_name(&kunit_dev->dev, "%s.%s", test->name, name);
	if (err) {
		kfree(kunit_dev);
		return ERR_PTR(err);
	}

	kunit_dev->dev.release = kunit_device_release;
	kunit_dev->dev.bus = &kunit_bus_type;
	kunit_dev->dev.parent = kunit_bus_device;

	err = device_register(&kunit_dev->dev);
	if (err) {
		put_device(&kunit_dev->dev);
		return ERR_PTR(err);
	}

	kunit_dev->dev.dma_mask = &kunit_dev->dev.coherent_dma_mask;
	kunit_dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	kunit_add_action(test, device_unregister_wrapper, &kunit_dev->dev);

	return kunit_dev;
}

/*
 * Create and register a new KUnit-managed device, using the user-supplied device_driver.
 * On failure, returns an error pointer.
 */
struct device *kunit_device_register_with_driver(struct kunit *test,
						 const char *name,
						 const struct device_driver *drv)
{
	struct kunit_device *kunit_dev = kunit_device_register_internal(test, name, drv);

	if (IS_ERR_OR_NULL(kunit_dev))
		return ERR_CAST(kunit_dev);

	return &kunit_dev->dev;
}
EXPORT_SYMBOL_GPL(kunit_device_register_with_driver);

/*
 * Create and register a new KUnit-managed device, including a matching device_driver.
 * On failure, returns an error pointer.
 */
struct device *kunit_device_register(struct kunit *test, const char *name)
{
	struct device_driver *drv;
	struct kunit_device *dev;

	drv = kunit_driver_create(test, name);
	if (IS_ERR(drv))
		return ERR_CAST(drv);

	dev = kunit_device_register_internal(test, name, drv);
	if (IS_ERR(dev)) {
		kunit_release_action(test, driver_unregister_wrapper, (void *)drv);
		return ERR_CAST(dev);
	}

	/* Request the driver be freed. */
	dev->driver = drv;


	return &dev->dev;
}
EXPORT_SYMBOL_GPL(kunit_device_register);

/* Unregisters a KUnit-managed device early (including the driver, if automatically created). */
void kunit_device_unregister(struct kunit *test, struct device *dev)
{
	const struct device_driver *driver = to_kunit_device(dev)->driver;

	kunit_release_action(test, device_unregister_wrapper, dev);
	if (driver)
		kunit_release_action(test, driver_unregister_wrapper, (void *)driver);
}
EXPORT_SYMBOL_GPL(kunit_device_unregister);

