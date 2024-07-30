/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#ifndef _LINUX_BACKLIGHT_H
#define _LINUX_BACKLIGHT_H

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/types.h>

/**
 * enum backlight_update_reason - what method was used to update backlight
 *
 * A driver indicates the method (reason) used for updating the backlight
 * when calling backlight_force_update().
 */
enum backlight_update_reason {
	/**
	 * @BACKLIGHT_UPDATE_HOTKEY: The backlight was updated using a hot-key.
	 */
	BACKLIGHT_UPDATE_HOTKEY,

	/**
	 * @BACKLIGHT_UPDATE_SYSFS: The backlight was updated using sysfs.
	 */
	BACKLIGHT_UPDATE_SYSFS,
};

/**
 * enum backlight_type - the type of backlight control
 *
 * The type of interface used to control the backlight.
 */
enum backlight_type {
	/**
	 * @BACKLIGHT_RAW:
	 *
	 * The backlight is controlled using hardware registers.
	 */
	BACKLIGHT_RAW = 1,

	/**
	 * @BACKLIGHT_PLATFORM:
	 *
	 * The backlight is controlled using a platform-specific interface.
	 */
	BACKLIGHT_PLATFORM,

	/**
	 * @BACKLIGHT_FIRMWARE:
	 *
	 * The backlight is controlled using a standard firmware interface.
	 */
	BACKLIGHT_FIRMWARE,

	/**
	 * @BACKLIGHT_TYPE_MAX: Number of entries.
	 */
	BACKLIGHT_TYPE_MAX,
};

/**
 * enum backlight_notification - the type of notification
 *
 * The notifications that is used for notification sent to the receiver
 * that registered notifications using backlight_register_notifier().
 */
enum backlight_notification {
	/**
	 * @BACKLIGHT_REGISTERED: The backlight device is registered.
	 */
	BACKLIGHT_REGISTERED,

	/**
	 * @BACKLIGHT_UNREGISTERED: The backlight revice is unregistered.
	 */
	BACKLIGHT_UNREGISTERED,
};

/** enum backlight_scale - the type of scale used for brightness values
 *
 * The type of scale used for brightness values.
 */
enum backlight_scale {
	/**
	 * @BACKLIGHT_SCALE_UNKNOWN: The scale is unknown.
	 */
	BACKLIGHT_SCALE_UNKNOWN = 0,

	/**
	 * @BACKLIGHT_SCALE_LINEAR: The scale is linear.
	 *
	 * The linear scale will increase brightness the same for each step.
	 */
	BACKLIGHT_SCALE_LINEAR,

	/**
	 * @BACKLIGHT_SCALE_NON_LINEAR: The scale is not linear.
	 *
	 * This is often used when the brightness values tries to adjust to
	 * the relative perception of the eye demanding a non-linear scale.
	 */
	BACKLIGHT_SCALE_NON_LINEAR,
};

struct backlight_device;

/**
 * struct backlight_ops - backlight operations
 *
 * The backlight operations are specified when the backlight device is registered.
 */
struct backlight_ops {
	/**
	 * @options: Configure how operations are called from the core.
	 *
	 * The options parameter is used to adjust the behaviour of the core.
	 * Set BL_CORE_SUSPENDRESUME to get the update_status() operation called
	 * upon suspend and resume.
	 */
	unsigned int options;

#define BL_CORE_SUSPENDRESUME	(1 << 0)

	/**
	 * @update_status: Operation called when properties have changed.
	 *
	 * Notify the backlight driver some property has changed.
	 * The update_status operation is protected by the update_lock.
	 *
	 * The backlight driver is expected to use backlight_is_blank()
	 * to check if the display is blanked and set brightness accordingly.
	 * update_status() is called when any of the properties has changed.
	 *
	 * RETURNS:
	 *
	 * 0 on success, negative error code if any failure occurred.
	 */
	int (*update_status)(struct backlight_device *);

	/**
	 * @get_brightness: Return the current backlight brightness.
	 *
	 * The driver may implement this as a readback from the HW.
	 * This operation is optional and if not present then the current
	 * brightness property value is used.
	 *
	 * RETURNS:
	 *
	 * A brightness value which is 0 or a positive number.
	 * On failure a negative error code is returned.
	 */
	int (*get_brightness)(struct backlight_device *);

