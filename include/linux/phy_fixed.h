/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PHY_FIXED_H
#define __PHY_FIXED_H

#include <linux/types.h>

struct fixed_phy_status {
	int link;
	int speed;
	int duplex;
	int pause;
	int asym_pause;
};

struct device_node;
struct gpio_desc;
struct net_device;

#if IS_ENABLED(CONFIG_FIXED_PHY)
extern int fixed_phy_change_carrier(struct net_device *dev, bool new_carrier);
extern int fixed_phy_add(unsigned int irq, int phy_id,
			 struct fixed_phy_status *status);
extern struct phy_device *fixed_phy_register(unsigned int irq,
					     struct fixed_phy_status *status,
					     struct device_node *np);

extern struct phy_device *
fixed_phy_register_with_gpiod(unsigned int irq,
			      struct fixed_phy_status *status,
			      struct gpio_desc *gpiod);

extern void fixed_phy_unregister(struct phy_device *phydev);
extern int fixed_phy_set_link_update(struct phy_device *phydev,
			int (*link_update)(struct net_device *,
					   struct fixed_phy_status *));
#else
static inline int fixed_phy_add(unsigned int irq, int phy_id,
				struct fixed_phy_status *status)
{
	return -ENODEV;
}
static inline struct phy_device *fixed_phy_register(unsigned int irq,
						struct fixed_phy_status *status,
						struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline struct phy_device *
fixed_phy_register_with_gpiod(unsigned int irq,
			      struct fixed_phy_status *status,
			      struct gpio_desc *gpiod)
{
	return ERR_PTR(-ENODEV);
}

static inline void fixed_phy_unregister(struct phy_device *phydev)
{
}
static inline int fixed_phy_set_link_update(struct phy_device *phydev,
			int (*link_update)(struct net_device *,
					   struct fixed_phy_status *))
{
	return -ENODEV;
}
static inline int fixed_phy_change_carrier(struct net_device *dev, bool new_carrier)
{
	return -EINVAL;
}
#endif /* CONFIG_FIXED_PHY */

#endif /* __PHY_FIXED_H */
