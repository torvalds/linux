/*
 * pm_wakeirq.h - Device wakeirq helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_PM_WAKEIRQ_H
#define _LINUX_PM_WAKEIRQ_H

#ifdef CONFIG_PM

extern int dev_pm_set_wake_irq(struct device *dev, int irq);
extern int dev_pm_set_dedicated_wake_irq(struct device *dev,
					 int irq);
extern void dev_pm_clear_wake_irq(struct device *dev);
extern void dev_pm_enable_wake_irq(struct device *dev);
extern void dev_pm_disable_wake_irq(struct device *dev);

#else	/* !CONFIG_PM */

static inline int dev_pm_set_wake_irq(struct device *dev, int irq)
{
	return 0;
}

static inline int dev_pm_set_dedicated_wake_irq(struct device *dev, int irq)
{
	return 0;
}

static inline void dev_pm_clear_wake_irq(struct device *dev)
{
}

static inline void dev_pm_enable_wake_irq(struct device *dev)
{
}

static inline void dev_pm_disable_wake_irq(struct device *dev)
{
}

#endif	/* CONFIG_PM */
#endif	/* _LINUX_PM_WAKEIRQ_H */
