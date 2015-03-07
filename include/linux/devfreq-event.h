/*
 * devfreq-event: a framework to provide raw data and events of devfreq devices
 *
 * Copyright (C) 2014 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_DEVFREQ_EVENT_H__
#define __LINUX_DEVFREQ_EVENT_H__

#include <linux/device.h>

/**
 * struct devfreq_event_dev - the devfreq-event device
 *
 * @node	: Contain the devfreq-event device that have been registered.
 * @dev		: the device registered by devfreq-event class. dev.parent is
 *		  the device using devfreq-event.
 * @lock	: a mutex to protect accessing devfreq-event.
 * @enable_count: the number of enable function have been called.
 * @desc	: the description for devfreq-event device.
 *
 * This structure contains devfreq-event device information.
 */
struct devfreq_event_dev {
	struct list_head node;

	struct device dev;
	struct mutex lock;
	u32 enable_count;

	const struct devfreq_event_desc *desc;
};

/**
 * struct devfreq_event_data - the devfreq-event data
 *
 * @load_count	: load count of devfreq-event device for the given period.
 * @total_count	: total count of devfreq-event device for the given period.
 *		  each count may represent a clock cycle, a time unit
 *		  (ns/us/...), or anything the device driver wants.
 *		  Generally, utilization is load_count / total_count.
 *
 * This structure contains the data of devfreq-event device for polling period.
 */
struct devfreq_event_data {
	unsigned long load_count;
	unsigned long total_count;
};

/**
 * struct devfreq_event_ops - the operations of devfreq-event device
 *
 * @enable	: Enable the devfreq-event device.
 * @disable	: Disable the devfreq-event device.
 * @reset	: Reset all setting of the devfreq-event device.
 * @set_event	: Set the specific event type for the devfreq-event device.
 * @get_event	: Get the result of the devfreq-event devie with specific
 *		  event type.
 *
 * This structure contains devfreq-event device operations which can be
 * implemented by devfreq-event device drivers.
 */
struct devfreq_event_ops {
	/* Optional functions */
	int (*enable)(struct devfreq_event_dev *edev);
	int (*disable)(struct devfreq_event_dev *edev);
	int (*reset)(struct devfreq_event_dev *edev);

	/* Mandatory functions */
	int (*set_event)(struct devfreq_event_dev *edev);
	int (*get_event)(struct devfreq_event_dev *edev,
			 struct devfreq_event_data *edata);
};

/**
 * struct devfreq_event_desc - the descriptor of devfreq-event device
 *
 * @name	: the name of devfreq-event device.
 * @driver_data	: the private data for devfreq-event driver.
 * @ops		: the operation to control devfreq-event device.
 *
 * Each devfreq-event device is described with a this structure.
 * This structure contains the various data for devfreq-event device.
 */
struct devfreq_event_desc {
	const char *name;
	void *driver_data;

	struct devfreq_event_ops *ops;
};

#if defined(CONFIG_PM_DEVFREQ_EVENT)
extern int devfreq_event_enable_edev(struct devfreq_event_dev *edev);
extern int devfreq_event_disable_edev(struct devfreq_event_dev *edev);
extern bool devfreq_event_is_enabled(struct devfreq_event_dev *edev);
extern int devfreq_event_set_event(struct devfreq_event_dev *edev);
extern int devfreq_event_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata);
extern int devfreq_event_reset_event(struct devfreq_event_dev *edev);
extern struct devfreq_event_dev *devfreq_event_get_edev_by_phandle(
				struct device *dev, int index);
extern int devfreq_event_get_edev_count(struct device *dev);
extern struct devfreq_event_dev *devfreq_event_add_edev(struct device *dev,
				struct devfreq_event_desc *desc);
extern int devfreq_event_remove_edev(struct devfreq_event_dev *edev);
extern struct devfreq_event_dev *devm_devfreq_event_add_edev(struct device *dev,
				struct devfreq_event_desc *desc);
extern void devm_devfreq_event_remove_edev(struct device *dev,
				struct devfreq_event_dev *edev);
static inline void *devfreq_event_get_drvdata(struct devfreq_event_dev *edev)
{
	return edev->desc->driver_data;
}
#else
static inline int devfreq_event_enable_edev(struct devfreq_event_dev *edev)
{
	return -EINVAL;
}

static inline int devfreq_event_disable_edev(struct devfreq_event_dev *edev)
{
	return -EINVAL;
}

static inline bool devfreq_event_is_enabled(struct devfreq_event_dev *edev)
{
	return false;
}

static inline int devfreq_event_set_event(struct devfreq_event_dev *edev)
{
	return -EINVAL;
}

static inline int devfreq_event_get_event(struct devfreq_event_dev *edev,
					struct devfreq_event_data *edata)
{
	return -EINVAL;
}

static inline int devfreq_event_reset_event(struct devfreq_event_dev *edev)
{
	return -EINVAL;
}

static inline void *devfreq_event_get_drvdata(struct devfreq_event_dev *edev)
{
	return ERR_PTR(-EINVAL);
}

static inline struct devfreq_event_dev *devfreq_event_get_edev_by_phandle(
					struct device *dev, int index)
{
	return ERR_PTR(-EINVAL);
}

static inline int devfreq_event_get_edev_count(struct device *dev)
{
	return -EINVAL;
}

static inline struct devfreq_event_dev *devfreq_event_add_edev(struct device *dev,
					struct devfreq_event_desc *desc)
{
	return ERR_PTR(-EINVAL);
}

static inline int devfreq_event_remove_edev(struct devfreq_event_dev *edev)
{
	return -EINVAL;
}

static inline struct devfreq_event_dev *devm_devfreq_event_add_edev(
					struct device *dev,
					struct devfreq_event_desc *desc)
{
	return ERR_PTR(-EINVAL);
}

static inline void devm_devfreq_event_remove_edev(struct device *dev,
					struct devfreq_event_dev *edev)
{
}

static inline void *devfreq_event_get_drvdata(struct devfreq_event_dev *edev)
{
	return NULL;
}
#endif /* CONFIG_PM_DEVFREQ_EVENT */

#endif /* __LINUX_DEVFREQ_EVENT_H__ */
