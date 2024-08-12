// SPDX-License-Identifier: GPL-2.0
/*
 * bus.h - the bus-specific portions of the driver model
 *
 * Copyright (c) 2001-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (c) 2004-2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2008-2009 Novell Inc.
 * Copyright (c) 2012-2019 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (c) 2012-2019 Linux Foundation
 *
 * See Documentation/driver-api/driver-model/ for more information.
 */

#ifndef _DEVICE_BUS_H_
#define _DEVICE_BUS_H_

#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/pm.h>

struct device_driver;
struct fwnode_handle;

/**
 * struct bus_type - The bus type of the device
 *
 * @name:	The name of the bus.
 * @dev_name:	Used for subsystems to enumerate devices like ("foo%u", dev->id).
 * @bus_groups:	Default attributes of the bus.
 * @dev_groups:	Default attributes of the devices on the bus.
 * @drv_groups: Default attributes of the device drivers on the bus.
 * @match:	Called, perhaps multiple times, whenever a new device or driver
 *		is added for this bus. It should return a positive value if the
 *		given device can be handled by the given driver and zero
 *		otherwise. It may also return error code if determining that
 *		the driver supports the device is not possible. In case of
 *		-EPROBE_DEFER it will queue the device for deferred probing.
 * @uevent:	Called when a device is added, removed, or a few other things
 *		that generate uevents to add the environment variables.
 * @probe:	Called when a new device or driver add to this bus, and callback
 *		the specific driver's probe to initial the matched device.
 * @sync_state:	Called to sync device state to software state after all the
 *		state tracking consumers linked to this device (present at
 *		the time of late_initcall) have successfully bound to a
 *		driver. If the device has no consumers, this function will
 *		be called at late_initcall_sync level. If the device has
 *		consumers that are never bound to a driver, this function
 *		will never get called until they do.
 * @remove:	Called when a device removed from this bus.
 * @shutdown:	Called at shut-down time to quiesce the device.
 *
 * @online:	Called to put the device back online (after offlining it).
 * @offline:	Called to put the device offline for hot-removal. May fail.
 *
 * @suspend:	Called when a device on this bus wants to go to sleep mode.
 * @resume:	Called to bring a device on this bus out of sleep mode.
 * @num_vf:	Called to find out how many virtual functions a device on this
 *		bus supports.
 * @dma_configure:	Called to setup DMA configuration on a device on
 *			this bus.
 * @dma_cleanup:	Called to cleanup DMA configuration on a device on
 *			this bus.
 * @pm:		Power management operations of this bus, callback the specific
 *		device driver's pm-ops.
 * @need_parent_lock:	When probing or removing a device on this bus, the
 *			device core should lock the device's parent.
 *
 * A bus is a channel between the processor and one or more devices. For the
 * purposes of the device model, all devices are connected via a bus, even if
 * it is an internal, virtual, "platform" bus. Buses can plug into each other.
 * A USB controller is usually a PCI device, for example. The device model
 * represents the actual connections between buses and the devices they control.
 * A bus is represented by the bus_type structure. It contains the name, the
 * default attributes, the bus' methods, PM operations, and the driver core's
 * private data.
 */
struct bus_type {
	const char		*name;
	const char		*dev_name;
	const struct attribute_group **bus_groups;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;

	int (*match)(struct device *dev, const struct device_driver *drv);
	int (*uevent)(const struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	void (*sync_state)(struct device *dev);
	void (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*online)(struct device *dev);
	int (*offline)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);

	int (*num_vf)(struct device *dev);

	int (*dma_configure)(struct device *dev);
	void (*dma_cleanup)(struct device *dev);

	const struct dev_pm_ops *pm;

	bool need_parent_lock;
};

int __must_check bus_register(const struct bus_type *bus);

void bus_unregister(const struct bus_type *bus);

int __must_check bus_rescan_devices(const struct bus_type *bus);

struct bus_attribute {
	struct attribute	attr;
	ssize_t (*show)(const struct bus_type *bus, char *buf);
	ssize_t (*store)(const struct bus_type *bus, const char *buf, size_t count);
};

#define BUS_ATTR_RW(_name) \
	struct bus_attribute bus_attr_##_name = __ATTR_RW(_name)
#define BUS_ATTR_RO(_name) \
	struct bus_attribute bus_attr_##_name = __ATTR_RO(_name)
#define BUS_ATTR_WO(_name) \
	struct bus_attribute bus_attr_##_name = __ATTR_WO(_name)

int __must_check bus_create_file(const struct bus_type *bus, struct bus_attribute *attr);
void bus_remove_file(const struct bus_type *bus, struct bus_attribute *attr);

/* Generic device matching functions that all busses can use to match with */
int device_match_name(struct device *dev, const void *name);
int device_match_of_node(struct device *dev, const void *np);
int device_match_fwnode(struct device *dev, const void *fwnode);
int device_match_devt(struct device *dev, const void *pdevt);
int device_match_acpi_dev(struct device *dev, const void *adev);
int device_match_acpi_handle(struct device *dev, const void *handle);
int device_match_any(struct device *dev, const void *unused);

