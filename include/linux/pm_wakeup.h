/*
 *  pm_wakeup.h - Power management wakeup interface
 *
 *  Copyright (C) 2008 Alan Stern
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_PM_WAKEUP_H
#define _LINUX_PM_WAKEUP_H

#ifndef _DEVICE_H_
# error "please don't include this file directly"
#endif

#ifdef CONFIG_PM

/* changes to device_may_wakeup take effect on the next pm state change.
 * by default, devices should wakeup if they can.
 */
static inline void device_init_wakeup(struct device *dev, int val)
{
	dev->power.can_wakeup = dev->power.should_wakeup = !!val;
}

static inline int device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

static inline void device_set_wakeup_enable(struct device *dev, int val)
{
	dev->power.should_wakeup = !!val;
}

static inline int device_may_wakeup(struct device *dev)
{
	return dev->power.can_wakeup & dev->power.should_wakeup;
}

/*
 * Platform hook to activate device wakeup capability, if that's not already
 * handled by enable_irq_wake() etc.
 * Returns zero on success, else negative errno
 */
extern int (*platform_enable_wakeup)(struct device *dev, int is_on);

static inline int call_platform_enable_wakeup(struct device *dev, int is_on)
{
	if (platform_enable_wakeup)
		return (*platform_enable_wakeup)(dev, is_on);
	return 0;
}

#else /* !CONFIG_PM */

/* For some reason the next two routines work even without CONFIG_PM */
static inline void device_init_wakeup(struct device *dev, int val)
{
	dev->power.can_wakeup = !!val;
}

static inline int device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

#define device_set_wakeup_enable(dev, val)	do {} while (0)
#define device_may_wakeup(dev)			0

static inline int call_platform_enable_wakeup(struct device *dev, int is_on)
{
	return 0;
}

#endif /* !CONFIG_PM */

#endif /* _LINUX_PM_WAKEUP_H */
