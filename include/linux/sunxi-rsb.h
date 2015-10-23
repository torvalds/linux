/*
 * Allwinner Reduced Serial Bus Driver
 *
 * Copyright (c) 2015 Chen-Yu Tsai
 *
 * Author: Chen-Yu Tsai <wens@csie.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _SUNXI_RSB_H
#define _SUNXI_RSB_H

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct sunxi_rsb;

/**
 * struct sunxi_rsb_device - Basic representation of an RSB device
 * @dev:	Driver model representation of the device.
 * @ctrl:	RSB controller managing the bus hosting this device.
 * @rtaddr:	This device's runtime address
 * @hwaddr:	This device's hardware address
 */
struct sunxi_rsb_device {
	struct device		dev;
	struct sunxi_rsb	*rsb;
	int			irq;
	u8			rtaddr;
	u16			hwaddr;
};

static inline struct sunxi_rsb_device *to_sunxi_rsb_device(struct device *d)
{
	return container_of(d, struct sunxi_rsb_device, dev);
}

static inline void *sunxi_rsb_device_get_drvdata(const struct sunxi_rsb_device *rdev)
{
	return dev_get_drvdata(&rdev->dev);
}

static inline void sunxi_rsb_device_set_drvdata(struct sunxi_rsb_device *rdev,
						void *data)
{
	dev_set_drvdata(&rdev->dev, data);
}

/**
 * struct sunxi_rsb_driver - RSB slave device driver
 * @driver:	RSB device drivers should initialize name and owner field of
 *		this structure.
 * @probe:	binds this driver to a RSB device.
 * @remove:	unbinds this driver from the RSB device.
 */
struct sunxi_rsb_driver {
	struct device_driver driver;
	int (*probe)(struct sunxi_rsb_device *rdev);
	int (*remove)(struct sunxi_rsb_device *rdev);
};

static inline struct sunxi_rsb_driver *to_sunxi_rsb_driver(struct device_driver *d)
{
	return container_of(d, struct sunxi_rsb_driver, driver);
}

int sunxi_rsb_driver_register(struct sunxi_rsb_driver *rdrv);

/**
 * sunxi_rsb_driver_unregister() - unregister an RSB client driver
 * @rdrv:	the driver to unregister
 */
static inline void sunxi_rsb_driver_unregister(struct sunxi_rsb_driver *rdrv)
{
	if (rdrv)
		driver_unregister(&rdrv->driver);
}

#define module_sunxi_rsb_driver(__sunxi_rsb_driver) \
	module_driver(__sunxi_rsb_driver, sunxi_rsb_driver_register, \
			sunxi_rsb_driver_unregister)

struct regmap *__devm_regmap_init_sunxi_rsb(struct sunxi_rsb_device *rdev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name);

/**
 * devm_regmap_init_sunxi_rsb(): Initialise managed register map
 *
 * @rdev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_sunxi_rsb(rdev, config)			\
	__regmap_lockdep_wrapper(__devm_regmap_init_sunxi_rsb, #config,	\
				 rdev, config)

#endif /* _SUNXI_RSB_H */
