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

int dev_pm_opp_get_opp_count(struct device *dev);

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
#else
static inline unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	return 0;
}

static inline int dev_pm_opp_get_opp_count(struct device *dev)
{
	return 0;
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
#endif		/* CONFIG_PM_OPP */

#if defined(CONFIG_PM_OPP) && defined(CONFIG_OF)
int of_init_opp_table(struct device *dev);
void of_free_opp_table(struct device *dev);
#else
static inline int of_init_opp_table(struct device *dev)
{
	return -EINVAL;
}

static inline void of_free_opp_table(struct device *dev)
{
}
#endif

#endif		/* __LINUX_OPP_H__ */
