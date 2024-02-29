/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wmi.h - ACPI WMI interface
 *
 * Copyright (c) 2015 Andrew Lutomirski
 */

#ifndef _LINUX_WMI_H
#define _LINUX_WMI_H

#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/mod_devicetable.h>

/**
 * struct wmi_device - WMI device structure
 * @dev: Device associated with this WMI device
 * @setable: True for devices implementing the Set Control Method
 *
 * This represents WMI devices discovered by the WMI driver core.
 */
struct wmi_device {
	struct device dev;
	bool setable;
};

/**
 * to_wmi_device() - Helper macro to cast a device to a wmi_device
 * @device: device struct
 *
 * Cast a struct device to a struct wmi_device.
 */
#define to_wmi_device(device)	container_of(device, struct wmi_device, dev)

extern acpi_status wmidev_evaluate_method(struct wmi_device *wdev,
					  u8 instance, u32 method_id,
					  const struct acpi_buffer *in,
					  struct acpi_buffer *out);

extern union acpi_object *wmidev_block_query(struct wmi_device *wdev,
					     u8 instance);

acpi_status wmidev_block_set(struct wmi_device *wdev, u8 instance, const struct acpi_buffer *in);

u8 wmidev_instance_count(struct wmi_device *wdev);

/**
 * struct wmi_driver - WMI driver structure
 * @driver: Driver model structure
 * @id_table: List of WMI GUIDs supported by this driver
 * @no_notify_data: WMI events provide no event data
 * @probe: Callback for device binding
 * @remove: Callback for device unbinding
 * @notify: Callback for receiving WMI events
 *
 * This represents WMI drivers which handle WMI devices.
 */
struct wmi_driver {
	struct device_driver driver;
	const struct wmi_device_id *id_table;
	bool no_notify_data;

	int (*probe)(struct wmi_device *wdev, const void *context);
	void (*remove)(struct wmi_device *wdev);
	void (*notify)(struct wmi_device *device, union acpi_object *data);
};

extern int __must_check __wmi_driver_register(struct wmi_driver *driver,
					      struct module *owner);
extern void wmi_driver_unregister(struct wmi_driver *driver);

/**
 * wmi_driver_register() - Helper macro to register a WMI driver
 * @driver: wmi_driver struct
 *
 * Helper macro for registering a WMI driver. It automatically passes
 * THIS_MODULE to the underlying function.
 */
#define wmi_driver_register(driver) __wmi_driver_register((driver), THIS_MODULE)

/**
 * module_wmi_driver() - Helper macro to register/unregister a WMI driver
 * @__wmi_driver: wmi_driver struct
 *
 * Helper macro for WMI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit().
 */
#define module_wmi_driver(__wmi_driver) \
	module_driver(__wmi_driver, wmi_driver_register, \
		      wmi_driver_unregister)

#endif
