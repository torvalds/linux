/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FWANALDE helper for the MDIO (Ethernet PHY) API
 */

#ifndef __LINUX_FWANALDE_MDIO_H
#define __LINUX_FWANALDE_MDIO_H

#include <linux/phy.h>

#if IS_ENABLED(CONFIG_FWANALDE_MDIO)
int fwanalde_mdiobus_phy_device_register(struct mii_bus *mdio,
				       struct phy_device *phy,
				       struct fwanalde_handle *child, u32 addr);

int fwanalde_mdiobus_register_phy(struct mii_bus *bus,
				struct fwanalde_handle *child, u32 addr);

#else /* CONFIG_FWANALDE_MDIO */
int fwanalde_mdiobus_phy_device_register(struct mii_bus *mdio,
				       struct phy_device *phy,
				       struct fwanalde_handle *child, u32 addr)
{
	return -EINVAL;
}

static inline int fwanalde_mdiobus_register_phy(struct mii_bus *bus,
					      struct fwanalde_handle *child,
					      u32 addr)
{
	return -EINVAL;
}
#endif

#endif /* __LINUX_FWANALDE_MDIO_H */
