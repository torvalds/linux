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

#include <asm/of_device.h>

/*
 * The of_platform_bus_type is a bus type used by drivers that do not
 * attach to a macio or similar bus but still use OF probing
 * mechanism
 */
extern struct bus_type of_platform_bus_type;

/*
 * An of_platform_driver driver is attached to a basic of_device on
 * the "platform bus" (of_platform_bus_type)
 */
struct of_platform_driver
{
	char			*name;
	struct of_device_id	*match_table;
	struct module		*owner;

	int	(*probe)(struct of_device* dev,
			 const struct of_device_id *match);
	int	(*remove)(struct of_device* dev);

	int	(*suspend)(struct of_device* dev, pm_message_t state);
	int	(*resume)(struct of_device* dev);
	int	(*shutdown)(struct of_device* dev);

	struct device_driver	driver;
};
#define	to_of_platform_driver(drv) \
	container_of(drv,struct of_platform_driver, driver)

/* Platform drivers register/unregister */
extern int of_register_platform_driver(struct of_platform_driver *drv);
extern void of_unregister_platform_driver(struct of_platform_driver *drv);

/* Platform devices and busses creation */
extern struct of_device *of_platform_device_create(struct device_node *np,
						   const char *bus_id,
						   struct device *parent);
/* pseudo "matches" value to not do deep probe */
#define OF_NO_DEEP_PROBE ((struct of_device_id *)-1)

extern int of_platform_bus_probe(struct device_node *root,
				 struct of_device_id *matches,
				 struct device *parent);

extern struct of_device *of_find_device_by_node(struct device_node *np);
extern struct of_device *of_find_device_by_phandle(phandle ph);
