/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020 Intel Corporation
 *
 * Please see Documentation/driver-api/auxiliary_bus.rst for more information.
 */

#ifndef _AUXILIARY_BUS_H_
#define _AUXILIARY_BUS_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>

/**
 * DOC: DEVICE_LIFESPAN
 *
 * The registering driver is the entity that allocates memory for the
 * auxiliary_device and registers it on the auxiliary bus.  It is important to
 * note that, as opposed to the platform bus, the registering driver is wholly
 * responsible for the management of the memory used for the device object.
 *
 * To be clear the memory for the auxiliary_device is freed in the release()
 * callback defined by the registering driver.  The registering driver should
 * only call auxiliary_device_delete() and then auxiliary_device_uninit() when
 * it is done with the device.  The release() function is then automatically
 * called if and when other code releases their reference to the devices.
 *
 * A parent object, defined in the shared header file, contains the
 * auxiliary_device.  It also contains a pointer to the shared object(s), which
 * also is defined in the shared header.  Both the parent object and the shared
 * object(s) are allocated by the registering driver.  This layout allows the
 * auxiliary_driver's registering module to perform a container_of() call to go
 * from the pointer to the auxiliary_device, that is passed during the call to
 * the auxiliary_driver's probe function, up to the parent object, and then
 * have access to the shared object(s).
 *
 * The memory for the shared object(s) must have a lifespan equal to, or
 * greater than, the lifespan of the memory for the auxiliary_device.  The
 * auxiliary_driver should only consider that the shared object is valid as
 * long as the auxiliary_device is still registered on the auxiliary bus.  It
 * is up to the registering driver to manage (e.g. free or keep available) the
 * memory for the shared object beyond the life of the auxiliary_device.
 *
 * The registering driver must unregister all auxiliary devices before its own
 * driver.remove() is completed.  An easy way to ensure this is to use the
 * devm_add_action_or_reset() call to register a function against the parent
 * device which unregisters the auxiliary device object(s).
 *
 * Finally, any operations which operate on the auxiliary devices must continue
 * to function (if only to return an error) after the registering driver
 * unregisters the auxiliary device.
 */

/**
 * struct auxiliary_device - auxiliary device object.
 * @dev: Device,
 *       The release and parent fields of the device structure must be filled
 *       in
 * @name: Match name found by the auxiliary device driver,
 * @id: unique identitier if multiple devices of the same name are exported,
 * @sysfs: embedded struct which hold all sysfs related fields,
 * @sysfs.irqs: irqs xarray contains irq indices which are used by the device,
 * @sysfs.lock: Synchronize irq sysfs creation,
 * @sysfs.irq_dir_exists: whether "irqs" directory exists,
 *
 * An auxiliary_device represents a part of its parent device's functionality.
 * It is given a name that, combined with the registering drivers
 * KBUILD_MODNAME, creates a match_name that is used for driver binding, and an
 * id that combined with the match_name provide a unique name to register with
 * the bus subsystem.  For example, a driver registering an auxiliary device is
 * named 'foo_mod.ko' and the subdevice is named 'foo_dev'.  The match name is
 * therefore 'foo_mod.foo_dev'.
 *
 * Registering an auxiliary_device is a three-step process.
 *
 * First, a 'struct auxiliary_device' needs to be defined or allocated for each
 * sub-device desired.  The name, id, dev.release, and dev.parent fields of
 * this structure must be filled in as follows.
 *
 * The 'name' field is to be given a name that is recognized by the auxiliary
 * driver.  If two auxiliary_devices with the same match_name, eg
 * "foo_mod.foo_dev", are registered onto the bus, they must have unique id
 * values (e.g. "x" and "y") so that the registered devices names are
 * "foo_mod.foo_dev.x" and "foo_mod.foo_dev.y".  If match_name + id are not
 * unique, then the device_add fails and generates an error message.
 *
 * The auxiliary_device.dev.type.release or auxiliary_device.dev.release must
 * be populated with a non-NULL pointer to successfully register the
 * auxiliary_device.  This release call is where resources associated with the
 * auxiliary device must be free'ed.  Because once the device is placed on the
 * bus the parent driver can not tell what other code may have a reference to
 * this data.
 *
 * The auxiliary_device.dev.parent should be set.  Typically to the registering
 * drivers device.
 *
 * Second, call auxiliary_device_init(), which checks several aspects of the
 * auxiliary_device struct and performs a device_initialize().  After this step
 * completes, any error state must have a call to auxiliary_device_uninit() in
 * its resolution path.
 *
 * The third and final step in registering an auxiliary_device is to perform a
 * call to auxiliary_device_add(), which sets the name of the device and adds
 * the device to the bus.
 *
 * .. code-block:: c
 *
 *      #define MY_DEVICE_NAME "foo_dev"
 *
 *      ...
 *
 *	struct auxiliary_device *my_aux_dev = my_aux_dev_alloc(xxx);
 *
 *	// Step 1:
 *	my_aux_dev->name = MY_DEVICE_NAME;
 *	my_aux_dev->id = my_unique_id_alloc(xxx);
 *	my_aux_dev->dev.release = my_aux_dev_release;
 *	my_aux_dev->dev.parent = my_dev;
 *
 *	// Step 2:
 *	if (auxiliary_device_init(my_aux_dev))
 *		goto fail;
 *
 *	// Step 3:
 *	if (auxiliary_device_add(my_aux_dev)) {
 *		auxiliary_device_uninit(my_aux_dev);
 *		goto fail;
 *	}
 *
 *	...
 *
 *
 * Unregistering an auxiliary_device is a two-step process to mirror the
 * register process.  First call auxiliary_device_delete(), then call
 * auxiliary_device_uninit().
 *
 * .. code-block:: c
 *
 *         auxiliary_device_delete(my_dev->my_aux_dev);
 *         auxiliary_device_uninit(my_dev->my_aux_dev);
 */
