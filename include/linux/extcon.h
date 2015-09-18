/*
 *  External connector (extcon) class driver
 *
 * Copyright (C) 2015 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * Copyright (C) 2012 Samsung Electronics
 * Author: Donggeun Kim <dg77.kim@samsung.com>
 * Author: MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * based on switch class driver
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#ifndef __LINUX_EXTCON_H__
#define __LINUX_EXTCON_H__

#include <linux/device.h>

/*
 * Define the unique id of supported external connectors
 */
#define EXTCON_NONE			0

#define EXTCON_USB			1	/* USB connector */
#define EXTCON_USB_HOST			2

#define EXTCON_TA			3	/* Charger connector */
#define EXTCON_FAST_CHARGER		4
#define EXTCON_SLOW_CHARGER		5
#define EXTCON_CHARGE_DOWNSTREAM	6

#define EXTCON_LINE_IN			7	/* Audio/Video connector */
#define EXTCON_LINE_OUT			8
#define EXTCON_MICROPHONE		9
#define EXTCON_HEADPHONE		10
#define EXTCON_HDMI			11
#define EXTCON_MHL			12
#define EXTCON_DVI			13
#define EXTCON_VGA			14
#define EXTCON_SPDIF_IN			15
#define EXTCON_SPDIF_OUT		16
#define EXTCON_VIDEO_IN			17
#define EXTCON_VIDEO_OUT		18

#define EXTCON_DOCK			19	/* Misc connector */
#define EXTCON_JIG			20
#define EXTCON_MECHANICAL		21

struct extcon_cable;

/**
 * struct extcon_dev - An extcon device represents one external connector.
 * @name:		The name of this extcon device. Parent device name is
 *			used if NULL.
 * @supported_cable:	Array of supported cable names ending with EXTCON_NONE.
 *			If supported_cable is NULL, cable name related APIs
 *			are disabled.
 * @mutually_exclusive:	Array of mutually exclusive set of cables that cannot
 *			be attached simultaneously. The array should be
 *			ending with NULL or be NULL (no mutually exclusive
 *			cables). For example, if it is { 0x7, 0x30, 0}, then,
 *			{0, 1}, {0, 1, 2}, {0, 2}, {1, 2}, or {4, 5} cannot
 *			be attached simulataneously. {0x7, 0} is equivalent to
 *			{0x3, 0x6, 0x5, 0}. If it is {0xFFFFFFFF, 0}, there
 *			can be no simultaneous connections.
 * @dev:		Device of this extcon.
 * @state:		Attach/detach state of this extcon. Do not provide at
 *			register-time.
 * @nh:			Notifier for the state change events from this extcon
 * @entry:		To support list of extcon devices so that users can
 *			search for extcon devices based on the extcon name.
 * @lock:
 * @max_supported:	Internal value to store the number of cables.
 * @extcon_dev_type:	Device_type struct to provide attribute_groups
 *			customized for each extcon device.
 * @cables:		Sysfs subdirectories. Each represents one cable.
 *
 * In most cases, users only need to provide "User initializing data" of
 * this struct when registering an extcon. In some exceptional cases,
 * optional callbacks may be needed. However, the values in "internal data"
 * are overwritten by register function.
 */
struct extcon_dev {
	/* Optional user initializing data */
	const char *name;
	const unsigned int *supported_cable;
	const u32 *mutually_exclusive;

	/* Internal data. Please do not set. */
	struct device dev;
	struct raw_notifier_head *nh;
	struct list_head entry;
	int max_supported;
	spinlock_t lock;	/* could be called by irq handler */
	u32 state;

	/* /sys/class/extcon/.../cable.n/... */
	struct device_type extcon_dev_type;
	struct extcon_cable *cables;

	/* /sys/class/extcon/.../mutually_exclusive/... */
	struct attribute_group attr_g_muex;
	struct attribute **attrs_muex;
	struct device_attribute *d_attrs_muex;
};

/**
 * struct extcon_cable - An internal data for each cable of extcon device.
 * @edev:		The extcon device
 * @cable_index:	Index of this cable in the edev
 * @attr_g:		Attribute group for the cable
 * @attr_name:		"name" sysfs entry
 * @attr_state:		"state" sysfs entry
 * @attrs:		Array pointing to attr_name and attr_state for attr_g
 */
struct extcon_cable {
	struct extcon_dev *edev;
	int cable_index;

	struct attribute_group attr_g;
	struct device_attribute attr_name;
	struct device_attribute attr_state;

	struct attribute *attrs[3]; /* to be fed to attr_g.attrs */
};

/**
 * struct extcon_specific_cable_nb - An internal data for
 *				     extcon_register_interest().
 * @user_nb:		user provided notifier block for events from
 *			a specific cable.
 * @cable_index:	the target cable.
 * @edev:		the target extcon device.
 * @previous_value:	the saved previous event value.
 */
struct extcon_specific_cable_nb {
	struct notifier_block *user_nb;
	int cable_index;
	struct extcon_dev *edev;
	unsigned long previous_value;
};

#if IS_ENABLED(CONFIG_EXTCON)

/*
 * Following APIs are for notifiers or configurations.
 * Notifiers are the external port and connection devices.
 */
extern int extcon_dev_register(struct extcon_dev *edev);
extern void extcon_dev_unregister(struct extcon_dev *edev);
extern int devm_extcon_dev_register(struct device *dev,
				    struct extcon_dev *edev);
