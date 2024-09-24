// SPDX-License-Identifier: GPL-2.0
/*
 * Test managed platform driver
 */

#include <linux/completion.h>
#include <linux/device/bus.h>
#include <linux/device/driver.h>
#include <linux/platform_device.h>

#include <kunit/platform_device.h>
#include <kunit/resource.h>

struct kunit_platform_device_alloc_params {
	const char *name;
	int id;
};

static int kunit_platform_device_alloc_init(struct kunit_resource *res, void *context)
{
	struct kunit_platform_device_alloc_params *params = context;
	struct platform_device *pdev;

	pdev = platform_device_alloc(params->name, params->id);
	if (!pdev)
		return -ENOMEM;

	res->data = pdev;

	return 0;
}

static void kunit_platform_device_alloc_exit(struct kunit_resource *res)
{
	struct platform_device *pdev = res->data;

	platform_device_put(pdev);
}

/**
 * kunit_platform_device_alloc() - Allocate a KUnit test managed platform device
 * @test: test context
 * @name: device name of platform device to alloc
 * @id: identifier of platform device to alloc.
 *
 * Allocate a test managed platform device. The device is put when the test completes.
 *
 * Return: Allocated platform device on success, NULL on failure.
 */
struct platform_device *
kunit_platform_device_alloc(struct kunit *test, const char *name, int id)
{
	struct kunit_platform_device_alloc_params params = {
		.name = name,
		.id = id,
	};

	return kunit_alloc_resource(test,
				    kunit_platform_device_alloc_init,
				    kunit_platform_device_alloc_exit,
				    GFP_KERNEL, &params);
}
EXPORT_SYMBOL_GPL(kunit_platform_device_alloc);

static void kunit_platform_device_add_exit(struct kunit_resource *res)
{
	struct platform_device *pdev = res->data;

	platform_device_unregister(pdev);
}

static bool
kunit_platform_device_alloc_match(struct kunit *test,
				  struct kunit_resource *res, void *match_data)
{
	struct platform_device *pdev = match_data;

	return res->data == pdev && res->free == kunit_platform_device_alloc_exit;
}

KUNIT_DEFINE_ACTION_WRAPPER(platform_device_unregister_wrapper,
			    platform_device_unregister, struct platform_device *);
/**
 * kunit_platform_device_add() - Register a KUnit test managed platform device
 * @test: test context
 * @pdev: platform device to add
 *
 * Register a test managed platform device. The device is unregistered when the
 * test completes.
 *
 * Return: 0 on success, negative errno on failure.
 */
