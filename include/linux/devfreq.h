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
#include <linux/pm_opp.h>

#define DEVFREQ_NAME_LEN 16

/* DEVFREQ notifier interface */
#define DEVFREQ_TRANSITION_NOTIFIER	(0)

/* Transition notifiers of DEVFREQ_TRANSITION_NOTIFIER */
#define	DEVFREQ_PRECHANGE		(0)
#define DEVFREQ_POSTCHANGE		(1)

struct devfreq;

/**
 * struct devfreq_dev_status - Data given from devfreq user device to
 *			     governors. Represents the performance
 *			     statistics.
 * @total_time:		The total time represented by this instance of
 *			devfreq_dev_status
 * @busy_time:		The time that the device was working among the
 *			total_time.
 * @current_frequency:	The operating frequency.
 * @private_data:	An entry not specified by the devfreq framework.
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
 * @initial_freq:	The operating frequency when devfreq_add_device() is
 *			called.
 * @polling_ms:		The polling interval in ms. 0 disables polling.
 * @target:		The device should set its operating frequency at
 *			freq or lowest-upper-than-freq value. If freq is
 *			higher than any operable frequency, set maximum.
 *			Before returning, target function should set
 *			freq at the current frequency.
 *			The "flags" parameter's possible values are
 *			explained above with "DEVFREQ_FLAG_*" macros.
 * @get_dev_status:	The device should provide the current performance
 *			status to devfreq. Governors are recommended not to
 *			use this directly. Instead, governors are recommended
 *			to use devfreq_update_stats() along with
 *			devfreq.last_status.
 * @get_cur_freq:	The device should provide the current frequency
 *			at which it is operating.
 * @exit:		An optional callback that is called when devfreq
 *			is removing the devfreq object due to error or
 *			from devfreq_remove_device() call. If the user
 *			has registered devfreq->nb at a notifier-head,
 *			this is the time to unregister it.
 * @freq_table:	Optional list of frequencies to support statistics.
 * @max_state:	The size of freq_table.
 */
struct devfreq_dev_profile {
	unsigned long initial_freq;
	unsigned int polling_ms;

	int (*target)(struct device *dev, unsigned long *freq, u32 flags);
	int (*get_dev_status)(struct device *dev,
			      struct devfreq_dev_status *stat);
	int (*get_cur_freq)(struct device *dev, unsigned long *freq);
	void (*exit)(struct device *dev);

	unsigned long *freq_table;
	unsigned int max_state;
};

/**
 * struct devfreq_governor - Devfreq policy governor
 * @node:		list node - contains registered devfreq governors
 * @name:		Governor's name
 * @immutable:		Immutable flag for governor. If the value is 1,
 *			this govenror is never changeable to other governor.
 * @get_target_freq:	Returns desired operating frequency for the device.
 *			Basically, get_target_freq will run
 *			devfreq_dev_profile.get_dev_status() to get the
 *			status of the device (load = busy_time / total_time).
 *			If no_central_polling is set, this callback is called
 *			only with update_devfreq() notified by OPP.
 * @event_handler:      Callback for devfreq core framework to notify events
 *                      to governors. Events include per device governor
 *                      init and exit, opp changes out of devfreq, suspend
 *                      and resume of per device devfreq during device idle.
 *
 * Note that the callbacks are called with devfreq->lock locked by devfreq.
 */
struct devfreq_governor {
	struct list_head node;

	const char name[DEVFREQ_NAME_LEN];
	const unsigned int immutable;
	int (*get_target_freq)(struct devfreq *this, unsigned long *freq);
	int (*event_handler)(struct devfreq *devfreq,
				unsigned int event, void *data);
};

