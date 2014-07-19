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
struct phy_device *of_phy_attach(struct net_device *dev,
				 struct device_node *phy_np, u32 flags,
				 phy_interface_t iface);

extern struct mii_bus *of_mdio_find_bus(struct device_node *mdio_np);

extern void of_mdiobus_link_phydev(struct mii_bus *mdio,
				   struct phy_device *phydev);

#else /* CONFIG_OF */
static inline int of_mdiobus_register(struct mii_bus *mdio, struct device_node *np)
{
	/*
	 * Fall back to the non-DT function to register a bus.
	 * This way, we don't have to keep compat bits around in drivers.
	 */

	return mdiobus_register(mdio);
}

static inline struct phy_device *of_phy_find_device(struct device_node *phy_np)
{
	return NULL;
}

static inline struct phy_device *of_phy_connect(struct net_device *dev,
						struct device_node *phy_np,
						void (*hndlr)(struct net_device *),
						u32 flags, phy_interface_t iface)
{
	return NULL;
}

static inline struct phy_device *of_phy_attach(struct net_device *dev,
					       struct device_node *phy_np,
					       u32 flags, phy_interface_t iface)
{
	return NULL;
}

static inline struct mii_bus *of_mdio_find_bus(struct device_node *mdio_np)
{
	return NULL;
}

static inline void of_mdiobus_link_phydev(struct mii_bus *mdio,
					  struct phy_device *phydev)
{
}
#endif /* CONFIG_OF */

#if defined(CONFIG_OF) && defined(CONFIG_FIXED_PHY)
extern int of_phy_register_fixed_link(struct device_node *np);
extern bool of_phy_is_fixed_link(struct device_node *np);
#else
static inline int of_phy_register_fixed_link(struct device_node *np)
{
	return -ENOSYS;
}
static inline bool of_phy_is_fixed_link(struct device_node *np)
{
	return false;
}
#endif


#endif /* __LINUX_OF_MDIO_H */
