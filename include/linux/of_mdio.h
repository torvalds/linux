/*
 * OF helpers for the MDIO (Ethernet PHY) API
 *
 * Copyright (c) 2009 Secret Lab Technologies, Ltd.
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_OF_MDIO_H
#define __LINUX_OF_MDIO_H

#include <linux/phy.h>
#include <linux/of.h>

#ifdef CONFIG_OF
extern int of_mdiobus_register(struct mii_bus *mdio, struct device_node *np);
extern struct phy_device *of_phy_find_device(struct device_node *phy_np);
extern struct phy_device *of_phy_connect(struct net_device *dev,
					 struct device_node *phy_np,
					 void (*hndlr)(struct net_device *),
					 u32 flags, phy_interface_t iface);
extern struct phy_device *of_phy_connect_fixed_link(struct net_device *dev,
					 void (*hndlr)(struct net_device *),
					 phy_interface_t iface);

extern struct mii_bus *of_mdio_find_bus(struct device_node *mdio_np);

#else /* CONFIG_OF */
int of_mdiobus_register(struct mii_bus *mdio, struct device_node *np)
{
	return -ENOSYS;
}

struct phy_device *of_phy_find_device(struct device_node *phy_np)
{
	return NULL;
}

struct phy_device *of_phy_connect(struct net_device *dev,
					 struct device_node *phy_np,
					 void (*hndlr)(struct net_device *),
					 u32 flags, phy_interface_t iface)
{
	return NULL;
}

struct phy_device *of_phy_connect_fixed_link(struct net_device *dev,
					 void (*hndlr)(struct net_device *),
					 phy_interface_t iface)
{
	return NULL;
}

struct mii_bus *of_mdio_find_bus(struct device_node *mdio_np)
{
	return NULL;
}
#endif /* CONFIG_OF */

#endif /* __LINUX_OF_MDIO_H */
