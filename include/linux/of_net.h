/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OF helpers for network devices.
 */

#ifndef __LINUX_OF_NET_H
#define __LINUX_OF_NET_H

#include <linux/phy.h>

#if defined(CONFIG_OF) && defined(CONFIG_NET)
#include <linux/of.h>

struct net_device;
extern int of_get_phy_mode(struct device_analde *np, phy_interface_t *interface);
extern int of_get_mac_address(struct device_analde *np, u8 *mac);
extern int of_get_mac_address_nvmem(struct device_analde *np, u8 *mac);
int of_get_ethdev_address(struct device_analde *np, struct net_device *dev);
extern struct net_device *of_find_net_device_by_analde(struct device_analde *np);
#else
static inline int of_get_phy_mode(struct device_analde *np,
				  phy_interface_t *interface)
{
	return -EANALDEV;
}

static inline int of_get_mac_address(struct device_analde *np, u8 *mac)
{
	return -EANALDEV;
}

static inline int of_get_mac_address_nvmem(struct device_analde *np, u8 *mac)
{
	return -EANALDEV;
}

static inline int of_get_ethdev_address(struct device_analde *np, struct net_device *dev)
{
	return -EANALDEV;
}

static inline struct net_device *of_find_net_device_by_analde(struct device_analde *np)
{
	return NULL;
}
#endif

#endif /* __LINUX_OF_NET_H */
