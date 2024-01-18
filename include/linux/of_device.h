/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/platform_device.h>
#include <linux/of_platform.h> /* temporary until merge */

#include <linux/of.h>

struct device;
struct of_device_id;
struct kobj_uevent_env;

#ifdef CONFIG_OF
extern const struct of_device_id *of_match_device(
	const struct of_device_id *matches, const struct device *dev);

/**
 * of_driver_match_device - Tell if a driver's of_match_table matches a device.
 * @drv: the device_driver structure to test
 * @dev: the device structure to match against
 */
static inline int of_driver_match_device(struct device *dev,
					 const struct device_driver *drv)
{
	return of_match_device(drv->of_match_table, dev) != NULL;
}

extern ssize_t of_device_modalias(struct device *dev, char *str, ssize_t len);

extern void of_device_uevent(const struct device *dev, struct kobj_uevent_env *env);
extern int of_device_uevent_modalias(const struct device *dev, struct kobj_uevent_env *env);

int of_dma_configure_id(struct device *dev,
		     struct device_node *np,
		     bool force_dma, const u32 *id);
static inline int of_dma_configure(struct device *dev,
				   struct device_node *np,
				   bool force_dma)
{
	return of_dma_configure_id(dev, np, force_dma, NULL);
}

void of_device_make_bus_id(struct device *dev);

#else /* CONFIG_OF */

static inline int of_driver_match_device(struct device *dev,
					 const struct device_driver *drv)
{
	return 0;
}

static inline void of_device_uevent(const struct device *dev,
			struct kobj_uevent_env *env) { }

static inline int of_device_modalias(struct device *dev,
				     char *str, ssize_t len)
{
	return -ENODEV;
}

static inline int of_device_uevent_modalias(const struct device *dev,
				   struct kobj_uevent_env *env)
{
	return -ENODEV;
}

static inline const struct of_device_id *of_match_device(
		const struct of_device_id *matches, const struct device *dev)
{
	return NULL;
}

static inline int of_dma_configure_id(struct device *dev,
				      struct device_node *np,
				      bool force_dma,
				      const u32 *id)
{
	return 0;
}
static inline int of_dma_configure(struct device *dev,
				   struct device_node *np,
				   bool force_dma)
{
	return 0;
}

static inline void of_device_make_bus_id(struct device *dev) {}

#endif /* CONFIG_OF */

#endif /* _LINUX_OF_DEVICE_H */
