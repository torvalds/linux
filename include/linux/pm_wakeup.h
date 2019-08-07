/*
 *  pm_wakeup.h - Power management wakeup interface
 *
 *  Copyright (C) 2008 Alan Stern
 *  Copyright (C) 2010 Rafael J. Wysocki, Novell Inc.
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

#include <linux/types.h>

struct wake_irq;

/**
 * struct wakeup_source - Representation of wakeup sources
 *
 * @name: Name of the wakeup source
 * @id: Wakeup source id
 * @entry: Wakeup source list entry
 * @lock: Wakeup source lock
 * @wakeirq: Optional device specific wakeirq
 * @timer: Wakeup timer list
 * @timer_expires: Wakeup timer expiration
 * @total_time: Total time this wakeup source has been active.
 * @max_time: Maximum time this wakeup source has been continuously active.
 * @last_time: Monotonic clock when the wakeup source's was touched last time.
 * @prevent_sleep_time: Total time this source has been preventing autosleep.
 * @event_count: Number of signaled wakeup events.
 * @active_count: Number of times the wakeup source was activated.
 * @relax_count: Number of times the wakeup source was deactivated.
 * @expire_count: Number of times the wakeup source's timeout has expired.
 * @wakeup_count: Number of times the wakeup source might abort suspend.
 * @dev: Struct device for sysfs statistics about the wakeup source.
 * @active: Status of the wakeup source.
 * @has_timeout: The wakeup source has been activated with a timeout.
 */
struct wakeup_source {
	const char 		*name;
	int			id;
	struct list_head	entry;
	spinlock_t		lock;
	struct wake_irq		*wakeirq;
	struct timer_list	timer;
	unsigned long		timer_expires;
	ktime_t total_time;
	ktime_t max_time;
	ktime_t last_time;
	ktime_t start_prevent_time;
	ktime_t prevent_sleep_time;
	unsigned long		event_count;
	unsigned long		active_count;
	unsigned long		relax_count;
	unsigned long		expire_count;
	unsigned long		wakeup_count;
	struct device		*dev;
	bool			active:1;
	bool			autosleep_enabled:1;
};

#ifdef CONFIG_PM_SLEEP

/*
 * Changes to device_may_wakeup take effect on the next pm state change.
 */

static inline bool device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

static inline bool device_may_wakeup(struct device *dev)
{
	return dev->power.can_wakeup && !!dev->power.wakeup;
}

static inline void device_set_wakeup_path(struct device *dev)
{
	dev->power.wakeup_path = true;
}

/* drivers/base/power/wakeup.c */
extern struct wakeup_source *wakeup_source_create(const char *name);
extern void wakeup_source_destroy(struct wakeup_source *ws);
extern void wakeup_source_add(struct wakeup_source *ws);
extern void wakeup_source_remove(struct wakeup_source *ws);
extern struct wakeup_source *wakeup_source_register(struct device *dev,
						    const char *name);
extern void wakeup_source_unregister(struct wakeup_source *ws);
extern int device_wakeup_enable(struct device *dev);
extern int device_wakeup_disable(struct device *dev);
extern void device_set_wakeup_capable(struct device *dev, bool capable);
extern int device_init_wakeup(struct device *dev, bool val);
extern int device_set_wakeup_enable(struct device *dev, bool enable);
extern void __pm_stay_awake(struct wakeup_source *ws);
extern void pm_stay_awake(struct device *dev);
extern void __pm_relax(struct wakeup_source *ws);
extern void pm_relax(struct device *dev);
extern void pm_wakeup_ws_event(struct wakeup_source *ws, unsigned int msec, bool hard);
extern void pm_wakeup_dev_event(struct device *dev, unsigned int msec, bool hard);

#else /* !CONFIG_PM_SLEEP */

static inline void device_set_wakeup_capable(struct device *dev, bool capable)
{
	dev->power.can_wakeup = capable;
}

static inline bool device_can_wakeup(struct device *dev)
{
	return dev->power.can_wakeup;
}

static inline struct wakeup_source *wakeup_source_create(const char *name)
{
	return NULL;
}

static inline void wakeup_source_destroy(struct wakeup_source *ws) {}

static inline void wakeup_source_add(struct wakeup_source *ws) {}

static inline void wakeup_source_remove(struct wakeup_source *ws) {}

static inline struct wakeup_source *wakeup_source_register(struct device *dev,
							   const char *name)
{
	return NULL;
}

static inline void wakeup_source_unregister(struct wakeup_source *ws) {}

static inline int device_wakeup_enable(struct device *dev)
{
	dev->power.should_wakeup = true;
	return 0;
}

static inline int device_wakeup_disable(struct device *dev)
{
	dev->power.should_wakeup = false;
	return 0;
}

static inline int device_set_wakeup_enable(struct device *dev, bool enable)
{
	dev->power.should_wakeup = enable;
	return 0;
}

static inline int device_init_wakeup(struct device *dev, bool val)
{
	device_set_wakeup_capable(dev, val);
	device_set_wakeup_enable(dev, val);
	return 0;
}

static inline bool device_may_wakeup(struct device *dev)
{
	return dev->power.can_wakeup && dev->power.should_wakeup;
}

static inline void device_set_wakeup_path(struct device *dev) {}

static inline void __pm_stay_awake(struct wakeup_source *ws) {}

static inline void pm_stay_awake(struct device *dev) {}

static inline void __pm_relax(struct wakeup_source *ws) {}

static inline void pm_relax(struct device *dev) {}

static inline void pm_wakeup_ws_event(struct wakeup_source *ws,
				      unsigned int msec, bool hard) {}

static inline void pm_wakeup_dev_event(struct device *dev, unsigned int msec,
				       bool hard) {}

#endif /* !CONFIG_PM_SLEEP */

static inline void __pm_wakeup_event(struct wakeup_source *ws, unsigned int msec)
{
	return pm_wakeup_ws_event(ws, msec, false);
}

static inline void pm_wakeup_event(struct device *dev, unsigned int msec)
{
	return pm_wakeup_dev_event(dev, msec, false);
}

static inline void pm_wakeup_hard_event(struct device *dev)
{
	return pm_wakeup_dev_event(dev, 0, true);
}

#endif /* _LINUX_PM_WAKEUP_H */