	/**
	 * @controls_device: Check against the display device
	 *
	 * Check if the backlight controls the given display device. This
	 * operation is optional and if not implemented it is assumed that
	 * the display is always the one controlled by the backlight.
	 *
	 * RETURNS:
	 *
	 * If display_dev is NULL or display_dev matches the device controlled by
	 * the backlight, return true. Otherwise return false.
	 */
	bool (*controls_device)(struct backlight_device *bd, struct device *display_dev);
};

/**
 * struct backlight_properties - backlight properties
 *
 * This structure defines all the properties of a backlight.
 */
struct backlight_properties {
	/**
	 * @brightness: The current brightness requested by the user.
	 *
	 * The backlight core makes sure the range is (0 to max_brightness)
	 * when the brightness is set via the sysfs attribute:
	 * /sys/class/backlight/<backlight>/brightness.
	 *
	 * This value can be set in the backlight_properties passed
	 * to devm_backlight_device_register() to set a default brightness
	 * value.
	 */
	int brightness;

	/**
	 * @max_brightness: The maximum brightness value.
	 *
	 * This value must be set in the backlight_properties passed to
	 * devm_backlight_device_register() and shall not be modified by the
	 * driver after registration.
	 */
	int max_brightness;

	/**
	 * @power: The current power mode.
	 *
	 * User space can configure the power mode using the sysfs
	 * attribute: /sys/class/backlight/<backlight>/bl_power
	 * When the power property is updated update_status() is called.
	 *
	 * The possible values are: (0: full on, 4: full off), see
	 * BACKLIGHT_POWER constants.
	 *
	 * When the backlight device is enabled, @power is set to
	 * BACKLIGHT_POWER_ON. When the backlight device is disabled,
	 * @power is set to BACKLIGHT_POWER_OFF.
	 */
	int power;

#define BACKLIGHT_POWER_ON		(0)
#define BACKLIGHT_POWER_OFF		(4)
#define BACKLIGHT_POWER_REDUCED		(1) // deprecated; don't use in new code

	/**
	 * @type: The type of backlight supported.
	 *
	 * The backlight type allows userspace to make appropriate
	 * policy decisions based on the backlight type.
	 *
	 * This value must be set in the backlight_properties
	 * passed to devm_backlight_device_register().
	 */
	enum backlight_type type;

	/**
	 * @state: The state of the backlight core.
	 *
	 * The state is a bitmask. BL_CORE_FBBLANK is set when the display
	 * is expected to be blank. BL_CORE_SUSPENDED is set when the
	 * driver is suspended.
	 *
	 * backlight drivers are expected to use backlight_is_blank()
	 * in their update_status() operation rather than reading the
	 * state property.
	 *
	 * The state is maintained by the core and drivers may not modify it.
	 */
	unsigned int state;

#define BL_CORE_SUSPENDED	(1 << 0)	/* backlight is suspended */
#define BL_CORE_FBBLANK		(1 << 1)	/* backlight is under an fb blank event */

	/**
	 * @scale: The type of the brightness scale.
	 */
	enum backlight_scale scale;
};

/**
 * struct backlight_device - backlight device data
 *
 * This structure holds all data required by a backlight device.
 */
struct backlight_device {
	/**
	 * @props: Backlight properties
	 */
	struct backlight_properties props;

	/**
	 * @update_lock: The lock used when calling the update_status() operation.
	 *
	 * update_lock is an internal backlight lock that serialise access
	 * to the update_status() operation. The backlight core holds the update_lock
	 * when calling the update_status() operation. The update_lock shall not
	 * be used by backlight drivers.
	 */
	struct mutex update_lock;

	/**
	 * @ops_lock: The lock used around everything related to backlight_ops.
	 *
	 * ops_lock is an internal backlight lock that protects the ops pointer
	 * and is used around all accesses to ops and when the operations are
	 * invoked. The ops_lock shall not be used by backlight drivers.
	 */
	struct mutex ops_lock;

	/**
	 * @ops: Pointer to the backlight operations.
	 *
	 * If ops is NULL, the driver that registered this device has been unloaded,
	 * and if class_get_devdata() points to something in the body of that driver,
	 * it is also invalid.
	 */
	const struct backlight_ops *ops;

	/**
	 * @fb_notif: The framebuffer notifier block
	 */
	struct notifier_block fb_notif;

	/**
	 * @entry: List entry of all registered backlight devices
	 */
	struct list_head entry;

	/**
	 * @dev: Parent device.
	 */
	struct device dev;