/* iterator helpers for buses */
int bus_for_each_dev(const struct bus_type *bus, struct device *start, void *data,
		     int (*fn)(struct device *dev, void *data));
struct device *bus_find_device(const struct bus_type *bus, struct device *start,
			       const void *data,
			       int (*match)(struct device *dev, const void *data));
/**
 * bus_find_device_by_name - device iterator for locating a particular device
 * of a specific name.
 * @bus: bus type
 * @start: Device to begin with
 * @name: name of the device to match
 */
static inline struct device *bus_find_device_by_name(const struct bus_type *bus,
						     struct device *start,
						     const char *name)
{
	return bus_find_device(bus, start, name, device_match_name);
}

/**
 * bus_find_device_by_of_node : device iterator for locating a particular device
 * matching the of_node.
 * @bus: bus type
 * @np: of_node of the device to match.
 */
static inline struct device *
bus_find_device_by_of_node(const struct bus_type *bus, const struct device_node *np)
{
	return bus_find_device(bus, NULL, np, device_match_of_node);
}

/**
 * bus_find_device_by_fwnode : device iterator for locating a particular device
 * matching the fwnode.
 * @bus: bus type
 * @fwnode: fwnode of the device to match.
 */
static inline struct device *
bus_find_device_by_fwnode(const struct bus_type *bus, const struct fwnode_handle *fwnode)
{
	return bus_find_device(bus, NULL, fwnode, device_match_fwnode);
}

/**
 * bus_find_device_by_devt : device iterator for locating a particular device
 * matching the device type.
 * @bus: bus type
 * @devt: device type of the device to match.
 */
static inline struct device *bus_find_device_by_devt(const struct bus_type *bus,
						     dev_t devt)
{
	return bus_find_device(bus, NULL, &devt, device_match_devt);
}

/**
 * bus_find_next_device - Find the next device after a given device in a
 * given bus.
 * @bus: bus type
 * @cur: device to begin the search with.
 */
static inline struct device *
bus_find_next_device(const struct bus_type *bus,struct device *cur)
{
	return bus_find_device(bus, cur, NULL, device_match_any);
}

#ifdef CONFIG_ACPI
struct acpi_device;

/**
 * bus_find_device_by_acpi_dev : device iterator for locating a particular device
 * matching the ACPI COMPANION device.
 * @bus: bus type
 * @adev: ACPI COMPANION device to match.
 */
static inline struct device *
bus_find_device_by_acpi_dev(const struct bus_type *bus, const struct acpi_device *adev)
{
	return bus_find_device(bus, NULL, adev, device_match_acpi_dev);
}
#else
static inline struct device *
bus_find_device_by_acpi_dev(const struct bus_type *bus, const void *adev)
{
	return NULL;
}
#endif

int bus_for_each_drv(const struct bus_type *bus, struct device_driver *start,
		     void *data, int (*fn)(struct device_driver *, void *));
void bus_sort_breadthfirst(const struct bus_type *bus,
			   int (*compare)(const struct device *a,
					  const struct device *b));
/*
 * Bus notifiers: Get notified of addition/removal of devices
 * and binding/unbinding of drivers to devices.
 * In the long run, it should be a replacement for the platform
 * notify hooks.
 */
struct notifier_block;

int bus_register_notifier(const struct bus_type *bus, struct notifier_block *nb);
int bus_unregister_notifier(const struct bus_type *bus, struct notifier_block *nb);

/**
 * enum bus_notifier_event - Bus Notifier events that have happened
 * @BUS_NOTIFY_ADD_DEVICE: device is added to this bus
 * @BUS_NOTIFY_DEL_DEVICE: device is about to be removed from this bus
 * @BUS_NOTIFY_REMOVED_DEVICE: device is successfully removed from this bus
 * @BUS_NOTIFY_BIND_DRIVER: a driver is about to be bound to this device on this bus
 * @BUS_NOTIFY_BOUND_DRIVER: a driver is successfully bound to this device on this bus
 * @BUS_NOTIFY_UNBIND_DRIVER: a driver is about to be unbound from this device on this bus
 * @BUS_NOTIFY_UNBOUND_DRIVER: a driver is successfully unbound from this device on this bus
 * @BUS_NOTIFY_DRIVER_NOT_BOUND: a driver failed to be bound to this device on this bus
 *
 * These are the value passed to a bus notifier when a specific event happens.
 *
 * Note that bus notifiers are likely to be called with the device lock already
 * held by the driver core, so be careful in any notifier callback as to what
 * you do with the device structure.
 *
 * All bus notifiers are called with the target struct device * as an argument.
 */
enum bus_notifier_event {
	BUS_NOTIFY_ADD_DEVICE,
	BUS_NOTIFY_DEL_DEVICE,
	BUS_NOTIFY_REMOVED_DEVICE,
	BUS_NOTIFY_BIND_DRIVER,
	BUS_NOTIFY_BOUND_DRIVER,
	BUS_NOTIFY_UNBIND_DRIVER,
	BUS_NOTIFY_UNBOUND_DRIVER,
	BUS_NOTIFY_DRIVER_NOT_BOUND,
};

struct kset *bus_get_kset(const struct bus_type *bus);
struct device *bus_get_dev_root(const struct bus_type *bus);

#endif
