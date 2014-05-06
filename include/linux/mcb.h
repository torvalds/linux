/*
 * MEN Chameleon Bus.
 *
 * Copyright (C) 2014 MEN Mikroelektronik GmbH (www.men.de)
 * Author: Johannes Thumshirn <johannes.thumshirn@men.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_MCB_H
#define _LINUX_MCB_H

#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/irqreturn.h>

struct mcb_driver;

/**
 * struct mcb_bus - MEN Chameleon Bus
 *
 * @dev: pointer to carrier device
 * @children: the child busses
 * @bus_nr: mcb bus number
 */
struct mcb_bus {
	struct list_head children;
	struct device dev;
	int bus_nr;
};
#define to_mcb_bus(b) container_of((b), struct mcb_bus, dev)

/**
 * struct mcb_device - MEN Chameleon Bus device
 *
 * @bus_list: internal list handling for bus code
 * @dev: device in kernel representation
 * @bus: mcb bus the device is plugged to
 * @subordinate: subordinate MCBus in case of bridge
 * @is_added: flag to check if device is added to bus
 * @driver: associated mcb_driver
 * @id: mcb device id
 * @inst: instance in Chameleon table
 * @group: group in Chameleon table
 * @var: variant in Chameleon table
 * @bar: BAR in Chameleon table
 * @rev: revision in Chameleon table
 * @irq: IRQ resource
 * @memory: memory resource
 */
struct mcb_device {
	struct list_head bus_list;
	struct device dev;
	struct mcb_bus *bus;
	struct mcb_bus *subordinate;
	bool is_added;
	struct mcb_driver *driver;
	u16 id;
	int inst;
	int group;
	int var;
	int bar;
	int rev;
	struct resource irq;
	struct resource mem;
};
#define to_mcb_device(x) container_of((x), struct mcb_device, dev)

/**
 * struct mcb_driver - MEN Chameleon Bus device driver
 *
 * @driver: device_driver
 * @id_table: mcb id table
 * @probe: probe callback
 * @remove: remove callback
 * @shutdown: shutdown callback
 */
struct mcb_driver {
	struct device_driver driver;
	const struct mcb_device_id *id_table;
	int (*probe)(struct mcb_device *mdev, const struct mcb_device_id *id);
	void (*remove)(struct mcb_device *mdev);
	void (*shutdown)(struct mcb_device *mdev);
};
#define to_mcb_driver(x) container_of((x), struct mcb_driver, driver)

static inline void *mcb_get_drvdata(struct mcb_device *dev)
{
	return dev_get_drvdata(&dev->dev);
}

static inline void mcb_set_drvdata(struct mcb_device *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

extern int __must_check __mcb_register_driver(struct mcb_driver *drv,
					struct module *owner,
					const char *mod_name);
#define mcb_register_driver(driver)		\
	__mcb_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)
extern void mcb_unregister_driver(struct mcb_driver *driver);
#define module_mcb_driver(__mcb_driver)		\
	module_driver(__mcb_driver, mcb_register_driver, mcb_unregister_driver);
extern void mcb_bus_add_devices(const struct mcb_bus *bus);
extern int mcb_device_register(struct mcb_bus *bus, struct mcb_device *dev);
extern struct mcb_bus *mcb_alloc_bus(void);
extern struct mcb_bus *mcb_bus_get(struct mcb_bus *bus);
extern void mcb_bus_put(struct mcb_bus *bus);
extern struct mcb_device *mcb_alloc_dev(struct mcb_bus *bus);
extern void mcb_free_dev(struct mcb_device *dev);
extern void mcb_release_bus(struct mcb_bus *bus);
extern struct resource *mcb_request_mem(struct mcb_device *dev,
					const char *name);
extern void mcb_release_mem(struct resource *mem);
extern int mcb_get_irq(struct mcb_device *dev);

#endif /* _LINUX_MCB_H */
