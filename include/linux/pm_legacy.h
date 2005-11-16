#ifndef __LINUX_PM_LEGACY_H__
#define __LINUX_PM_LEGACY_H__

#include <linux/config.h>

#ifdef CONFIG_PM_LEGACY

extern int pm_active;

#define PM_IS_ACTIVE() (pm_active != 0)

/*
 * Register a device with power management
 */
struct pm_dev __deprecated *
pm_register(pm_dev_t type, unsigned long id, pm_callback callback);

/*
 * Unregister a device with power management
 */
void __deprecated pm_unregister(struct pm_dev *dev);

/*
 * Unregister all devices with matching callback
 */
void __deprecated pm_unregister_all(pm_callback callback);

/*
 * Send a request to all devices
 */
int __deprecated pm_send_all(pm_request_t rqst, void *data);

#else /* CONFIG_PM_LEGACY */

#define PM_IS_ACTIVE() 0

static inline struct pm_dev *pm_register(pm_dev_t type,
					 unsigned long id,
					 pm_callback callback)
{
	return NULL;
}

static inline void pm_unregister(struct pm_dev *dev) {}

static inline void pm_unregister_all(pm_callback callback) {}

static inline int pm_send_all(pm_request_t rqst, void *data)
{
	return 0;
}

#endif /* CONFIG_PM_LEGACY */

#endif /* __LINUX_PM_LEGACY_H__ */

