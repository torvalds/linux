/*
 * devfreq: Generic Dynamic Voltage and Frequency Scaling (DVFS) Framework
 *	    for Non-CPU Devices.
 *
 * Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_DEVFREQ_H__
#define __LINUX_DEVFREQ_H__

#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/opp.h>

#define DEVFREQ_NAME_LEN 16

struct devfreq;

/**
 * struct devfreq_dev_status - Data given from devfreq user device to
 *			     governors. Represents the performance
 *			     statistics.
 * @total_time		The total time represented by this instance of
 *			devfreq_dev_status
 * @busy_time		The time that the device was working among the
 *			total_time.
 * @current_frequency	The operating frequency.
 * @private_data	An entry not specified by the devfreq framework.
 *			A device and a specific governor may have their
 *			own protocol with private_data. However, because
 *			this is governor-specific, a governor using this
 *			will be only compatible with devices aware of it.
 */
struct devfreq_dev_status {
	/* both since the last measure */
	unsigned long total_time;
	unsigned long busy_time;
	unsigned long current_frequency;
	void *private_data;
};

/*
 * The resulting frequency should be at most this. (this bound is the
 * least upper bound; thus, the resulting freq should be lower or same)
 * If the flag is not set, the resulting frequency should be at most the
 * bound (greatest lower bound)
 */
#define DEVFREQ_FLAG_LEAST_UPPER_BOUND		0x1

/**
 * struct devfreq_dev_profile - Devfreq's user device profile
 * @initial_freq	The operating frequency when devfreq_add_device() is
 *			called.
 * @polling_ms		The polling interval in ms. 0 disables polling.
 * @target		The device should set its operating frequency at
 *			freq or lowest-upper-than-freq value. If freq is
 *			higher than any operable frequency, set maximum.
 *			Before returning, target function should set
 *			freq at the current frequency.
 *			The "flags" parameter's possible values are
 *			explained above with "DEVFREQ_FLAG_*" macros.
 * @get_dev_status	The device should provide the current performance
 *			status to devfreq, which is used by governors.
 * @exit		An optional callback that is called when devfreq
 *			is removing the devfreq object due to error or
 *			from devfreq_remove_device() call. If the user
 *			has registered devfreq->nb at a notifier-head,
 *			this is the time to unregister it.
 */
struct devfreq_dev_profile {
	unsigned long initial_freq;
	unsigned int polling_ms;

	int (*target)(struct device *dev, unsigned long *freq, u32 flags);
	int (*get_dev_status)(struct device *dev,
			      struct devfreq_dev_status *stat);
	void (*exit)(struct device *dev);
};

/**
 * struct devfreq_governor - Devfreq policy governor
 * @name		Governor's name
 * @get_target_freq	Returns desired operating frequency for the device.
 *			Basically, get_target_freq will run
 *			devfreq_dev_profile.get_dev_status() to get the
 *			status of the device (load = busy_time / total_time).
 *			If no_central_polling is set, this callback is called
 *			only with update_devfreq() notified by OPP.
 * @init		Called when the devfreq is being attached to a device
 * @exit		Called when the devfreq is being removed from a
 *			device. Governor should stop any internal routines
 *			before return because related data may be
 *			freed after exit().
 * @no_central_polling	Do not use devfreq's central polling mechanism.
 *			When this is set, devfreq will not call
 *			get_target_freq with devfreq_monitor(). However,
 *			devfreq will call get_target_freq with
 *			devfreq_update() notified by OPP framework.
 *
 * Note that the callbacks are called with devfreq->lock locked by devfreq.
 */
struct devfreq_governor {
	const char name[DEVFREQ_NAME_LEN];
	int (*get_target_freq)(struct devfreq *this, unsigned long *freq);
	int (*init)(struct devfreq *this);
	void (*exit)(struct devfreq *this);
	const bool no_central_polling;
};

