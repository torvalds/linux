/* SPDX-License-Identifier: GPL-2.0
 *
 * CDX bus public interface
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 *
 */

#ifndef _CDX_BUS_H_
#define _CDX_BUS_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>

#define MAX_CDX_DEV_RESOURCES	4
#define CDX_CONTROLLER_ID_SHIFT 4
#define CDX_BUS_NUM_MASK 0xF

/* Forward declaration for CDX controller */
struct cdx_controller;

enum {
	CDX_DEV_BUS_MASTER_CONF,
	CDX_DEV_RESET_CONF,
};

struct cdx_device_config {
	u8 type;
	bool bus_master_enable;
};

typedef int (*cdx_bus_enable_cb)(struct cdx_controller *cdx, u8 bus_num);

typedef int (*cdx_bus_disable_cb)(struct cdx_controller *cdx, u8 bus_num);

typedef int (*cdx_scan_cb)(struct cdx_controller *cdx);

typedef int (*cdx_dev_configure_cb)(struct cdx_controller *cdx,
				    u8 bus_num, u8 dev_num,
				    struct cdx_device_config *dev_config);

/**
 * CDX_DEVICE - macro used to describe a specific CDX device
 * @vend: the 16 bit CDX Vendor ID
 * @dev: the 16 bit CDX Device ID
 *
 * This macro is used to create a struct cdx_device_id that matches a
 * specific device. The subvendor and subdevice fields will be set to
 * CDX_ANY_ID.
 */
#define CDX_DEVICE(vend, dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = CDX_ANY_ID, .subdevice = CDX_ANY_ID

/**
 * CDX_DEVICE_DRIVER_OVERRIDE - macro used to describe a CDX device with
 *                              override_only flags.
 * @vend: the 16 bit CDX Vendor ID
 * @dev: the 16 bit CDX Device ID
 * @driver_override: the 32 bit CDX Device override_only
 *
 * This macro is used to create a struct cdx_device_id that matches only a
 * driver_override device. The subvendor and subdevice fields will be set to
 * CDX_ANY_ID.
 */
#define CDX_DEVICE_DRIVER_OVERRIDE(vend, dev, driver_override) \
	.vendor = (vend), .device = (dev), .subvendor = CDX_ANY_ID,\
	.subdevice = CDX_ANY_ID, .override_only = (driver_override)

/**
 * struct cdx_ops - Callbacks supported by CDX controller.
 * @bus_enable: enable bus on the controller
 * @bus_disable: disable bus on the controller
 * @scan: scan the devices on the controller
 * @dev_configure: configuration like reset, master_enable,
 *		   msi_config etc for a CDX device
 */
struct cdx_ops {
	cdx_bus_enable_cb bus_enable;
	cdx_bus_disable_cb bus_disable;
	cdx_scan_cb scan;
	cdx_dev_configure_cb dev_configure;
};

/**
 * struct cdx_controller: CDX controller object
 * @dev: Linux device associated with the CDX controller.
 * @priv: private data
 * @id: Controller ID
 * @controller_registered: controller registered with bus
 * @ops: CDX controller ops
 */
struct cdx_controller {
	struct device *dev;
	void *priv;
	u32 id;
	bool controller_registered;
	struct cdx_ops *ops;
};

/**
 * struct cdx_device - CDX device object
 * @dev: Linux driver model device object
 * @cdx: CDX controller associated with the device
 * @vendor: Vendor ID for CDX device
 * @device: Device ID for CDX device
 * @subsystem_vendor: Subsystem Vendor ID for CDX device
 * @subsystem_device: Subsystem Device ID for CDX device
 * @class: Class for the CDX device
 * @revision: Revision of the CDX device
 * @bus_num: Bus number for this CDX device
 * @dev_num: Device number for this device
 * @res: array of MMIO region entries
 * @res_attr: resource binary attribute
 * @res_count: number of valid MMIO regions
 * @dma_mask: Default DMA mask
 * @flags: CDX device flags
 * @req_id: Requestor ID associated with CDX device
 * @is_bus: Is this bus device
 * @enabled: is this bus enabled
 * @driver_override: driver name to force a match; do not set directly,
 *                   because core frees it; use driver_set_override() to
 *                   set or clear it.
 */
struct cdx_device {
	struct device dev;
	struct cdx_controller *cdx;
	u16 vendor;
	u16 device;
	u16 subsystem_vendor;
	u16 subsystem_device;
	u32 class;
	u8 revision;
	u8 bus_num;
	u8 dev_num;
	struct resource res[MAX_CDX_DEV_RESOURCES];
	u8 res_count;
	u64 dma_mask;
	u16 flags;
	u32 req_id;
	bool is_bus;
	bool enabled;
	const char *driver_override;
};

#define to_cdx_device(_dev) \
	container_of(_dev, struct cdx_device, dev)

/**
 * struct cdx_driver - CDX device driver
 * @driver: Generic device driver
 * @match_id_table: table of supported device matching Ids
 * @probe: Function called when a device is added
 * @remove: Function called when a device is removed
 * @shutdown: Function called at shutdown time to quiesce the device
 * @reset_prepare: Function called before is reset to notify driver
 * @reset_done: Function called after reset is complete to notify driver
 * @driver_managed_dma: Device driver doesn't use kernel DMA API for DMA.
 *		For most device drivers, no need to care about this flag
 *		as long as all DMAs are handled through the kernel DMA API.
 *		For some special ones, for example VFIO drivers, they know
 *		how to manage the DMA themselves and set this flag so that
 *		the IOMMU layer will allow them to setup and manage their
 *		own I/O address space.
 */
struct cdx_driver {
	struct device_driver driver;
	const struct cdx_device_id *match_id_table;
	int (*probe)(struct cdx_device *dev);
	int (*remove)(struct cdx_device *dev);
	void (*shutdown)(struct cdx_device *dev);
	void (*reset_prepare)(struct cdx_device *dev);
	void (*reset_done)(struct cdx_device *dev);
	bool driver_managed_dma;
};

#define to_cdx_driver(_drv) \
	container_of(_drv, struct cdx_driver, driver)

/* Macro to avoid include chaining to get THIS_MODULE */
#define cdx_driver_register(drv) \
	__cdx_driver_register(drv, THIS_MODULE)

/**
 * __cdx_driver_register - registers a CDX device driver
 * @cdx_driver: CDX driver to register
 * @owner: module owner
 *
 * Return: -errno on failure, 0 on success.
 */
int __must_check __cdx_driver_register(struct cdx_driver *cdx_driver,
				       struct module *owner);

/**
 * cdx_driver_unregister - unregisters a device driver from the
 * CDX bus.
 * @cdx_driver: CDX driver to register
 */
void cdx_driver_unregister(struct cdx_driver *cdx_driver);

extern struct bus_type cdx_bus_type;

/**
 * cdx_dev_reset - Reset CDX device
 * @dev: device pointer
 *
 * Return: 0 for success, -errno on failure
 */
int cdx_dev_reset(struct device *dev);

/**
 * cdx_set_master - enables bus-mastering for CDX device
 * @cdx_dev: the CDX device to enable
 *
 * Return: 0 for success, -errno on failure
 */
int cdx_set_master(struct cdx_device *cdx_dev);

/**
 * cdx_clear_master - disables bus-mastering for CDX device
 * @cdx_dev: the CDX device to disable
 *
 * Return: 0 for success, -errno on failure
 */
int cdx_clear_master(struct cdx_device *cdx_dev);

#endif /* _CDX_BUS_H_ */
