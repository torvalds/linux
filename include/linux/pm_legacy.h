#ifndef __LINUX_PM_LEGACY_H__
#define __LINUX_PM_LEGACY_H__


#ifdef CONFIG_PM_LEGACY

/*
 * Register a device with power management
 */
struct pm_dev __deprecated *
pm_register(pm_dev_t type, unsigned long id, pm_callback callback);

/*
 * Send a request to all devices
 */
int __deprecated pm_send_all(pm_request_t rqst, void *data);

#else /* CONFIG_PM_LEGACY */

static inline struct pm_dev *pm_register(pm_dev_t type,
					 unsigned long id,
					 pm_callback callback)
{
	return NULL;
}

static inline int pm_send_all(pm_request_t rqst, void *data)
{
	return 0;
}

#endif /* CONFIG_PM_LEGACY */

#endif /* __LINUX_PM_LEGACY_H__ */

