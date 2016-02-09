/*
 * Generic OPP Interface
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_OPP_H__
#define __LINUX_OPP_H__

#include <linux/err.h>
#include <linux/notifier.h>

struct dev_pm_opp;
struct device;

enum dev_pm_opp_event {
	OPP_EVENT_ADD, OPP_EVENT_REMOVE, OPP_EVENT_ENABLE, OPP_EVENT_DISABLE,
};

#if defined(CONFIG_PM_OPP)

unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp);

unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp);

bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp);

int dev_pm_opp_get_opp_count(struct device *dev);
unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev);
unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev);
struct dev_pm_opp *dev_pm_opp_get_suspend_opp(struct device *dev);

struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					      unsigned long freq,
					      bool available);

struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					      unsigned long *freq);

struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					     unsigned long *freq);

int dev_pm_opp_add(struct device *dev, unsigned long freq,
		   unsigned long u_volt);
void dev_pm_opp_remove(struct device *dev, unsigned long freq);

int dev_pm_opp_enable(struct device *dev, unsigned long freq);

int dev_pm_opp_disable(struct device *dev, unsigned long freq);

struct srcu_notifier_head *dev_pm_opp_get_notifier(struct device *dev);
int dev_pm_opp_set_supported_hw(struct device *dev, const u32 *versions,
				unsigned int count);
void dev_pm_opp_put_supported_hw(struct device *dev);
int dev_pm_opp_set_prop_name(struct device *dev, const char *name);
void dev_pm_opp_put_prop_name(struct device *dev);
int dev_pm_opp_set_regulator(struct device *dev, const char *name);
void dev_pm_opp_put_regulator(struct device *dev);
#else
static inline unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	return 0;
}

static inline bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp)
{
	return false;
}

static inline int dev_pm_opp_get_opp_count(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev)
{
	return 0;
}

static inline struct dev_pm_opp *dev_pm_opp_get_suspend_opp(struct device *dev)
{
	return NULL;
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					unsigned long freq, bool available)
{
	return ERR_PTR(-EINVAL);
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					unsigned long *freq)
{
	return ERR_PTR(-EINVAL);
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					unsigned long *freq)
{
	return ERR_PTR(-EINVAL);
}

static inline int dev_pm_opp_add(struct device *dev, unsigned long freq,
					unsigned long u_volt)
{
	return -EINVAL;
}

static inline void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
}

static inline int dev_pm_opp_enable(struct device *dev, unsigned long freq)
{
	return 0;
}

static inline int dev_pm_opp_disable(struct device *dev, unsigned long freq)
{
	return 0;
}

static inline struct srcu_notifier_head *dev_pm_opp_get_notifier(
							struct device *dev)
{
	return ERR_PTR(-EINVAL);
}

static inline int dev_pm_opp_set_supported_hw(struct device *dev,
					      const u32 *versions,
					      unsigned int count)
{
	return -EINVAL;
}

static inline void dev_pm_opp_put_supported_hw(struct device *dev) {}

static inline int dev_pm_opp_set_prop_name(struct device *dev, const char *name)
{
	return -EINVAL;
}

static inline void dev_pm_opp_put_prop_name(struct device *dev) {}

static inline int dev_pm_opp_set_regulator(struct device *dev, const char *name)
{
	return -EINVAL;
}

static inline void dev_pm_opp_put_regulator(struct device *dev) {}

#endif		/* CONFIG_PM_OPP */

#if defined(CONFIG_PM_OPP) && defined(CONFIG_OF)
int dev_pm_opp_of_add_table(struct device *dev);
void dev_pm_opp_of_remove_table(struct device *dev);
int dev_pm_opp_of_cpumask_add_table(cpumask_var_t cpumask);
void dev_pm_opp_of_cpumask_remove_table(cpumask_var_t cpumask);
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask);
int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask);
#else
static inline int dev_pm_opp_of_add_table(struct device *dev)
{
	return -EINVAL;
}

static inline void dev_pm_opp_of_remove_table(struct device *dev)
{
}

static inline int dev_pm_opp_of_cpumask_add_table(cpumask_var_t cpumask)
{
	return -ENOSYS;
}

static inline void dev_pm_opp_of_cpumask_remove_table(cpumask_var_t cpumask)
{
}

static inline int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask)
{
	return -ENOSYS;
}

static inline int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev, cpumask_var_t cpumask)
{
	return -ENOSYS;
}
#endif

#endif		/* __LINUX_OPP_H__ */
