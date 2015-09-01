/*
 * soundbus
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */

#include <linux/module.h>
#include "soundbus.h"

MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Apple Soundbus");

struct soundbus_dev *soundbus_dev_get(struct soundbus_dev *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->ofdev.dev);
	if (tmp)
		return to_soundbus_device(tmp);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(soundbus_dev_get);

void soundbus_dev_put(struct soundbus_dev *dev)
{
	if (dev)
		put_device(&dev->ofdev.dev);
}
EXPORT_SYMBOL_GPL(soundbus_dev_put);

static int soundbus_probe(struct device *dev)
{
	int error = -ENODEV;
	struct soundbus_driver *drv;
	struct soundbus_dev *soundbus_dev;

	drv = to_soundbus_driver(dev->driver);
	soundbus_dev = to_soundbus_device(dev);

	if (!drv->probe)
		return error;

	soundbus_dev_get(soundbus_dev);

	error = drv->probe(soundbus_dev);
	if (error)
		soundbus_dev_put(soundbus_dev);

	return error;
}


static int soundbus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct soundbus_dev * soundbus_dev;
	struct platform_device * of;
	const char *compat;
	int retval = 0;
	int cplen, seen = 0;

	if (!dev)
		return -ENODEV;

	soundbus_dev = to_soundbus_device(dev);
	if (!soundbus_dev)
		return -ENODEV;

	of = &soundbus_dev->ofdev;

	/* stuff we want to pass to /sbin/hotplug */
	retval = add_uevent_var(env, "OF_NAME=%s", of->dev.of_node->name);
	if (retval)
		return retval;

	retval = add_uevent_var(env, "OF_TYPE=%s", of->dev.of_node->type);
	if (retval)
		return retval;

	/* Since the compatible field can contain pretty much anything
	 * it's not really legal to split it out with commas. We split it
	 * up using a number of environment variables instead. */

	compat = of_get_property(of->dev.of_node, "compatible", &cplen);
	while (compat && cplen > 0) {
		int tmp = env->buflen;
		retval = add_uevent_var(env, "OF_COMPATIBLE_%d=%s", seen, compat);
		if (retval)
			return retval;
		compat += env->buflen - tmp;
		cplen -= env->buflen - tmp;
		seen += 1;
	}

	retval = add_uevent_var(env, "OF_COMPATIBLE_N=%d", seen);
	if (retval)
		return retval;
	retval = add_uevent_var(env, "MODALIAS=%s", soundbus_dev->modalias);

	return retval;
}

static int soundbus_device_remove(struct device *dev)
{
	struct soundbus_dev * soundbus_dev = to_soundbus_device(dev);
	struct soundbus_driver * drv = to_soundbus_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(soundbus_dev);
	soundbus_dev_put(soundbus_dev);

	return 0;
}

static void soundbus_device_shutdown(struct device *dev)
{
	struct soundbus_dev * soundbus_dev = to_soundbus_device(dev);
	struct soundbus_driver * drv = to_soundbus_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(soundbus_dev);
}

#ifdef CONFIG_PM

static int soundbus_device_suspend(struct device *dev, pm_message_t state)
{
	struct soundbus_dev * soundbus_dev = to_soundbus_device(dev);
	struct soundbus_driver * drv = to_soundbus_driver(dev->driver);

	if (dev->driver && drv->suspend)
		return drv->suspend(soundbus_dev, state);
	return 0;
}

static int soundbus_device_resume(struct device * dev)
{
	struct soundbus_dev * soundbus_dev = to_soundbus_device(dev);
	struct soundbus_driver * drv = to_soundbus_driver(dev->driver);

	if (dev->driver && drv->resume)
		return drv->resume(soundbus_dev);
	return 0;
}

#endif /* CONFIG_PM */

/* soundbus_dev_attrs is declared in sysfs.c */
ATTRIBUTE_GROUPS(soundbus_dev);
static struct bus_type soundbus_bus_type = {
	.name		= "aoa-soundbus",
	.probe		= soundbus_probe,
	.uevent		= soundbus_uevent,
	.remove		= soundbus_device_remove,
	.shutdown	= soundbus_device_shutdown,
#ifdef CONFIG_PM
	.suspend	= soundbus_device_suspend,
	.resume		= soundbus_device_resume,
#endif
	.dev_groups	= soundbus_dev_groups,
};

int soundbus_add_one(struct soundbus_dev *dev)
{
	static int devcount;

	/* sanity checks */
	if (!dev->attach_codec ||
	    !dev->ofdev.dev.of_node ||
	    dev->pcmname ||
	    dev->pcmid != -1) {
		printk(KERN_ERR "soundbus: adding device failed sanity check!\n");
		return -EINVAL;
	}

	dev_set_name(&dev->ofdev.dev, "soundbus:%x", ++devcount);
	dev->ofdev.dev.bus = &soundbus_bus_type;
	return of_device_register(&dev->ofdev);
}
EXPORT_SYMBOL_GPL(soundbus_add_one);

void soundbus_remove_one(struct soundbus_dev *dev)
{
	of_device_unregister(&dev->ofdev);
}
EXPORT_SYMBOL_GPL(soundbus_remove_one);

int soundbus_register_driver(struct soundbus_driver *drv)
{
	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &soundbus_bus_type;

	/* register with core */
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(soundbus_register_driver);

void soundbus_unregister_driver(struct soundbus_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(soundbus_unregister_driver);

static int __init soundbus_init(void)
{
	return bus_register(&soundbus_bus_type);
}

static void __exit soundbus_exit(void)
{
	bus_unregister(&soundbus_bus_type);
}

subsys_initcall(soundbus_init);
module_exit(soundbus_exit);