int kunit_platform_device_add(struct kunit *test, struct platform_device *pdev)
{
	struct kunit_resource *res;
	int ret;

	ret = platform_device_add(pdev);
	if (ret)
		return ret;

	res = kunit_find_resource(test, kunit_platform_device_alloc_match, pdev);
	if (res) {
		/*
		 * Transfer the reference count of the platform device if it
		 * was allocated with kunit_platform_device_alloc(). In this
		 * case, calling platform_device_put() when the test exits from
		 * kunit_platform_device_alloc_exit() would lead to reference
		 * count underflow because platform_device_unregister_wrapper()
		 * calls platform_device_unregister() which also calls
		 * platform_device_put().
		 *
		 * Usually callers transfer the refcount initialized in
		 * platform_device_alloc() to platform_device_add() by calling
		 * platform_device_unregister() when platform_device_add()
		 * succeeds or platform_device_put() when it fails. KUnit has to
		 * keep this straight by redirecting the free routine for the
		 * resource to the right function. Luckily this only has to
		 * account for the success scenario.
		 */
		res->free = kunit_platform_device_add_exit;
		kunit_put_resource(res);
	} else {
		ret = kunit_add_action_or_reset(test, platform_device_unregister_wrapper, pdev);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_platform_device_add);

struct kunit_platform_device_probe_nb {
	struct completion *x;
	struct device *dev;
	struct notifier_block nb;
};

static int kunit_platform_device_probe_notify(struct notifier_block *nb,
					      unsigned long event, void *data)
{
	struct kunit_platform_device_probe_nb *knb;
	struct device *dev = data;

	knb = container_of(nb, struct kunit_platform_device_probe_nb, nb);
	if (event != BUS_NOTIFY_BOUND_DRIVER || knb->dev != dev)
		return NOTIFY_DONE;

	complete(knb->x);

	return NOTIFY_OK;
}

static void kunit_platform_device_probe_nb_remove(void *nb)
{
	bus_unregister_notifier(&platform_bus_type, nb);
}

/**
 * kunit_platform_device_prepare_wait_for_probe() - Prepare a completion
 * variable to wait for a platform device to probe
 * @test: test context
 * @pdev: platform device to prepare to wait for probe of
 * @x: completion variable completed when @dev has probed
 *
 * Prepare a completion variable @x to wait for @pdev to probe. Waiting on the
 * completion forces a preemption, allowing the platform driver to probe.
 *
 * Example
 *
 * .. code-block:: c
 *
 *	static int kunit_platform_driver_probe(struct platform_device *pdev)
 *	{
 *		return 0;
 *	}
 *
 *	static void kunit_platform_driver_test(struct kunit *test)
 *	{
 *		struct platform_device *pdev;
 *		struct platform_driver *pdrv;
 *		DECLARE_COMPLETION_ONSTACK(comp);
 *
 *		pdev = kunit_platform_device_alloc(test, "kunit-platform", -1);
 *		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);
 *		KUNIT_ASSERT_EQ(test, 0, kunit_platform_device_add(test, pdev));
 *
 *		pdrv = kunit_kzalloc(test, sizeof(*pdrv), GFP_KERNEL);
 *		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdrv);
 *
 *		pdrv->probe = kunit_platform_driver_probe;
 *		pdrv->driver.name = "kunit-platform";
 *		pdrv->driver.owner = THIS_MODULE;
 *
 *		KUNIT_ASSERT_EQ(test, 0, kunit_platform_device_prepare_wait_for_probe(test, pdev, &comp));
 *		KUNIT_ASSERT_EQ(test, 0, kunit_platform_driver_register(test, pdrv));
 *
 *		KUNIT_EXPECT_NE(test, 0, wait_for_completion_timeout(&comp, 3 * HZ));
 *	}
 *
 * Return: 0 on success, negative errno on failure.
 */
int kunit_platform_device_prepare_wait_for_probe(struct kunit *test,
						 struct platform_device *pdev,
						 struct completion *x)
{
	struct device *dev = &pdev->dev;
	struct kunit_platform_device_probe_nb *knb;
	bool bound;

	knb = kunit_kzalloc(test, sizeof(*knb), GFP_KERNEL);
	if (!knb)
		return -ENOMEM;

	knb->nb.notifier_call = kunit_platform_device_probe_notify;
	knb->dev = dev;
	knb->x = x;

	device_lock(dev);
	bound = device_is_bound(dev);
	if (bound) {
		device_unlock(dev);
		complete(x);
		kunit_kfree(test, knb);
		return 0;
	}

	bus_register_notifier(&platform_bus_type, &knb->nb);
	device_unlock(&pdev->dev);

	return kunit_add_action_or_reset(test, kunit_platform_device_probe_nb_remove, &knb->nb);
}
EXPORT_SYMBOL_GPL(kunit_platform_device_prepare_wait_for_probe);

KUNIT_DEFINE_ACTION_WRAPPER(platform_driver_unregister_wrapper,
			    platform_driver_unregister, struct platform_driver *);
/**
 * kunit_platform_driver_register() - Register a KUnit test managed platform driver
 * @test: test context
 * @drv: platform driver to register
 *
 * Register a test managed platform driver. This allows callers to embed the
 * @drv in a container structure and use container_of() in the probe function
 * to pass information to KUnit tests.
 *
 * Example
 *
 * .. code-block:: c
 *
 *	struct kunit_test_context {
 *		struct platform_driver pdrv;
 *		const char *data;
 *	};
 *
 *	static inline struct kunit_test_context *
 *	to_test_context(struct platform_device *pdev)
 *	{
 *		return container_of(to_platform_driver(pdev->dev.driver),
 *				    struct kunit_test_context,
 *				    pdrv);
 *	}
 *
 *	static int kunit_platform_driver_probe(struct platform_device *pdev)
 *	{
 *		struct kunit_test_context *ctx;
 *
 *		ctx = to_test_context(pdev);
 *		ctx->data = "test data";
 *
 *		return 0;
 *	}
 *
 *	static void kunit_platform_driver_test(struct kunit *test)
 *	{
 *		struct kunit_test_context *ctx;
 *
 *		ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
 *		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
 *
 *		ctx->pdrv.probe = kunit_platform_driver_probe;
 *		ctx->pdrv.driver.name = "kunit-platform";
 *		ctx->pdrv.driver.owner = THIS_MODULE;
 *
 *		KUNIT_EXPECT_EQ(test, 0, kunit_platform_driver_register(test, &ctx->pdrv));
 *		<... wait for driver to probe ...>
 *		KUNIT_EXPECT_STREQ(test, ctx->data, "test data");
 *	}
 *
 * Return: 0 on success, negative errno on failure.
 */
int kunit_platform_driver_register(struct kunit *test,
				   struct platform_driver *drv)
{
	int ret;

	ret = platform_driver_register(drv);
	if (ret)
		return ret;

	return kunit_add_action_or_reset(test, platform_driver_unregister_wrapper, drv);
}
EXPORT_SYMBOL_GPL(kunit_platform_driver_register);
