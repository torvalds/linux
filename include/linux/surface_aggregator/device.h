/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Surface System Aggregator Module (SSAM) bus and client-device subsystem.
 *
 * Main interface for the surface-aggregator bus, surface-aggregator client
 * devices, and respective drivers building on top of the SSAM controller.
 * Provides support for non-platform/non-ACPI SSAM clients via dedicated
 * subsystem.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_AGGREGATOR_DEVICE_H
#define _LINUX_SURFACE_AGGREGATOR_DEVICE_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
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
	.target   = ((tid) != SSAM_ANY_TID) ? (tid) : 0,			\
	.instance = ((iid) != SSAM_ANY_IID) ? (iid) : 0,			\
	.function = ((fun) != SSAM_ANY_FUN) ? (fun) : 0				\

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

/**
 * struct ssam_device - SSAM client device.
 * @dev:  Driver model representation of the device.
 * @ctrl: SSAM controller managing this device.
 * @uid:  UID identifying the device.
 */
struct ssam_device {
	struct device dev;
	struct ssam_controller *ctrl;

	struct ssam_device_uid uid;
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

#endif /* _LINUX_SURFACE_AGGREGATOR_DEVICE_H */
