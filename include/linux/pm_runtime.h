/*
 * pm_runtime.h - Device run-time power management helper functions.
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 */

#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <linux/device.h>
#include <linux/pm.h>

#include <linux/jiffies.h>

/* Runtime PM flag argument bits */
#define RPM_ASYNC		0x01	/* Request is asynchronous */
#define RPM_NOWAIT		0x02	/* Don't wait for concurrent
					    state change */
#define RPM_GET_PUT		0x04	/* Increment/decrement the
					    usage_count */
#define RPM_AUTO		0x08	/* Use autosuspend_delay */

#ifdef CONFIG_PM_RUNTIME

extern struct workqueue_struct *pm_wq;

extern int __pm_runtime_idle(struct device *dev, int rpmflags);
extern int __pm_runtime_suspend(struct device *dev, int rpmflags);
extern int __pm_runtime_resume(struct device *dev, int rpmflags);
extern int pm_schedule_suspend(struct device *dev, unsigned int delay);
extern int __pm_runtime_set_status(struct device *dev, unsigned int status);
extern int pm_runtime_barrier(struct device *dev);
extern void pm_runtime_enable(struct device *dev);
extern void __pm_runtime_disable(struct device *dev, bool check_resume);
extern void pm_runtime_allow(struct device *dev);
extern void pm_runtime_forbid(struct device *dev);
extern int pm_generic_runtime_idle(struct device *dev);
extern int pm_generic_runtime_suspend(struct device *dev);
extern int pm_generic_runtime_resume(struct device *dev);
extern void pm_runtime_no_callbacks(struct device *dev);
extern void pm_runtime_irq_safe(struct device *dev);
extern void __pm_runtime_use_autosuspend(struct device *dev, bool use);
extern void pm_runtime_set_autosuspend_delay(struct device *dev, int delay);
extern unsigned long pm_runtime_autosuspend_expiration(struct device *dev);

static inline bool pm_children_suspended(struct device *dev)
{
	return dev->power.ignore_children
		|| !atomic_read(&dev->power.child_count);
}

static inline void pm_suspend_ignore_children(struct device *dev, bool enable)
{
	dev->power.ignore_children = enable;
}

static inline void pm_runtime_get_noresume(struct device *dev)
{
	atomic_inc(&dev->power.usage_count);
}

static inline void pm_runtime_put_noidle(struct device *dev)
{
	atomic_add_unless(&dev->power.usage_count, -1, 0);
}

static inline bool device_run_wake(struct device *dev)
{
	return dev->power.run_wake;
}

static inline void device_set_run_wake(struct device *dev, bool enable)
{
	dev->power.run_wake = enable;
}

static inline bool pm_runtime_suspended(struct device *dev)
{
	return dev->power.runtime_status == RPM_SUSPENDED
		&& !dev->power.disable_depth;
}

static inline bool pm_runtime_enabled(struct device *dev)
{
	return !dev->power.disable_depth;
}

static inline bool pm_runtime_callbacks_present(struct device *dev)
{
	return !dev->power.no_callbacks;
}

static inline void pm_runtime_mark_last_busy(struct device *dev)
{
	ACCESS_ONCE(dev->power.last_busy) = jiffies;
}

#else /* !CONFIG_PM_RUNTIME */

static inline int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_suspend(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	return 1;
}
static inline int pm_schedule_suspend(struct device *dev, unsigned int delay)
{
	return -ENOSYS;
}
static inline int __pm_runtime_set_status(struct device *dev,
					    unsigned int status) { return 0; }
static inline int pm_runtime_barrier(struct device *dev) { return 0; }
static inline void pm_runtime_enable(struct device *dev) {}
static inline void __pm_runtime_disable(struct device *dev, bool c) {}
static inline void pm_runtime_allow(struct device *dev) {}
static inline void pm_runtime_forbid(struct device *dev) {}

static inline bool pm_children_suspended(struct device *dev) { return false; }
static inline void pm_suspend_ignore_children(struct device *dev, bool en) {}
static inline void pm_runtime_get_noresume(struct device *dev) {}
static inline void pm_runtime_put_noidle(struct device *dev) {}
static inline bool device_run_wake(struct device *dev) { return false; }
static inline void device_set_run_wake(struct device *dev, bool enable) {}
static inline bool pm_runtime_suspended(struct device *dev) { return false; }
static inline bool pm_runtime_enabled(struct device *dev) { return false; }

