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

/* Notes on locking:
 *
 * backlight_device->ops_lock is an internal backlight lock protecting the
 * ops pointer and no code outside the core should need to touch it.
 *
 * Access to update_status() is serialised by the update_lock mutex since
 * most drivers seem to need this and historically get it wrong.
 *
 * Most drivers don't need locking on their get_brightness() method.
 * If yours does, you need to implement it in the driver. You can use the
 * update_lock mutex if appropriate.
 *
 * Any other use of the locks below is probably wrong.
 */

enum backlight_update_reason {
	BACKLIGHT_UPDATE_HOTKEY,
	BACKLIGHT_UPDATE_SYSFS,
};

enum backlight_type {
	BACKLIGHT_RAW = 1,
	BACKLIGHT_PLATFORM,
	BACKLIGHT_FIRMWARE,
	BACKLIGHT_TYPE_MAX,
};

enum backlight_notification {
	BACKLIGHT_REGISTERED,
	BACKLIGHT_UNREGISTERED,
};

enum backlight_scale {
	BACKLIGHT_SCALE_UNKNOWN = 0,
	BACKLIGHT_SCALE_LINEAR,
	BACKLIGHT_SCALE_NON_LINEAR,
};

struct backlight_device;
struct fb_info;

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
	 * @check_fb: Check the framebuffer device.
	 *
	 * Check if given framebuffer device is the one bound to this backlight.
	 * This operation is optional and if not implemented it is assumed that the
	 * fbdev is always the one bound to the backlight.
	 *
	 * RETURNS:
	 *
	 * If info is NULL or the info matches the fbdev bound to the backlight return true.
	 * If info does not match the fbdev bound to the backlight return false.
	 */
	int (*check_fb)(struct backlight_device *bd, struct fb_info *info);
};

/* This structure defines all the properties of a backlight */
struct backlight_properties {
	/* Current User requested brightness (0 - max_brightness) */
	int brightness;
	/* Maximal value for brightness (read-only) */
	int max_brightness;
	/* Current FB Power mode (0: full on, 1..3: power saving
	   modes; 4: full off), see FB_BLANK_XXX */
	int power;
	/* FB Blanking active? (values as for power) */
	/* Due to be removed, please use (state & BL_CORE_FBBLANK) */
	int fb_blank;
	/* Backlight type */
	enum backlight_type type;
	/* Flags used to signal drivers of state changes */
	unsigned int state;
	/* Type of the brightness scale (linear, non-linear, ...) */
	enum backlight_scale scale;

#define BL_CORE_SUSPENDED	(1 << 0)	/* backlight is suspended */
#define BL_CORE_FBBLANK		(1 << 1)	/* backlight is under an fb blank event */

};

struct backlight_device {
	/* Backlight properties */
	struct backlight_properties props;

	/* Serialise access to update_status method */
	struct mutex update_lock;

	/* This protects the 'ops' field. If 'ops' is NULL, the driver that
	   registered this device has been unloaded, and if class_get_devdata()
	   points to something in the body of that driver, it is also invalid. */
	struct mutex ops_lock;
	const struct backlight_ops *ops;

	/* The framebuffer notifier block */
	struct notifier_block fb_notif;

	/* list entry of all registered backlight devices */
	struct list_head entry;

	struct device dev;

	/* Multiple framebuffers may share one backlight device */
	bool fb_bl_on[FB_MAX];

	int use_count;
};

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

	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.fb_blank = FB_BLANK_UNBLANK;
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

	bd->props.power = FB_BLANK_POWERDOWN;
	bd->props.fb_blank = FB_BLANK_POWERDOWN;
	bd->props.state |= BL_CORE_FBBLANK;

	return backlight_update_status(bd);
}

/**
 * backlight_put - Drop backlight reference
 * @bd: the backlight device to put
 */
static inline void backlight_put(struct backlight_device *bd)
{
	if (bd)
		put_device(&bd->dev);
}

/**
 * backlight_is_blank - Return true if display is expected to be blank
 * @bd: the backlight device
 *
 * Display is expected to be blank if any of these is true::
 *
 *   1) if power in not UNBLANK
 *   2) if fb_blank is not UNBLANK
 *   3) if state indicate BLANK or SUSPENDED
 *
 * Returns true if display is expected to be blank, false otherwise.
 */
static inline bool backlight_is_blank(const struct backlight_device *bd)
{
	return bd->props.power != FB_BLANK_UNBLANK ||
	       bd->props.fb_blank != FB_BLANK_UNBLANK ||
	       bd->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK);
}

extern struct backlight_device *backlight_device_register(const char *name,
	struct device *dev, void *devdata, const struct backlight_ops *ops,
	const struct backlight_properties *props);
extern struct backlight_device *devm_backlight_device_register(
	struct device *dev, const char *name, struct device *parent,
	void *devdata, const struct backlight_ops *ops,
	const struct backlight_properties *props);
extern void backlight_device_unregister(struct backlight_device *bd);
extern void devm_backlight_device_unregister(struct device *dev,
					struct backlight_device *bd);
extern void backlight_force_update(struct backlight_device *bd,
				   enum backlight_update_reason reason);
extern int backlight_register_notifier(struct notifier_block *nb);
extern int backlight_unregister_notifier(struct notifier_block *nb);
extern struct backlight_device *backlight_device_get_by_type(enum backlight_type type);
struct backlight_device *backlight_device_get_by_name(const char *name);
extern int backlight_device_set_brightness(struct backlight_device *bd, unsigned long brightness);

#define to_backlight_device(obj) container_of(obj, struct backlight_device, dev)

static inline void * bl_get_data(struct backlight_device *bl_dev)
{
	return dev_get_drvdata(&bl_dev->dev);
}

struct generic_bl_info {
	const char *name;
	int max_intensity;
	int default_intensity;
	int limit_mask;
	void (*set_bl_intensity)(int intensity);
	void (*kick_battery)(void);
};

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
struct backlight_device *of_find_backlight(struct device *dev);
struct backlight_device *devm_of_find_backlight(struct device *dev);
#else
static inline struct backlight_device *of_find_backlight(struct device *dev)
{
	return NULL;
}

static inline struct backlight_device *
devm_of_find_backlight(struct device *dev)
{
	return NULL;
}
#endif

#endif
