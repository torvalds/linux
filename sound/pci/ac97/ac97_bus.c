/*
 * Linux driver model AC97 bus interface
 *
 * Author:	Nicolas Pitre
 * Created:	Jan 14, 2005
 * Copyright:	(C) MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>

/*
 * Codec families have names seperated by commas, so we search for an
 * individual codec name within the family string. 
 */
static int ac97_bus_match(struct device *dev, struct device_driver *drv)
{
	return (strstr(dev->bus_id, drv->name) != NULL);
}

static int ac97_bus_suspend(struct device *dev, pm_message_t state)
{
	int ret = 0;

	if (dev->driver && dev->driver->suspend) {
		ret = dev->driver->suspend(dev, state, SUSPEND_DISABLE);
		if (ret == 0)
			ret = dev->driver->suspend(dev, state, SUSPEND_SAVE_STATE);
		if (ret == 0)
			ret = dev->driver->suspend(dev, state, SUSPEND_POWER_DOWN);
	}
	return ret;
}

static int ac97_bus_resume(struct device *dev)
{
	int ret = 0;

	if (dev->driver && dev->driver->resume) {
		ret = dev->driver->resume(dev, RESUME_POWER_ON);
		if (ret == 0)
			ret = dev->driver->resume(dev, RESUME_RESTORE_STATE);
		if (ret == 0)
			ret = dev->driver->resume(dev, RESUME_ENABLE);
	}
	return ret;
}

struct bus_type ac97_bus_type = {
	.name		= "ac97",
	.match		= ac97_bus_match,
	.suspend	= ac97_bus_suspend,
	.resume		= ac97_bus_resume,
};

static int __init ac97_bus_init(void)
{
	return bus_register(&ac97_bus_type);
}

subsys_initcall(ac97_bus_init);

static void __exit ac97_bus_exit(void)
{
	bus_unregister(&ac97_bus_type);
}

module_exit(ac97_bus_exit);

EXPORT_SYMBOL(ac97_bus_type);

MODULE_LICENSE("GPL");
