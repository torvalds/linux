/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OF helpers for network devices.
 */

#ifndef __LINUX_OF_NET_H
#define __LINUX_OF_NET_H

#include <linux/phy.h>

#ifdef CONFIG_OF_NET
#include <linux/of.h>

struct net_device;
extern int of_get_phy_mode(struct device_node *np, phy_interface_t *interface);
extern const void *of_get_mac_address(struct device_node *np);
extern struct net_device *of_find_net_device_by_node(struct device_node *np);
#else
static inline int of_get_phy_mode(struct device_node *np,
				  phy_interface_t *interface)
{
	return -ENODEV;
}

static inline const void *of_get_mac_address(struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline struct net_device *of_find_net_device_by_node(struct device_node *np)
{
	return NULL;
}
#endif

#endif /* __LINUX_OF_NET_H */