/**
 * struct devfreq - Device devfreq structure
 * @node:	list node - contains the devices with devfreq that have been
 *		registered.
 * @lock:	a mutex to protect accessing devfreq.
 * @dev:	device registered by devfreq class. dev.parent is the device
 *		using devfreq.
 * @profile:	device-specific devfreq profile
 * @governor:	method how to choose frequency based on the usage.
 * @governor_name:	devfreq governor name for use with this devfreq
 * @nb:		notifier block used to notify devfreq object that it should
 *		reevaluate operable frequencies. Devfreq users may use
 *		devfreq.nb to the corresponding register notifier call chain.
 * @work:	delayed work for load monitoring.
 * @previous_freq:	previously configured frequency value.
 * @data:	Private data of the governor. The devfreq framework does not
 *		touch this.
 * @min_freq:	Limit minimum frequency requested by user (0: none)
 * @max_freq:	Limit maximum frequency requested by user (0: none)
 * @stop_polling:	 devfreq polling status of a device.
 * @total_trans:	Number of devfreq transitions
 * @trans_table:	Statistics of devfreq transitions
 * @time_in_state:	Statistics of devfreq states
 * @last_stat_updated:	The last time stat updated
 * @transition_notifier_list: list head of DEVFREQ_TRANSITION_NOTIFIER notifier
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
	char governor_name[DEVFREQ_NAME_LEN];
	struct notifier_block nb;
	struct delayed_work work;

	unsigned long previous_freq;
	struct devfreq_dev_status last_status;

	void *data; /* private data for governors */

	unsigned long min_freq;
	unsigned long max_freq;
	bool stop_polling;

	/* information for device frequency transition */
	unsigned int total_trans;
	unsigned int *trans_table;
	unsigned long *time_in_state;
	unsigned long last_stat_updated;

	struct srcu_notifier_head transition_notifier_list;
};

struct devfreq_freqs {
	unsigned long old;
	unsigned long new;
};

#if defined(CONFIG_PM_DEVFREQ)
extern struct devfreq *devfreq_add_device(struct device *dev,
				  struct devfreq_dev_profile *profile,
				  const char *governor_name,
				  void *data);
extern int devfreq_remove_device(struct devfreq *devfreq);
extern struct devfreq *devm_devfreq_add_device(struct device *dev,
				  struct devfreq_dev_profile *profile,
				  const char *governor_name,
				  void *data);
extern void devm_devfreq_remove_device(struct device *dev,
				  struct devfreq *devfreq);

/* Supposed to be called by PM callbacks */
extern int devfreq_suspend_device(struct devfreq *devfreq);
extern int devfreq_resume_device(struct devfreq *devfreq);

/* Helper functions for devfreq user device driver with OPP. */
extern struct dev_pm_opp *devfreq_recommended_opp(struct device *dev,
					   unsigned long *freq, u32 flags);
extern int devfreq_register_opp_notifier(struct device *dev,
					 struct devfreq *devfreq);
extern int devfreq_unregister_opp_notifier(struct device *dev,
					   struct devfreq *devfreq);
extern int devm_devfreq_register_opp_notifier(struct device *dev,
					      struct devfreq *devfreq);
extern void devm_devfreq_unregister_opp_notifier(struct device *dev,
						struct devfreq *devfreq);
extern int devfreq_register_notifier(struct devfreq *devfreq,
					struct notifier_block *nb,
					unsigned int list);
extern int devfreq_unregister_notifier(struct devfreq *devfreq,
					struct notifier_block *nb,
					unsigned int list);
extern int devm_devfreq_register_notifier(struct device *dev,
				struct devfreq *devfreq,
				struct notifier_block *nb,
				unsigned int list);
extern void devm_devfreq_unregister_notifier(struct device *dev,
				struct devfreq *devfreq,
				struct notifier_block *nb,
				unsigned int list);
extern struct devfreq *devfreq_get_devfreq_by_phandle(struct device *dev,
						int index);

/**
 * devfreq_update_stats() - update the last_status pointer in struct devfreq
 * @df:		the devfreq instance whose status needs updating
 *
 *  Governors are recommended to use this function along with last_status,
 * which allows other entities to reuse the last_status without affecting
 * the values fetched later by governors.
 */
static inline int devfreq_update_stats(struct devfreq *df)
{
	return df->profile->get_dev_status(df->dev.parent, &df->last_status);
}

