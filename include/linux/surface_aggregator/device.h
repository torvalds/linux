/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Surface System Aggregator Module (SSAM) bus and client-device subsystem.
 *
 * Main interface for the surface-aggregator bus, surface-aggregator client
 * devices, and respective drivers building on top of the SSAM controller.
 * Provides support for non-platform/non-ACPI SSAM clients via dedicated
 * subsystem.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_AGGREGATOR_DEVICE_H
#define _LINUX_SURFACE_AGGREGATOR_DEVICE_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/types.h>

#include <linux/surface_aggregator/controller.h>


/* -- Surface System Aggregator Module bus. --------------------------------- */

/**
 * enum ssam_device_domain - SAM device domain.
 * @SSAM_DOMAIN_VIRTUAL:   Virtual device.
 * @SSAM_DOMAIN_SERIALHUB: Physical device connected via Surface Serial Hub.
 */
enum ssam_device_domain {
	SSAM_DOMAIN_VIRTUAL   = 0x00,
	SSAM_DOMAIN_SERIALHUB = 0x01,
};

/**
 * enum ssam_virtual_tc - Target categories for the virtual SAM domain.
 * @SSAM_VIRTUAL_TC_HUB: Device hub category.
 */
enum ssam_virtual_tc {
	SSAM_VIRTUAL_TC_HUB = 0x00,
};

/**
 * struct ssam_device_uid - Unique identifier for SSAM device.
 * @domain:   Domain of the device.
 * @category: Target category of the device.
 * @target:   Target ID of the device.
 * @instance: Instance ID of the device.
 * @function: Sub-function of the device. This field can be used to split a
 *            single SAM device into multiple virtual subdevices to separate
 *            different functionality of that device and allow one driver per
 *            such functionality.
 */
struct ssam_device_uid {
	u8 domain;
	u8 category;
	u8 target;
	u8 instance;
	u8 function;
};

/*
 * Special values for device matching.
 *
 * These values are intended to be used with SSAM_DEVICE(), SSAM_VDEV(), and
 * SSAM_SDEV() exclusively. Specifically, they are used to initialize the
 * match_flags member of the device ID structure. Do not use them directly
 * with struct ssam_device_id or struct ssam_device_uid.
 */
#define SSAM_ANY_TID		0xffff
#define SSAM_ANY_IID		0xffff
#define SSAM_ANY_FUN		0xffff

/**
 * SSAM_DEVICE() - Initialize a &struct ssam_device_id with the given
 * parameters.
 * @d:   Domain of the device.
 * @cat: Target category of the device.
 * @tid: Target ID of the device.
 * @iid: Instance ID of the device.
 * @fun: Sub-function of the device.
 *
 * Initializes a &struct ssam_device_id with the given parameters. See &struct
 * ssam_device_uid for details regarding the parameters. The special values
 * %SSAM_ANY_TID, %SSAM_ANY_IID, and %SSAM_ANY_FUN can be used to specify that
 * matching should ignore target ID, instance ID, and/or sub-function,
 * respectively. This macro initializes the ``match_flags`` field based on the
 * given parameters.
 *
 * Note: The parameters @d and @cat must be valid &u8 values, the parameters
 * @tid, @iid, and @fun must be either valid &u8 values or %SSAM_ANY_TID,
 * %SSAM_ANY_IID, or %SSAM_ANY_FUN, respectively. Other non-&u8 values are not
 * allowed.
 */
#define SSAM_DEVICE(d, cat, tid, iid, fun)					\
	.match_flags = (((tid) != SSAM_ANY_TID) ? SSAM_MATCH_TARGET : 0)	\
		     | (((iid) != SSAM_ANY_IID) ? SSAM_MATCH_INSTANCE : 0)	\
		     | (((fun) != SSAM_ANY_FUN) ? SSAM_MATCH_FUNCTION : 0),	\
	.domain   = d,								\
	.category = cat,							\
	.target   = __builtin_choose_expr((tid) != SSAM_ANY_TID, (tid), 0),	\
	.instance = __builtin_choose_expr((iid) != SSAM_ANY_IID, (iid), 0),	\
	.function = __builtin_choose_expr((fun) != SSAM_ANY_FUN, (fun), 0)