static inline int pm_generic_runtime_idle(struct device *dev) { return 0; }
static inline int pm_generic_runtime_suspend(struct device *dev) { return 0; }
static inline int pm_generic_runtime_resume(struct device *dev) { return 0; }
static inline void pm_runtime_no_callbacks(struct device *dev) {}
static inline void pm_runtime_irq_safe(struct device *dev) {}

static inline bool pm_runtime_callbacks_present(struct device *dev) { return false; }
static inline void pm_runtime_mark_last_busy(struct device *dev) {}
static inline void __pm_runtime_use_autosuspend(struct device *dev,
						bool use) {}
static inline void pm_runtime_set_autosuspend_delay(struct device *dev,
						int delay) {}
static inline unsigned long pm_runtime_autosuspend_expiration(
				struct device *dev) { return 0; }

#endif /* !CONFIG_PM_RUNTIME */

static inline int pm_runtime_idle(struct device *dev)
{
	return __pm_runtime_idle(dev, 0);
}

static inline int pm_runtime_suspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, 0);
}

static inline int pm_runtime_autosuspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_AUTO);
}

static inline int pm_runtime_resume(struct device *dev)
{
	return __pm_runtime_resume(dev, 0);
}

static inline int pm_request_idle(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_ASYNC);
}

static inline int pm_request_resume(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_ASYNC);
}

static inline int pm_request_autosuspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_ASYNC | RPM_AUTO);
}

static inline int pm_runtime_get(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_GET_PUT | RPM_ASYNC);
}

static inline int pm_runtime_get_sync(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_GET_PUT);
}

static inline int pm_runtime_put(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_GET_PUT | RPM_ASYNC);
}

static inline int pm_runtime_put_autosuspend(struct device *dev)
{
	return __pm_runtime_suspend(dev,
	    RPM_GET_PUT | RPM_ASYNC | RPM_AUTO);
}

static inline int pm_runtime_put_sync(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_GET_PUT);
}

static inline int pm_runtime_put_sync_suspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_GET_PUT);
}

static inline int pm_runtime_put_sync_autosuspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_GET_PUT | RPM_AUTO);
}

static inline int pm_runtime_set_active(struct device *dev)
{
	return __pm_runtime_set_status(dev, RPM_ACTIVE);
}

static inline void pm_runtime_set_suspended(struct device *dev)
{
	__pm_runtime_set_status(dev, RPM_SUSPENDED);
}

static inline void pm_runtime_disable(struct device *dev)
{
	__pm_runtime_disable(dev, true);
}

static inline void pm_runtime_use_autosuspend(struct device *dev)
{
	__pm_runtime_use_autosuspend(dev, true);
}

static inline void pm_runtime_dont_use_autosuspend(struct device *dev)
{
	__pm_runtime_use_autosuspend(dev, false);
}

struct pm_clk_notifier_block {
	struct notifier_block nb;
	struct dev_power_domain *pwr_domain;
	char *con_ids[];
};

#ifdef CONFIG_PM_RUNTIME_CLK
extern int pm_runtime_clk_init(struct device *dev);
extern void pm_runtime_clk_destroy(struct device *dev);
extern int pm_runtime_clk_add(struct device *dev, const char *con_id);
extern void pm_runtime_clk_remove(struct device *dev, const char *con_id);
extern int pm_runtime_clk_suspend(struct device *dev);
extern int pm_runtime_clk_resume(struct device *dev);
#else
static inline int pm_runtime_clk_init(struct device *dev)
{
	return -EINVAL;
}
static inline void pm_runtime_clk_destroy(struct device *dev)
{
}
static inline int pm_runtime_clk_add(struct device *dev, const char *con_id)
{
	return -EINVAL;
}
static inline void pm_runtime_clk_remove(struct device *dev, const char *con_id)
{
}
#define pm_runtime_clock_suspend	NULL
#define pm_runtime_clock_resume		NULL
#endif

#ifdef CONFIG_HAVE_CLK
extern void pm_runtime_clk_add_notifier(struct bus_type *bus,
					struct pm_clk_notifier_block *clknb);
#else
static inline void pm_runtime_clk_add_notifier(struct bus_type *bus,
					struct pm_clk_notifier_block *clknb)
{
}
#endif

#endif
