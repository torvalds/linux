#ifndef _LINUX_OF_PLATFORM_H
#define _LINUX_OF_PLATFORM_H
/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#ifdef CONFIG_OF_DEVICE
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

/**
 * of_platform_driver - Legacy of-aware driver for platform devices.
 *
 * An of_platform_driver driver is attached to a basic platform_device on
 * the ibm ebus (ibmebus_bus_type).
 */
struct of_platform_driver
{
	int	(*probe)(struct platform_device* dev,
			 const struct of_device_id *match);
	int	(*remove)(struct platform_device* dev);

	int	(*suspend)(struct platform_device* dev, pm_message_t state);
	int	(*resume)(struct platform_device* dev);
	int	(*shutdown)(struct platform_device* dev);

	struct device_driver	driver;
};
#define	to_of_platform_driver(drv) \
	container_of(drv,struct of_platform_driver, driver)

/* Platform drivers register/unregister */
extern struct platform_device *of_device_alloc(struct device_node *np,
					 const char *bus_id,
					 struct device *parent);
extern struct platform_device *of_find_device_by_node(struct device_node *np);

#if !defined(CONFIG_SPARC) /* SPARC has its own device registration method */
/* Platform devices and busses creation */
extern struct platform_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent);

extern int of_platform_bus_probe(struct device_node *root,
				 const struct of_device_id *matches,
				 struct device *parent);
#endif /* !CONFIG_SPARC */

#endif /* CONFIG_OF_DEVICE */

#endif	/* _LINUX_OF_PLATFORM_H */