/**
 * struct devfreq - Device devfreq structure
 * @node	list node - contains the devices with devfreq that have been
 *		registered.
 * @lock	a mutex to protect accessing devfreq.
 * @dev		device registered by devfreq class. dev.parent is the device
 *		using devfreq.
 * @profile	device-specific devfreq profile
 * @governor	method how to choose frequency based on the usage.
 * @nb		notifier block used to notify devfreq object that it should
 *		reevaluate operable frequencies. Devfreq users may use
 *		devfreq.nb to the corresponding register notifier call chain.
 * @polling_jiffies	interval in jiffies.
 * @previous_freq	previously configured frequency value.
 * @next_polling	the number of remaining jiffies to poll with
 *			"devfreq_monitor" executions to reevaluate
 *			frequency/voltage of the device. Set by
 *			profile's polling_ms interval.
 * @data	Private data of the governor. The devfreq framework does not
 *		touch this.
 * @being_removed	a flag to mark that this object is being removed in
 *			order to prevent trying to remove the object multiple times.
 * @min_freq	Limit minimum frequency requested by user (0: none)
 * @max_freq	Limit maximum frequency requested by user (0: none)
 *
 * This structure stores the devfreq information for a give device.
 *
 * Note that when a governor accesses entries in struct devfreq in its
 * functions except for the context of callbacks defined in struct
 * devfreq_governor, the governor should protect its access with the
 * struct mutex lock in struct devfreq. A governor may use this mutex
 * to protect its own private data in void *data as well.
 */
struct devfreq {
	struct list_head node;

	struct mutex lock;
	struct device dev;
	struct devfreq_dev_profile *profile;
	const struct devfreq_governor *governor;
	struct notifier_block nb;

	unsigned long polling_jiffies;
	unsigned long previous_freq;
	unsigned int next_polling;

	void *data; /* private data for governors */

	bool being_removed;

	unsigned long min_freq;
	unsigned long max_freq;
};

#if defined(CONFIG_PM_DEVFREQ)
extern struct devfreq *devfreq_add_device(struct device *dev,
				  struct devfreq_dev_profile *profile,
				  const struct devfreq_governor *governor,
				  void *data);
extern int devfreq_remove_device(struct devfreq *devfreq);

/* Helper functions for devfreq user device driver with OPP. */
extern struct opp *devfreq_recommended_opp(struct device *dev,
					   unsigned long *freq, u32 flags);
extern int devfreq_register_opp_notifier(struct device *dev,
					 struct devfreq *devfreq);
extern int devfreq_unregister_opp_notifier(struct device *dev,
					   struct devfreq *devfreq);

#ifdef CONFIG_DEVFREQ_GOV_POWERSAVE
extern const struct devfreq_governor devfreq_powersave;
#endif
#ifdef CONFIG_DEVFREQ_GOV_PERFORMANCE
extern const struct devfreq_governor devfreq_performance;
#endif
#ifdef CONFIG_DEVFREQ_GOV_USERSPACE
extern const struct devfreq_governor devfreq_userspace;
#endif
#ifdef CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND
extern const struct devfreq_governor devfreq_simple_ondemand;
/**
 * struct devfreq_simple_ondemand_data - void *data fed to struct devfreq
 *	and devfreq_add_device
 * @ upthreshold	If the load is over this value, the frequency jumps.
 *			Specify 0 to use the default. Valid value = 0 to 100.
 * @ downdifferential	If the load is under upthreshold - downdifferential,
 *			the governor may consider slowing the frequency down.
 *			Specify 0 to use the default. Valid value = 0 to 100.
 *			downdifferential < upthreshold must hold.
 *
 * If the fed devfreq_simple_ondemand_data pointer is NULL to the governor,
 * the governor uses the default values.
 */
struct devfreq_simple_ondemand_data {
	unsigned int upthreshold;
	unsigned int downdifferential;
};
#endif

#else /* !CONFIG_PM_DEVFREQ */
static struct devfreq *devfreq_add_device(struct device *dev,
					  struct devfreq_dev_profile *profile,
					  struct devfreq_governor *governor,
					  void *data)
{
	return NULL;
}

static int devfreq_remove_device(struct devfreq *devfreq)
{
	return 0;
}

static struct opp *devfreq_recommended_opp(struct device *dev,
					   unsigned long *freq, u32 flags)
{
	return -EINVAL;
}

static int devfreq_register_opp_notifier(struct device *dev,
					 struct devfreq *devfreq)
{
	return -EINVAL;
}

static int devfreq_unregister_opp_notifier(struct device *dev,
					   struct devfreq *devfreq)
{
	return -EINVAL;
}

#define devfreq_powersave	NULL
#define devfreq_performance	NULL
#define devfreq_userspace	NULL
#define devfreq_simple_ondemand	NULL

#endif /* CONFIG_PM_DEVFREQ */

#endif /* __LINUX_DEVFREQ_H__ */
