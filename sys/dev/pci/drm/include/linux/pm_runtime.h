/* Public domain. */

#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <sys/types.h>
#include <sys/device.h>
#include <linux/pm.h>

static inline void
pm_runtime_mark_last_busy(struct device *dev)
{
}

static inline void
pm_runtime_use_autosuspend(struct device *dev)
{
}

static inline void
pm_runtime_dont_use_autosuspend(struct device *dev)
{
}

static inline void
pm_runtime_put_autosuspend(struct device *dev)
{
}

static inline void
pm_runtime_set_autosuspend_delay(struct device *dev, int x)
{
}

static inline void
pm_runtime_set_active(struct device *dev)
{
}

static inline void
pm_runtime_allow(struct device *dev)
{
}

static inline void
pm_runtime_put_noidle(struct device *dev)
{
}

static inline void
pm_runtime_forbid(struct device *dev)
{
}

static inline void
pm_runtime_get_noresume(struct device *dev)
{
}

static inline void
pm_runtime_put(struct device *dev)
{
}

static inline int
pm_runtime_get_sync(struct device *dev)
{
	return 0;
}

static inline int
pm_runtime_get_if_in_use(struct device *dev)
{
	return -EINVAL;
}

static inline int
pm_runtime_get_if_active(struct device *dev)
{
	return -EINVAL;
}

static inline int
pm_runtime_suspended(struct device *dev)
{
	return 0;
}

#endif