#if IS_ENABLED(CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND)
/**
 * struct devfreq_simple_ondemand_data - void *data fed to struct devfreq
 *	and devfreq_add_device
 * @upthreshold:	If the load is over this value, the frequency jumps.
 *			Specify 0 to use the default. Valid value = 0 to 100.
 * @downdifferential:	If the load is under upthreshold - downdifferential,
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

#if IS_ENABLED(CONFIG_DEVFREQ_GOV_PASSIVE)
/**
 * struct devfreq_passive_data - void *data fed to struct devfreq
 *	and devfreq_add_device
 * @parent:	the devfreq instance of parent device.
 * @get_target_freq:	Optional callback, Returns desired operating frequency
 *			for the device using passive governor. That is called
 *			when passive governor should decide the next frequency
 *			by using the new frequency of parent devfreq device
 *			using governors except for passive governor.
 *			If the devfreq device has the specific method to decide
 *			the next frequency, should use this callback.
 * @this:	the devfreq instance of own device.
 * @nb:		the notifier block for DEVFREQ_TRANSITION_NOTIFIER list
 *
 * The devfreq_passive_data have to set the devfreq instance of parent
 * device with governors except for the passive governor. But, don't need to
 * initialize the 'this' and 'nb' field because the devfreq core will handle
 * them.
 */
struct devfreq_passive_data {
	/* Should set the devfreq instance of parent device */
	struct devfreq *parent;

	/* Optional callback to decide the next frequency of passvice device */
	int (*get_target_freq)(struct devfreq *this, unsigned long *freq);

	/* For passive governor's internal use. Don't need to set them */
	struct devfreq *this;
	struct notifier_block nb;
};
#endif

#else /* !CONFIG_PM_DEVFREQ */
static inline struct devfreq *devfreq_add_device(struct device *dev,
					  struct devfreq_dev_profile *profile,
					  const char *governor_name,
					  void *data)
{
	return ERR_PTR(-ENOSYS);
}

static inline int devfreq_remove_device(struct devfreq *devfreq)
{
	return 0;
}

static inline struct devfreq *devm_devfreq_add_device(struct device *dev,
					struct devfreq_dev_profile *profile,
					const char *governor_name,
					void *data)
{
	return ERR_PTR(-ENOSYS);
}

static inline void devm_devfreq_remove_device(struct device *dev,
					struct devfreq *devfreq)
{
}

static inline int devfreq_suspend_device(struct devfreq *devfreq)
{
	return 0;
}

static inline int devfreq_resume_device(struct devfreq *devfreq)
{
	return 0;
}

static inline struct dev_pm_opp *devfreq_recommended_opp(struct device *dev,
					   unsigned long *freq, u32 flags)
{
	return ERR_PTR(-EINVAL);
}

static inline int devfreq_register_opp_notifier(struct device *dev,
					 struct devfreq *devfreq)
{
	return -EINVAL;
}

static inline int devfreq_unregister_opp_notifier(struct device *dev,
					   struct devfreq *devfreq)
{
	return -EINVAL;
}

static inline int devm_devfreq_register_opp_notifier(struct device *dev,
						     struct devfreq *devfreq)
{
	return -EINVAL;
}

static inline void devm_devfreq_unregister_opp_notifier(struct device *dev,
							struct devfreq *devfreq)
{
}

static inline int devfreq_register_notifier(struct devfreq *devfreq,
					struct notifier_block *nb,
					unsigned int list)
{
	return 0;
}

static inline int devfreq_unregister_notifier(struct devfreq *devfreq,
					struct notifier_block *nb,
					unsigned int list)
{
	return 0;
}

static inline int devm_devfreq_register_notifier(struct device *dev,
				struct devfreq *devfreq,
				struct notifier_block *nb,
				unsigned int list)
{
	return 0;
}

static inline void devm_devfreq_unregister_notifier(struct device *dev,
				struct devfreq *devfreq,
				struct notifier_block *nb,
				unsigned int list)
{
}

static inline struct devfreq *devfreq_get_devfreq_by_phandle(struct device *dev,
							int index)
{
	return ERR_PTR(-ENODEV);
}

static inline int devfreq_update_stats(struct devfreq *df)
{
	return -EINVAL;
}
#endif /* CONFIG_PM_DEVFREQ */

#endif /* __LINUX_DEVFREQ_H__ */