struct auxiliary_device {
	struct device dev;
	const char *name;
	u32 id;
	struct {
		struct xarray irqs;
		struct mutex lock; /* Synchronize irq sysfs creation */
		bool irq_dir_exists;
	} sysfs;
};

/**
 * struct auxiliary_driver - Definition of an auxiliary bus driver
 * @probe: Called when a matching device is added to the bus.
 * @remove: Called when device is removed from the bus.
 * @shutdown: Called at shut-down time to quiesce the device.
 * @suspend: Called to put the device to sleep mode. Usually to a power state.
 * @resume: Called to bring a device from sleep mode.
 * @name: Driver name.
 * @driver: Core driver structure.
 * @id_table: Table of devices this driver should match on the bus.
 *
 * Auxiliary drivers follow the standard driver model convention, where
 * discovery/enumeration is handled by the core, and drivers provide probe()
 * and remove() methods. They support power management and shutdown
 * notifications using the standard conventions.
 *
 * Auxiliary drivers register themselves with the bus by calling
 * auxiliary_driver_register(). The id_table contains the match_names of
 * auxiliary devices that a driver can bind with.
 *
 * .. code-block:: c
 *
 *         static const struct auxiliary_device_id my_auxiliary_id_table[] = {
 *		   { .name = "foo_mod.foo_dev" },
 *                 {},
 *         };
 *
 *         MODULE_DEVICE_TABLE(auxiliary, my_auxiliary_id_table);
 *
 *         struct auxiliary_driver my_drv = {
 *                 .name = "myauxiliarydrv",
 *                 .id_table = my_auxiliary_id_table,
 *                 .probe = my_drv_probe,
 *                 .remove = my_drv_remove
 *         };
 */
struct auxiliary_driver {
	int (*probe)(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id);
	void (*remove)(struct auxiliary_device *auxdev);
	void (*shutdown)(struct auxiliary_device *auxdev);
	int (*suspend)(struct auxiliary_device *auxdev, pm_message_t state);
	int (*resume)(struct auxiliary_device *auxdev);
	const char *name;
	struct device_driver driver;
	const struct auxiliary_device_id *id_table;
};

static inline void *auxiliary_get_drvdata(struct auxiliary_device *auxdev)
{
	return dev_get_drvdata(&auxdev->dev);
}

static inline void auxiliary_set_drvdata(struct auxiliary_device *auxdev, void *data)
{
	dev_set_drvdata(&auxdev->dev, data);
}

static inline struct auxiliary_device *to_auxiliary_dev(struct device *dev)
{
	return container_of(dev, struct auxiliary_device, dev);
}

static inline const struct auxiliary_driver *to_auxiliary_drv(const struct device_driver *drv)
{
	return container_of(drv, struct auxiliary_driver, driver);
}

int auxiliary_device_init(struct auxiliary_device *auxdev);
int __auxiliary_device_add(struct auxiliary_device *auxdev, const char *modname);
#define auxiliary_device_add(auxdev) __auxiliary_device_add(auxdev, KBUILD_MODNAME)

#ifdef CONFIG_SYSFS
int auxiliary_device_sysfs_irq_add(struct auxiliary_device *auxdev, int irq);
void auxiliary_device_sysfs_irq_remove(struct auxiliary_device *auxdev,
				       int irq);
#else /* CONFIG_SYSFS */
static inline int
auxiliary_device_sysfs_irq_add(struct auxiliary_device *auxdev, int irq)
{
	return 0;
}

static inline void
auxiliary_device_sysfs_irq_remove(struct auxiliary_device *auxdev, int irq) {}
#endif

static inline void auxiliary_device_uninit(struct auxiliary_device *auxdev)
{
	mutex_destroy(&auxdev->sysfs.lock);
	put_device(&auxdev->dev);
}

static inline void auxiliary_device_delete(struct auxiliary_device *auxdev)
{
	device_del(&auxdev->dev);
}

int __auxiliary_driver_register(struct auxiliary_driver *auxdrv, struct module *owner,
				const char *modname);
#define auxiliary_driver_register(auxdrv) \
	__auxiliary_driver_register(auxdrv, THIS_MODULE, KBUILD_MODNAME)

void auxiliary_driver_unregister(struct auxiliary_driver *auxdrv);

/**
 * module_auxiliary_driver() - Helper macro for registering an auxiliary driver
 * @__auxiliary_driver: auxiliary driver struct
 *
 * Helper macro for auxiliary drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 *
 * .. code-block:: c
 *
 *	module_auxiliary_driver(my_drv);
 */
#define module_auxiliary_driver(__auxiliary_driver) \
	module_driver(__auxiliary_driver, auxiliary_driver_register, auxiliary_driver_unregister)

#endif /* _AUXILIARY_BUS_H_ */
