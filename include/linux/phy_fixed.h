/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PHY_FIXED_H
#define __PHY_FIXED_H

#include <linux/types.h>

struct fixed_phy_status {
	int speed;
	int duplex;
	bool link:1;
	bool pause:1;
	bool asym_pause:1;
};

struct device_node;
struct net_device;

#if IS_ENABLED(CONFIG_FIXED_PHY)
extern int fixed_phy_change_carrier(struct net_device *dev, bool new_carrier);
struct phy_device *fixed_phy_register(const struct fixed_phy_status *status,
				      struct device_node *np);
struct phy_device *fixed_phy_register_100fd(void);

extern void fixed_phy_unregister(struct phy_device *phydev);
extern int fixed_phy_set_link_update(struct phy_device *phydev,
			int (*link_update)(struct net_device *,
					   struct fixed_phy_status *));
#else
static inline struct phy_device *
fixed_phy_register(const struct fixed_phy_status *status,
		   struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline struct phy_device *fixed_phy_register_100fd(void)
{
	return ERR_PTR(-ENODEV);
}

static inline void fixed_phy_unregister(struct phy_device *phydev)
{
}
#endif /* CONFIG_FIXED_PHY */

#endif /* __PHY_FIXED_H */
