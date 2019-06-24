/*
 * Copyright (C) 2015 Pengutronix, Uwe Kleine-König <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/device.h>

#define to_siox_device(_dev)	container_of((_dev), struct siox_device, dev)
struct siox_device {
	struct list_head node; /* node in smaster->devices */
	struct siox_master *smaster;
	struct device dev;

	const char *type;
	size_t inbytes;
	size_t outbytes;
	u8 statustype;

	u8 status_read_clean;
	u8 status_written;
	u8 status_written_lastcycle;
	bool connected;

	/* statistics */
	unsigned int watchdog_errors;
	unsigned int status_errors;

	struct kernfs_node *status_errors_kn;
	struct kernfs_node *watchdog_kn;
	struct kernfs_node *watchdog_errors_kn;
	struct kernfs_node *connected_kn;
};

bool siox_device_synced(struct siox_device *sdevice);
bool siox_device_connected(struct siox_device *sdevice);

struct siox_driver {
	int (*probe)(struct siox_device *sdevice);
	int (*remove)(struct siox_device *sdevice);
	void (*shutdown)(struct siox_device *sdevice);

	/*
	 * buf is big enough to hold sdev->inbytes - 1 bytes, the status byte
	 * is in the scope of the framework.
	 */
	int (*set_data)(struct siox_device *sdevice, u8 status, u8 buf[]);
	/*
	 * buf is big enough to hold sdev->outbytes - 1 bytes, the status byte
	 * is in the scope of the framework
	 */
	int (*get_data)(struct siox_device *sdevice, const u8 buf[]);

	struct device_driver driver;
};

static inline struct siox_driver *to_siox_driver(struct device_driver *driver)
{
	if (driver)
		return container_of(driver, struct siox_driver, driver);
	else
		return NULL;
}

int __siox_driver_register(struct siox_driver *sdriver, struct module *owner);

static inline int siox_driver_register(struct siox_driver *sdriver)
{
	return __siox_driver_register(sdriver, THIS_MODULE);
}

static inline void siox_driver_unregister(struct siox_driver *sdriver)
{
	return driver_unregister(&sdriver->driver);
}

/*
 * module_siox_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_siox_driver(__siox_driver) \
	module_driver(__siox_driver, siox_driver_register, \
			siox_driver_unregister)