extern void devm_extcon_dev_unregister(struct device *dev,
				       struct extcon_dev *edev);
extern struct extcon_dev *extcon_get_extcon_dev(const char *extcon_name);

/*
 * Following APIs control the memory of extcon device.
 */
extern struct extcon_dev *extcon_dev_allocate(const unsigned int *cable);
extern void extcon_dev_free(struct extcon_dev *edev);
extern struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
						   const unsigned int *cable);
extern void devm_extcon_dev_free(struct device *dev, struct extcon_dev *edev);

/*
 * get/set/update_state access the 32b encoded state value, which represents
 * states of all possible cables of the multistate port. For example, if one
 * calls extcon_set_state(edev, 0x7), it may mean that all the three cables
 * are attached to the port.
 */
static inline u32 extcon_get_state(struct extcon_dev *edev)
{
	return edev->state;
}

extern int extcon_set_state(struct extcon_dev *edev, u32 state);
extern int extcon_update_state(struct extcon_dev *edev, u32 mask, u32 state);

/*
 * get/set_cable_state access each bit of the 32b encoded state value.
 * They are used to access the status of each cable based on the cable_name.
 */
extern int extcon_get_cable_state_(struct extcon_dev *edev, unsigned int id);
extern int extcon_set_cable_state_(struct extcon_dev *edev, unsigned int id,
				   bool cable_state);

extern int extcon_get_cable_state(struct extcon_dev *edev,
				  const char *cable_name);
extern int extcon_set_cable_state(struct extcon_dev *edev,
				  const char *cable_name, bool cable_state);

/*
 * Following APIs are for notifiees (those who want to be notified)
 * to register a callback for events from a specific cable of the extcon.
 * Notifiees are the connected device drivers wanting to get notified by
 * a specific external port of a connection device.
 */
extern int extcon_register_interest(struct extcon_specific_cable_nb *obj,
				    const char *extcon_name,
				    const char *cable_name,
				    struct notifier_block *nb);
extern int extcon_unregister_interest(struct extcon_specific_cable_nb *nb);

/*
 * Following APIs are to monitor every action of a notifier.
 * Registrar gets notified for every external port of a connection device.
 * Probably this could be used to debug an action of notifier; however,
 * we do not recommend to use this for normal 'notifiee' device drivers who
 * want to be notified by a specific external port of the notifier.
 */
extern int extcon_register_notifier(struct extcon_dev *edev, unsigned int id,
				    struct notifier_block *nb);
extern int extcon_unregister_notifier(struct extcon_dev *edev, unsigned int id,
				    struct notifier_block *nb);

/*
 * Following API get the extcon device from devicetree.
 * This function use phandle of devicetree to get extcon device directly.
 */
extern struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev,
						     int index);

/* Following API to get information of extcon device */
extern const char *extcon_get_edev_name(struct extcon_dev *edev);

#else /* CONFIG_EXTCON */
static inline int extcon_dev_register(struct extcon_dev *edev)
{
	return 0;
}

static inline void extcon_dev_unregister(struct extcon_dev *edev) { }

static inline int devm_extcon_dev_register(struct device *dev,
					   struct extcon_dev *edev)
{
	return -EINVAL;
}

static inline void devm_extcon_dev_unregister(struct device *dev,
					      struct extcon_dev *edev) { }

static inline struct extcon_dev *extcon_dev_allocate(const unsigned int *cable)
{
	return ERR_PTR(-ENOSYS);
}

static inline void extcon_dev_free(struct extcon_dev *edev) { }

static inline struct extcon_dev *devm_extcon_dev_allocate(struct device *dev,
						const unsigned int *cable)
{
	return ERR_PTR(-ENOSYS);
}

static inline void devm_extcon_dev_free(struct extcon_dev *edev) { }

static inline u32 extcon_get_state(struct extcon_dev *edev)
{
	return 0;
}

static inline int extcon_set_state(struct extcon_dev *edev, u32 state)
{
	return 0;
}

static inline int extcon_update_state(struct extcon_dev *edev, u32 mask,
				       u32 state)
{
	return 0;
}

static inline int extcon_get_cable_state_(struct extcon_dev *edev,
					  unsigned int id)
{
	return 0;
}

static inline int extcon_set_cable_state_(struct extcon_dev *edev,
					  unsigned int id, bool cable_state)
{
	return 0;
}

static inline int extcon_get_cable_state(struct extcon_dev *edev,
			const char *cable_name)
{
	return 0;
}

static inline int extcon_set_cable_state(struct extcon_dev *edev,
			const char *cable_name, int state)
{
	return 0;
}

static inline struct extcon_dev *extcon_get_extcon_dev(const char *extcon_name)
{
	return NULL;
}

static inline int extcon_register_notifier(struct extcon_dev *edev,
					unsigned int id,
					struct notifier_block *nb)
{
	return 0;
}

static inline int extcon_unregister_notifier(struct extcon_dev *edev,
					unsigned int id,
					struct notifier_block *nb)
{
	return 0;
}

static inline int extcon_register_interest(struct extcon_specific_cable_nb *obj,
					   const char *extcon_name,
					   const char *cable_name,
					   struct notifier_block *nb)
{
	return 0;
}

static inline int extcon_unregister_interest(struct extcon_specific_cable_nb
						    *obj)
{
	return 0;
}

static inline struct extcon_dev *extcon_get_edev_by_phandle(struct device *dev,
							    int index)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_EXTCON */
#endif /* __LINUX_EXTCON_H__ */
