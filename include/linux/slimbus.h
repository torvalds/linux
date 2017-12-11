// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#ifndef _LINUX_SLIMBUS_H
#define _LINUX_SLIMBUS_H
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

extern struct bus_type slimbus_bus;

/**
 * struct slim_eaddr - Enumeration address for a SLIMbus device
 * @manf_id: Manufacturer Id for the device
 * @prod_code: Product code
 * @dev_index: Device index
 * @instance: Instance value
 */
struct slim_eaddr {
	u16 manf_id;
	u16 prod_code;
	u8 dev_index;
	u8 instance;
} __packed;

/**
 * enum slim_device_status - slim device status
 * @SLIM_DEVICE_STATUS_DOWN: Slim device is absent or not reported yet.
 * @SLIM_DEVICE_STATUS_UP: Slim device is announced on the bus.
 * @SLIM_DEVICE_STATUS_RESERVED: Reserved for future use.
 */
enum slim_device_status {
	SLIM_DEVICE_STATUS_DOWN = 0,
	SLIM_DEVICE_STATUS_UP,
	SLIM_DEVICE_STATUS_RESERVED,
};

struct slim_controller;

/**
 * struct slim_device - Slim device handle.
 * @dev: Driver model representation of the device.
 * @e_addr: Enumeration address of this device.
 * @status: slim device status
 * @ctrl: slim controller instance.
 * @laddr: 1-byte Logical address of this device.
 * @is_laddr_valid: indicates if the laddr is valid or not
 *
 * This is the client/device handle returned when a SLIMbus
 * device is registered with a controller.
 * Pointer to this structure is used by client-driver as a handle.
 */
struct slim_device {
	struct device		dev;
	struct slim_eaddr	e_addr;
	struct slim_controller	*ctrl;
	enum slim_device_status	status;
	u8			laddr;
	bool			is_laddr_valid;
};

#define to_slim_device(d) container_of(d, struct slim_device, dev)

/**
 * struct slim_driver - SLIMbus 'generic device' (slave) device driver
 *				(similar to 'spi_device' on SPI)
 * @probe: Binds this driver to a SLIMbus device.
 * @remove: Unbinds this driver from the SLIMbus device.
 * @shutdown: Standard shutdown callback used during powerdown/halt.
 * @device_status: This callback is called when
 *	- The device reports present and gets a laddr assigned
 *	- The device reports absent, or the bus goes down.
 * @driver: SLIMbus device drivers should initialize name and owner field of
 *	    this structure
 * @id_table: List of SLIMbus devices supported by this driver
 */

struct slim_driver {
	int	(*probe)(struct slim_device *sl);
	void	(*remove)(struct slim_device *sl);
	void	(*shutdown)(struct slim_device *sl);
	int	(*device_status)(struct slim_device *sl,
				 enum slim_device_status s);
	struct device_driver		driver;
	const struct slim_device_id	*id_table;
};
#define to_slim_driver(d) container_of(d, struct slim_driver, driver)

/*
 * use a macro to avoid include chaining to get THIS_MODULE
 */
#define slim_driver_register(drv) \
	__slim_driver_register(drv, THIS_MODULE)
int __slim_driver_register(struct slim_driver *drv, struct module *owner);
void slim_driver_unregister(struct slim_driver *drv);

/**
 * module_slim_driver() - Helper macro for registering a SLIMbus driver
 * @__slim_driver: slimbus_driver struct
 *
 * Helper macro for SLIMbus drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_slim_driver(__slim_driver) \
	module_driver(__slim_driver, slim_driver_register, \
			slim_driver_unregister)

static inline void *slim_get_devicedata(const struct slim_device *dev)
{
	return dev_get_drvdata(&dev->dev);
}

static inline void slim_set_devicedata(struct slim_device *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

struct slim_device *slim_get_device(struct slim_controller *ctrl,
				    struct slim_eaddr *e_addr);
int slim_get_logical_addr(struct slim_device *sbdev);
#endif /* _LINUX_SLIMBUS_H */
