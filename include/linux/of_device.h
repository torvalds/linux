/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h> /* temporary until merge */

#include <linux/of.h>
#include <linux/mod_devicetable.h>

struct device;

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

extern struct platform_device *of_dev_get(struct platform_device *dev);
extern void of_dev_put(struct platform_device *dev);

extern int of_device_add(struct platform_device *pdev);
extern int of_device_register(struct platform_device *ofdev);
extern void of_device_unregister(struct platform_device *ofdev);

extern const void *of_device_get_match_data(const struct device *dev);

extern ssize_t of_device_modalias(struct device *dev, char *str, ssize_t len);
extern int of_device_request_module(struct device *dev);

extern void of_device_uevent(struct device *dev, struct kobj_uevent_env *env);
extern int of_device_uevent_modalias(struct device *dev, struct kobj_uevent_env *env);

static inline void of_device_node_put(struct device *dev)
{
	of_node_put(dev->of_node);
}

static inline struct device_node *of_cpu_device_node_get(int cpu)
{
	struct device *cpu_dev;
	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return of_get_cpu_node(cpu, NULL);
	return of_node_get(cpu_dev->of_node);
}

int of_dma_configure(struct device *dev,
		     struct device_node *np,
		     bool force_dma);
void of_dma_deconfigure(struct device *dev);
#else /* CONFIG_OF */

static inline int of_driver_match_device(struct device *dev,
					 const struct device_driver *drv)
{
	return 0;
}

static inline void of_device_uevent(struct device *dev,
			struct kobj_uevent_env *env) { }

static inline const void *of_device_get_match_data(const struct device *dev)
{
	return NULL;
}

static inline int of_device_modalias(struct device *dev,
				     char *str, ssize_t len)
{
	return -ENODEV;
}

static inline int of_device_request_module(struct device *dev)
{
	return -ENODEV;
}

static inline int of_device_uevent_modalias(struct device *dev,
				   struct kobj_uevent_env *env)
{
	return -ENODEV;
}

static inline void of_device_node_put(struct device *dev) { }

static inline const struct of_device_id *__of_match_device(
		const struct of_device_id *matches, const struct device *dev)
{
	return NULL;
}
#define of_match_device(matches, dev)	\
	__of_match_device(of_match_ptr(matches), (dev))

static inline struct device_node *of_cpu_device_node_get(int cpu)
{
	return NULL;
}

static inline int of_dma_configure(struct device *dev,
				   struct device_node *np,
				   bool force_dma)
{
	return 0;
}
static inline void of_dma_deconfigure(struct device *dev)
{}
#endif /* CONFIG_OF */

#endif /* _LINUX_OF_DEVICE_H */