/**
 * SSAM_VDEV() - Initialize a &struct ssam_device_id as virtual device with
 * the given parameters.
 * @cat: Target category of the device.
 * @tid: Target ID of the device.
 * @iid: Instance ID of the device.
 * @fun: Sub-function of the device.
 *
 * Initializes a &struct ssam_device_id with the given parameters in the
 * virtual domain. See &struct ssam_device_uid for details regarding the
 * parameters. The special values %SSAM_ANY_TID, %SSAM_ANY_IID, and
 * %SSAM_ANY_FUN can be used to specify that matching should ignore target ID,
 * instance ID, and/or sub-function, respectively. This macro initializes the
 * ``match_flags`` field based on the given parameters.
 *
 * Note: The parameter @cat must be a valid &u8 value, the parameters @tid,
 * @iid, and @fun must be either valid &u8 values or %SSAM_ANY_TID,
 * %SSAM_ANY_IID, or %SSAM_ANY_FUN, respectively. Other non-&u8 values are not
 * allowed.
 */
#define SSAM_VDEV(cat, tid, iid, fun) \
	SSAM_DEVICE(SSAM_DOMAIN_VIRTUAL, SSAM_VIRTUAL_TC_##cat, tid, iid, fun)

/**
 * SSAM_SDEV() - Initialize a &struct ssam_device_id as physical SSH device
 * with the given parameters.
 * @cat: Target category of the device.
 * @tid: Target ID of the device.
 * @iid: Instance ID of the device.
 * @fun: Sub-function of the device.
 *
 * Initializes a &struct ssam_device_id with the given parameters in the SSH
 * domain. See &struct ssam_device_uid for details regarding the parameters.
 * The special values %SSAM_ANY_TID, %SSAM_ANY_IID, and %SSAM_ANY_FUN can be
 * used to specify that matching should ignore target ID, instance ID, and/or
 * sub-function, respectively. This macro initializes the ``match_flags``
 * field based on the given parameters.
 *
 * Note: The parameter @cat must be a valid &u8 value, the parameters @tid,
 * @iid, and @fun must be either valid &u8 values or %SSAM_ANY_TID,
 * %SSAM_ANY_IID, or %SSAM_ANY_FUN, respectively. Other non-&u8 values are not
 * allowed.
 */
#define SSAM_SDEV(cat, tid, iid, fun) \
	SSAM_DEVICE(SSAM_DOMAIN_SERIALHUB, SSAM_SSH_TC_##cat, tid, iid, fun)

/*
 * enum ssam_device_flags - Flags for SSAM client devices.
 * @SSAM_DEVICE_HOT_REMOVED_BIT:
 *	The device has been hot-removed. Further communication with it may time
 *	out and should be avoided.
 */
enum ssam_device_flags {
	SSAM_DEVICE_HOT_REMOVED_BIT = 0,
};

/**
 * struct ssam_device - SSAM client device.
 * @dev:   Driver model representation of the device.
 * @ctrl:  SSAM controller managing this device.
 * @uid:   UID identifying the device.
 * @flags: Device state flags, see &enum ssam_device_flags.
 */
struct ssam_device {
	struct device dev;
	struct ssam_controller *ctrl;

	struct ssam_device_uid uid;

	unsigned long flags;
};

/**
 * struct ssam_device_driver - SSAM client device driver.
 * @driver:      Base driver model structure.
 * @match_table: Match table specifying which devices the driver should bind to.
 * @probe:       Called when the driver is being bound to a device.
 * @remove:      Called when the driver is being unbound from the device.
 */
struct ssam_device_driver {
	struct device_driver driver;

	const struct ssam_device_id *match_table;

	int  (*probe)(struct ssam_device *sdev);
	void (*remove)(struct ssam_device *sdev);
};

#ifdef CONFIG_SURFACE_AGGREGATOR_BUS

extern struct bus_type ssam_bus_type;
extern const struct device_type ssam_device_type;

/**
 * is_ssam_device() - Check if the given device is a SSAM client device.
 * @d: The device to test the type of.
 *
 * Return: Returns %true if the specified device is of type &struct
 * ssam_device, i.e. the device type points to %ssam_device_type, and %false
 * otherwise.
 */
static inline bool is_ssam_device(struct device *d)
{
	return d->type == &ssam_device_type;
}

#else /* CONFIG_SURFACE_AGGREGATOR_BUS */

static inline bool is_ssam_device(struct device *d)
{
	return false;
}

#endif /* CONFIG_SURFACE_AGGREGATOR_BUS */

/**
 * to_ssam_device() - Casts the given device to a SSAM client device.
 * @d: The device to cast.
 *
 * Casts the given &struct device to a &struct ssam_device. The caller has to
 * ensure that the given device is actually enclosed in a &struct ssam_device,
 * e.g. by calling is_ssam_device().
 *
 * Return: Returns a pointer to the &struct ssam_device wrapping the given
 * device @d.
 */
static inline struct ssam_device *to_ssam_device(struct device *d)
{
	return container_of(d, struct ssam_device, dev);
}

/**
 * to_ssam_device_driver() - Casts the given device driver to a SSAM client
 * device driver.
 * @d: The driver to cast.
 *
 * Casts the given &struct device_driver to a &struct ssam_device_driver. The
 * caller has to ensure that the given driver is actually enclosed in a
 * &struct ssam_device_driver.
 *
 * Return: Returns the pointer to the &struct ssam_device_driver wrapping the
 * given device driver @d.
 */
static inline
struct ssam_device_driver *to_ssam_device_driver(struct device_driver *d)
{
	return container_of(d, struct ssam_device_driver, driver);
}

const struct ssam_device_id *ssam_device_id_match(const struct ssam_device_id *table,
						  const struct ssam_device_uid uid);

const struct ssam_device_id *ssam_device_get_match(const struct ssam_device *dev);

const void *ssam_device_get_match_data(const struct ssam_device *dev);

struct ssam_device *ssam_device_alloc(struct ssam_controller *ctrl,
				      struct ssam_device_uid uid);

int ssam_device_add(struct ssam_device *sdev);
void ssam_device_remove(struct ssam_device *sdev);

/**
 * ssam_device_mark_hot_removed() - Mark the given device as hot-removed.
 * @sdev: The device to mark as hot-removed.
 *
 * Mark the device as having been hot-removed. This signals drivers using the
 * device that communication with the device should be avoided and may lead to
 * timeouts.
 */
static inline void ssam_device_mark_hot_removed(struct ssam_device *sdev)
{
	dev_dbg(&sdev->dev, "marking device as hot-removed\n");
	set_bit(SSAM_DEVICE_HOT_REMOVED_BIT, &sdev->flags);
}

/**
 * ssam_device_is_hot_removed() - Check if the given device has been
 * hot-removed.
 * @sdev: The device to check.
 *
 * Checks if the given device has been marked as hot-removed. See
 * ssam_device_mark_hot_removed() for more details.
 *
 * Return: Returns ``true`` if the device has been marked as hot-removed.
 */
static inline bool ssam_device_is_hot_removed(struct ssam_device *sdev)
{
	return test_bit(SSAM_DEVICE_HOT_REMOVED_BIT, &sdev->flags);
}

/**
 * ssam_device_get() - Increment reference count of SSAM client device.
 * @sdev: The device to increment the reference count of.
 *
 * Increments the reference count of the given SSAM client device by
 * incrementing the reference count of the enclosed &struct device via
 * get_device().
 *
 * See ssam_device_put() for the counter-part of this function.
 *
 * Return: Returns the device provided as input.
 */
static inline struct ssam_device *ssam_device_get(struct ssam_device *sdev)
{
	return sdev ? to_ssam_device(get_device(&sdev->dev)) : NULL;
}

/**
 * ssam_device_put() - Decrement reference count of SSAM client device.
 * @sdev: The device to decrement the reference count of.
 *
 * Decrements the reference count of the given SSAM client device by
 * decrementing the reference count of the enclosed &struct device via
 * put_device().
 *
 * See ssam_device_get() for the counter-part of this function.
 */
static inline void ssam_device_put(struct ssam_device *sdev)
{
	if (sdev)
		put_device(&sdev->dev);
}

/**
 * ssam_device_get_drvdata() - Get driver-data of SSAM client device.
 * @sdev: The device to get the driver-data from.
 *
 * Return: Returns the driver-data of the given device, previously set via
 * ssam_device_set_drvdata().
 */
static inline void *ssam_device_get_drvdata(struct ssam_device *sdev)
{
	return dev_get_drvdata(&sdev->dev);
}

/**
 * ssam_device_set_drvdata() - Set driver-data of SSAM client device.
 * @sdev: The device to set the driver-data of.
 * @data: The data to set the device's driver-data pointer to.
 */
static inline void ssam_device_set_drvdata(struct ssam_device *sdev, void *data)
{
	dev_set_drvdata(&sdev->dev, data);
}

int __ssam_device_driver_register(struct ssam_device_driver *d, struct module *o);
void ssam_device_driver_unregister(struct ssam_device_driver *d);

/**
 * ssam_device_driver_register() - Register a SSAM client device driver.
 * @drv: The driver to register.
 */
#define ssam_device_driver_register(drv) \
	__ssam_device_driver_register(drv, THIS_MODULE)

/**
 * module_ssam_device_driver() - Helper macro for SSAM device driver
 * registration.
 * @drv: The driver managed by this module.
 *
 * Helper macro to register a SSAM device driver via module_init() and
 * module_exit(). This macro may only be used once per module and replaces the
 * aforementioned definitions.
 */
#define module_ssam_device_driver(drv)			\
	module_driver(drv, ssam_device_driver_register,	\
		      ssam_device_driver_unregister)


/* -- Helpers for controller and hub devices. ------------------------------- */

#ifdef CONFIG_SURFACE_AGGREGATOR_BUS

int __ssam_register_clients(struct device *parent, struct ssam_controller *ctrl,
			    struct fwnode_handle *node);
void ssam_remove_clients(struct device *dev);

#else /* CONFIG_SURFACE_AGGREGATOR_BUS */

static inline int __ssam_register_clients(struct device *parent, struct ssam_controller *ctrl,
					  struct fwnode_handle *node)
{
	return 0;
}

static inline void ssam_remove_clients(struct device *dev) {}

#endif /* CONFIG_SURFACE_AGGREGATOR_BUS */

/**
 * ssam_register_clients() - Register all client devices defined under the
 * given parent device.
 * @dev: The parent device under which clients should be registered.
 * @ctrl: The controller with which client should be registered.
 *
 * Register all clients that have via firmware nodes been defined as children
 * of the given (parent) device. The respective child firmware nodes will be
 * associated with the correspondingly created child devices.
 *
 * The given controller will be used to instantiate the new devices. See
 * ssam_device_add() for details.
 *
 * Return: Returns zero on success, nonzero on failure.
 */
static inline int ssam_register_clients(struct device *dev, struct ssam_controller *ctrl)
{
	return __ssam_register_clients(dev, ctrl, dev_fwnode(dev));
}

/**
 * ssam_device_register_clients() - Register all client devices defined under
 * the given SSAM parent device.
 * @sdev: The parent device under which clients should be registered.
 *
 * Register all clients that have via firmware nodes been defined as children
 * of the given (parent) device. The respective child firmware nodes will be
 * associated with the correspondingly created child devices.
 *
 * The controller used by the parent device will be used to instantiate the new
 * devices. See ssam_device_add() for details.
 *
 * Return: Returns zero on success, nonzero on failure.
 */
static inline int ssam_device_register_clients(struct ssam_device *sdev)
{
	return ssam_register_clients(&sdev->dev, sdev->ctrl);
}


/* -- Helpers for client-device requests. ----------------------------------- */

/**
 * SSAM_DEFINE_SYNC_REQUEST_CL_N() - Define synchronous client-device SAM
 * request function with neither argument nor return value.
 * @name: Name of the generated function.
 * @spec: Specification (&struct ssam_request_spec_md) defining the request.
 *
 * Defines a function executing the synchronous SAM request specified by
 * @spec, with the request having neither argument nor return value. Device
 * specifying parameters are not hard-coded, but instead are provided via the
 * client device, specifically its UID, supplied when calling this function.
 * The generated function takes care of setting up the request struct, buffer
 * allocation, as well as execution of the request itself, returning once the
 * request has been fully completed. The required transport buffer will be
 * allocated on the stack.
 *
 * The generated function is defined as ``static int name(struct ssam_device
 * *sdev)``, returning the status of the request, which is zero on success and
 * negative on failure. The ``sdev`` parameter specifies both the target
 * device of the request and by association the controller via which the
 * request is sent.
 *
 * Refer to ssam_request_sync_onstack() for more details on the behavior of
 * the generated function.
 */
#define SSAM_DEFINE_SYNC_REQUEST_CL_N(name, spec...)			\
	SSAM_DEFINE_SYNC_REQUEST_MD_N(__raw_##name, spec)		\
	static int name(struct ssam_device *sdev)			\
	{								\
		return __raw_##name(sdev->ctrl, sdev->uid.target,	\
				    sdev->uid.instance);		\
	}

/**
 * SSAM_DEFINE_SYNC_REQUEST_CL_W() - Define synchronous client-device SAM
 * request function with argument.
 * @name:  Name of the generated function.
 * @atype: Type of the request's argument.
 * @spec:  Specification (&struct ssam_request_spec_md) defining the request.
 *
 * Defines a function executing the synchronous SAM request specified by
 * @spec, with the request taking an argument of type @atype and having no
 * return value. Device specifying parameters are not hard-coded, but instead
 * are provided via the client device, specifically its UID, supplied when
 * calling this function. The generated function takes care of setting up the
 * request struct, buffer allocation, as well as execution of the request
 * itself, returning once the request has been fully completed. The required
 * transport buffer will be allocated on the stack.
 *
 * The generated function is defined as ``static int name(struct ssam_device
 * *sdev, const atype *arg)``, returning the status of the request, which is
 * zero on success and negative on failure. The ``sdev`` parameter specifies
 * both the target device of the request and by association the controller via
 * which the request is sent. The request's argument is specified via the
 * ``arg`` pointer.
 *
 * Refer to ssam_request_sync_onstack() for more details on the behavior of
 * the generated function.
 */
#define SSAM_DEFINE_SYNC_REQUEST_CL_W(name, atype, spec...)		\
	SSAM_DEFINE_SYNC_REQUEST_MD_W(__raw_##name, atype, spec)	\
	static int name(struct ssam_device *sdev, const atype *arg)	\
	{								\
		return __raw_##name(sdev->ctrl, sdev->uid.target,	\
				    sdev->uid.instance, arg);		\
	}

/**
 * SSAM_DEFINE_SYNC_REQUEST_CL_R() - Define synchronous client-device SAM
 * request function with return value.
 * @name:  Name of the generated function.
 * @rtype: Type of the request's return value.
 * @spec:  Specification (&struct ssam_request_spec_md) defining the request.
 *
 * Defines a function executing the synchronous SAM request specified by
 * @spec, with the request taking no argument but having a return value of
 * type @rtype. Device specifying parameters are not hard-coded, but instead
 * are provided via the client device, specifically its UID, supplied when
 * calling this function. The generated function takes care of setting up the
 * request struct, buffer allocation, as well as execution of the request
 * itself, returning once the request has been fully completed. The required
 * transport buffer will be allocated on the stack.
 *
 * The generated function is defined as ``static int name(struct ssam_device
 * *sdev, rtype *ret)``, returning the status of the request, which is zero on
 * success and negative on failure. The ``sdev`` parameter specifies both the
 * target device of the request and by association the controller via which
 * the request is sent. The request's return value is written to the memory
 * pointed to by the ``ret`` parameter.
 *
 * Refer to ssam_request_sync_onstack() for more details on the behavior of
 * the generated function.
 */
#define SSAM_DEFINE_SYNC_REQUEST_CL_R(name, rtype, spec...)		\
	SSAM_DEFINE_SYNC_REQUEST_MD_R(__raw_##name, rtype, spec)	\
	static int name(struct ssam_device *sdev, rtype *ret)		\
	{								\
		return __raw_##name(sdev->ctrl, sdev->uid.target,	\
				    sdev->uid.instance, ret);		\
	}

/**
 * SSAM_DEFINE_SYNC_REQUEST_CL_WR() - Define synchronous client-device SAM
 * request function with argument and return value.
 * @name:  Name of the generated function.
 * @atype: Type of the request's argument.
 * @rtype: Type of the request's return value.
 * @spec:  Specification (&struct ssam_request_spec_md) defining the request.
 *
 * Defines a function executing the synchronous SAM request specified by @spec,
 * with the request taking an argument of type @atype and having a return value
 * of type @rtype. Device specifying parameters are not hard-coded, but instead
 * are provided via the client device, specifically its UID, supplied when
 * calling this function. The generated function takes care of setting up the
 * request struct, buffer allocation, as well as execution of the request
 * itself, returning once the request has been fully completed. The required
 * transport buffer will be allocated on the stack.
 *
 * The generated function is defined as ``static int name(struct ssam_device
 * *sdev, const atype *arg, rtype *ret)``, returning the status of the request,
 * which is zero on success and negative on failure. The ``sdev`` parameter
 * specifies both the target device of the request and by association the
 * controller via which the request is sent. The request's argument is
 * specified via the ``arg`` pointer. The request's return value is written to
 * the memory pointed to by the ``ret`` parameter.
 *
 * Refer to ssam_request_sync_onstack() for more details on the behavior of
 * the generated function.
 */
#define SSAM_DEFINE_SYNC_REQUEST_CL_WR(name, atype, rtype, spec...)		\
	SSAM_DEFINE_SYNC_REQUEST_MD_WR(__raw_##name, atype, rtype, spec)	\
	static int name(struct ssam_device *sdev, const atype *arg, rtype *ret)	\
	{									\
		return __raw_##name(sdev->ctrl, sdev->uid.target,		\
				    sdev->uid.instance, arg, ret);		\
	}


/* -- Helpers for client-device notifiers. ---------------------------------- */

/**
 * ssam_device_notifier_register() - Register an event notifier for the
 * specified client device.
 * @sdev: The device the notifier should be registered on.
 * @n:    The event notifier to register.
 *
 * Register an event notifier. Increment the usage counter of the associated
 * SAM event if the notifier is not marked as an observer. If the event is not
 * marked as an observer and is currently not enabled, it will be enabled
 * during this call. If the notifier is marked as an observer, no attempt will
 * be made at enabling any event and no reference count will be modified.
 *
 * Notifiers marked as observers do not need to be associated with one specific
 * event, i.e. as long as no event matching is performed, only the event target
 * category needs to be set.
 *
 * Return: Returns zero on success, %-ENOSPC if there have already been
 * %INT_MAX notifiers for the event ID/type associated with the notifier block
 * registered, %-ENOMEM if the corresponding event entry could not be
 * allocated, %-ENODEV if the device is marked as hot-removed. If this is the
 * first time that a notifier block is registered for the specific associated
 * event, returns the status of the event-enable EC-command.
 */
static inline int ssam_device_notifier_register(struct ssam_device *sdev,
						struct ssam_event_notifier *n)
{
	/*
	 * Note that this check does not provide any guarantees whatsoever as
	 * hot-removal could happen at any point and we can't protect against
	 * it. Nevertheless, if we can detect hot-removal, bail early to avoid
	 * communication timeouts.
	 */
	if (ssam_device_is_hot_removed(sdev))
		return -ENODEV;

	return ssam_notifier_register(sdev->ctrl, n);
}

/**
 * ssam_device_notifier_unregister() - Unregister an event notifier for the
 * specified client device.
 * @sdev: The device the notifier has been registered on.
 * @n:    The event notifier to unregister.
 *
 * Unregister an event notifier. Decrement the usage counter of the associated
 * SAM event if the notifier is not marked as an observer. If the usage counter
 * reaches zero, the event will be disabled.
 *
 * In case the device has been marked as hot-removed, the event will not be
 * disabled on the EC, as in those cases any attempt at doing so may time out.
 *
 * Return: Returns zero on success, %-ENOENT if the given notifier block has
 * not been registered on the controller. If the given notifier block was the
 * last one associated with its specific event, returns the status of the
 * event-disable EC-command.
 */
static inline int ssam_device_notifier_unregister(struct ssam_device *sdev,
						  struct ssam_event_notifier *n)
{
	return __ssam_notifier_unregister(sdev->ctrl, n,
					  !ssam_device_is_hot_removed(sdev));
}

#endif /* _LINUX_SURFACE_AGGREGATOR_DEVICE_H */
