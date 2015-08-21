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
#include <sound/ac97_codec.h>

/*
 * Let drivers decide whether they want to support given codec from their
 * probe method. Drivers have direct access to the struct snd_ac97
 * structure and may  decide based on the id field amongst other things.
 */
static int ac97_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

struct bus_type ac97_bus_type = {
	.name		= "ac97",
	.match		= ac97_bus_match,
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
