/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pm_clock.h - Definitions and headers related to device clocks.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
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

struct clk;

#ifdef CONFIG_PM
extern int pm_clk_runtime_suspend(struct device *dev);
extern int pm_clk_runtime_resume(struct device *dev);
#define USE_PM_CLK_RUNTIME_OPS \
	.runtime_suspend = pm_clk_runtime_suspend, \
	.runtime_resume = pm_clk_runtime_resume,
#else
#define USE_PM_CLK_RUNTIME_OPS
#endif

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
extern int pm_clk_add_clk(struct device *dev, struct clk *clk);
extern int of_pm_clk_add_clks(struct device *dev);
extern void pm_clk_remove_clk(struct device *dev, struct clk *clk);
extern int pm_clk_suspend(struct device *dev);
extern int pm_clk_resume(struct device *dev);
extern int devm_pm_clk_create(struct device *dev);
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

static inline int pm_clk_add_clk(struct device *dev, struct clk *clk)
{
	return -EINVAL;
}
static inline int of_pm_clk_add_clks(struct device *dev)
{
	return -EINVAL;
}
#define pm_clk_suspend	NULL
#define pm_clk_resume	NULL
static inline void pm_clk_remove_clk(struct device *dev, struct clk *clk)
{
}
static inline int devm_pm_clk_create(struct device *dev)
{
	return -EINVAL;
}
#endif

#ifdef CONFIG_HAVE_CLK
extern void pm_clk_add_notifier(const struct bus_type *bus,
					struct pm_clk_notifier_block *clknb);
#else
static inline void pm_clk_add_notifier(const struct bus_type *bus,
					struct pm_clk_notifier_block *clknb)
{
}
#endif

#endif
