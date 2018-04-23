/*
 * OF helpers for network devices.
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_OF_NET_H
#define __LINUX_OF_NET_H

#ifdef CONFIG_OF_NET
#include <linux/of.h>

struct net_device;
extern int of_get_phy_mode(struct device_node *np);
extern const void *of_get_mac_address(struct device_node *np);
extern int of_get_nvmem_mac_address(struct device_node *np, void *addr);
extern struct net_device *of_find_net_device_by_node(struct device_node *np);
#else
static inline int of_get_phy_mode(struct device_node *np)
{
	return -ENODEV;
}

static inline const void *of_get_mac_address(struct device_node *np)
{
	return NULL;
}

static inline int of_get_nvmem_mac_address(struct device_node *np, void *addr)
{
	return -ENODEV;
}

static inline struct net_device *of_find_net_device_by_node(struct device_node *np)
{
	return NULL;
}
#endif

#endif /* __LINUX_OF_NET_H */
