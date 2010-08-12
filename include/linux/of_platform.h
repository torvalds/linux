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
 * ether the "platform bus" (platform_bus_type), or the ibm ebus
 * (ibmebus_bus_type).
 *
 * of_platform_driver is being phased out when used with the platform_bus_type,
 * and regular platform_drivers should be used instead.  When the transition
 * is complete, only ibmebus will be using this structure, and the
 * platform_driver member of this structure will be removed.
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
	struct platform_driver	platform_driver;
};
#define	to_of_platform_driver(drv) \
	container_of(drv,struct of_platform_driver, driver)

extern int of_register_driver(struct of_platform_driver *drv,
			      struct bus_type *bus);
extern void of_unregister_driver(struct of_platform_driver *drv);

/* Platform drivers register/unregister */
extern int of_register_platform_driver(struct of_platform_driver *drv);
extern void of_unregister_platform_driver(struct of_platform_driver *drv);

extern struct platform_device *of_device_alloc(struct device_node *np,
					 const char *bus_id,
					 struct device *parent);
extern struct platform_device *of_find_device_by_node(struct device_node *np);

extern int of_bus_type_init(struct bus_type *bus, const char *name);

#if !defined(CONFIG_SPARC) /* SPARC has its own device registration method */
/* Platform devices and busses creation */
extern struct platform_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent);

/* pseudo "matches" value to not do deep probe */
#define OF_NO_DEEP_PROBE ((struct of_device_id *)-1)

extern int of_platform_bus_probe(struct device_node *root,
				 const struct of_device_id *matches,
				 struct device *parent);
#endif /* !CONFIG_SPARC */

#endif /* CONFIG_OF_DEVICE */

#endif	/* _LINUX_OF_PLATFORM_H */
