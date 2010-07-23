#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

/*
 * The of_device *was* a kind of "base class" that was a superset of
 * struct device for use by devices attached to an OF node and probed
 * using OF properties.  However, the important bit of OF-style
 * probing, namely the device node pointer, has been moved into the
 * common struct device when CONFIG_OF is set to make OF-style probing
 * available to all bus types.  So now, just make of_device and
 * platform_device equivalent so that current of_platform bus users
 * can be transparently migrated over to using the platform bus.
 *
 * This line will go away once all references to of_device are removed
 * from the kernel.
 */
#define of_device platform_device
#include <linux/platform_device.h>
#include <linux/of_platform.h> /* temporary until merge */

#ifdef CONFIG_OF_DEVICE
#include <linux/device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>

#define	to_of_device(d) container_of(d, struct of_device, dev)

extern const struct of_device_id *of_match_device(
	const struct of_device_id *matches, const struct device *dev);
extern void of_device_make_bus_id(struct device *dev);

/**
 * of_driver_match_device - Tell if a driver's of_match_table matches a device.
 * @drv: the device_driver structure to test
 * @dev: the device structure to match against
 */
static inline int of_driver_match_device(const struct device *dev,
					 const struct device_driver *drv)
{
	return of_match_device(drv->of_match_table, dev) != NULL;
}

extern struct platform_device *of_dev_get(struct platform_device *dev);
extern void of_dev_put(struct platform_device *dev);

extern int of_device_register(struct platform_device *ofdev);
extern void of_device_unregister(struct platform_device *ofdev);
extern void of_release_dev(struct device *dev);

static inline void of_device_free(struct platform_device *dev)
{
	of_release_dev(&dev->dev);
}

extern ssize_t of_device_get_modalias(struct device *dev,
					char *str, ssize_t len);

extern int of_device_uevent(struct device *dev, struct kobj_uevent_env *env);


#else /* CONFIG_OF_DEVICE */

static inline int of_driver_match_device(struct device *dev,
					 struct device_driver *drv)
{
	return 0;
}

static inline int of_device_uevent(struct device *dev,
				   struct kobj_uevent_env *env)
{
	return -ENODEV;
}

#endif /* CONFIG_OF_DEVICE */

#endif /* _LINUX_OF_DEVICE_H */
