/*
 * pm_clock.h - Definitions and headers related to device clocks.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#ifndef _LINUX_PM_CLOCK_H
#define _LINUX_PM_CLOCK_H

#include <linux/device.h>
#include <linux/notifier.h>

struct pm_clk_notifier_block {
	struct notifier_block nb;
	struct dev_pm_domain *pm_domain;
	char *con_ids[];
};

#ifdef CONFIG_PM_CLK
static inline bool pm_clk_no_clocks(struct device *dev)
{
	return dev && dev->power.subsys_data
		&& list_empty(&dev->power.subsys_data->clock_list);
}

extern void pm_clk_init(struct device *dev);
extern int pm_clk_create(struct device *dev);
extern void pm_clk_destroy(struct device *dev);
extern int pm_clk_add(struct device *dev, const char *con_id);
extern void pm_clk_remove(struct device *dev, const char *con_id);
extern int pm_clk_suspend(struct device *dev);
extern int pm_clk_resume(struct device *dev);
#else
static inline bool pm_clk_no_clocks(struct device *dev)
{
	return true;
}
static inline void pm_clk_init(struct device *dev)
{
}
static inline int pm_clk_create(struct device *dev)
{
	return -EINVAL;
}
static inline void pm_clk_destroy(struct device *dev)
{
}
static inline int pm_clk_add(struct device *dev, const char *con_id)
{
	return -EINVAL;
}
static inline void pm_clk_remove(struct device *dev, const char *con_id)
{
}
#define pm_clk_suspend	NULL
#define pm_clk_resume	NULL
#endif

#ifdef CONFIG_HAVE_CLK
extern void pm_clk_add_notifier(struct bus_type *bus,
					struct pm_clk_notifier_block *clknb);
#else
static inline void pm_clk_add_notifier(struct bus_type *bus,
					struct pm_clk_notifier_block *clknb)
{
}
#endif

#endif