	/**
	 * @fb_bl_on: The state of individual fbdev's.
	 *
	 * Multiple fbdev's may share one backlight device. The fb_bl_on
	 * records the state of the individual fbdev.
	 */
	bool fb_bl_on[FB_MAX];

	/**
	 * @use_count: The number of uses of fb_bl_on.
	 */
	int use_count;
};

/**
 * backlight_update_status - force an update of the backlight device status
 * @bd: the backlight device
 */
static inline int backlight_update_status(struct backlight_device *bd)
{
	int ret = -ENOENT;

	mutex_lock(&bd->update_lock);
	if (bd->ops && bd->ops->update_status)
		ret = bd->ops->update_status(bd);
	mutex_unlock(&bd->update_lock);

	return ret;
}

/**
 * backlight_enable - Enable backlight
 * @bd: the backlight device to enable
 */
static inline int backlight_enable(struct backlight_device *bd)
{
	if (!bd)
		return 0;

	bd->props.power = BACKLIGHT_POWER_ON;
	bd->props.state &= ~BL_CORE_FBBLANK;

	return backlight_update_status(bd);
}

/**
 * backlight_disable - Disable backlight
 * @bd: the backlight device to disable
 */
static inline int backlight_disable(struct backlight_device *bd)
{
	if (!bd)
		return 0;

	bd->props.power = BACKLIGHT_POWER_OFF;
	bd->props.state |= BL_CORE_FBBLANK;

	return backlight_update_status(bd);
}

/**
 * backlight_is_blank - Return true if display is expected to be blank
 * @bd: the backlight device
 *
 * Display is expected to be blank if any of these is true::
 *
 *   1) if power in not UNBLANK
 *   2) if state indicate BLANK or SUSPENDED
 *
 * Returns true if display is expected to be blank, false otherwise.
 */
static inline bool backlight_is_blank(const struct backlight_device *bd)
{
	return bd->props.power != BACKLIGHT_POWER_ON ||
	       bd->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK);
}

/**
 * backlight_get_brightness - Returns the current brightness value
 * @bd: the backlight device
 *
 * Returns the current brightness value, taking in consideration the current
 * state. If backlight_is_blank() returns true then return 0 as brightness
 * otherwise return the current brightness property value.
 *
 * Backlight drivers are expected to use this function in their update_status()
 * operation to get the brightness value.
 */
static inline int backlight_get_brightness(const struct backlight_device *bd)
{
	if (backlight_is_blank(bd))
		return 0;
	else
		return bd->props.brightness;
}

struct backlight_device *
backlight_device_register(const char *name, struct device *dev, void *devdata,
			  const struct backlight_ops *ops,
			  const struct backlight_properties *props);
struct backlight_device *
devm_backlight_device_register(struct device *dev, const char *name,
			       struct device *parent, void *devdata,
			       const struct backlight_ops *ops,
			       const struct backlight_properties *props);
void backlight_device_unregister(struct backlight_device *bd);
void devm_backlight_device_unregister(struct device *dev,
				      struct backlight_device *bd);
void backlight_force_update(struct backlight_device *bd,
			    enum backlight_update_reason reason);
int backlight_register_notifier(struct notifier_block *nb);
int backlight_unregister_notifier(struct notifier_block *nb);
struct backlight_device *backlight_device_get_by_name(const char *name);
struct backlight_device *backlight_device_get_by_type(enum backlight_type type);
int backlight_device_set_brightness(struct backlight_device *bd,
				    unsigned long brightness);

#define to_backlight_device(obj) container_of(obj, struct backlight_device, dev)

/**
 * bl_get_data - access devdata
 * @bl_dev: pointer to backlight device
 *
 * When a backlight device is registered the driver has the possibility
 * to supply a void * devdata. bl_get_data() return a pointer to the
 * devdata.
 *
 * RETURNS:
 *
 * pointer to devdata stored while registering the backlight device.
 */
static inline void * bl_get_data(struct backlight_device *bl_dev)
{
	return dev_get_drvdata(&bl_dev->dev);
}

#ifdef CONFIG_OF
struct backlight_device *of_find_backlight_by_node(struct device_node *node);
#else
static inline struct backlight_device *
of_find_backlight_by_node(struct device_node *node)
{
	return NULL;
}
#endif

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE)
struct backlight_device *devm_of_find_backlight(struct device *dev);
#else
static inline struct backlight_device *
devm_of_find_backlight(struct device *dev)
{
	return NULL;
}
#endif

#endif
