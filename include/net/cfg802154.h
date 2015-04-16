/*
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 */

#ifndef __NET_CFG802154_H
#define __NET_CFG802154_H

#include <linux/ieee802154.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/bug.h>

#include <net/nl802154.h>

struct wpan_phy;
struct wpan_phy_cca;

struct cfg802154_ops {
	struct net_device * (*add_virtual_intf_deprecated)(struct wpan_phy *wpan_phy,
							   const char *name,
							   int type);
	void	(*del_virtual_intf_deprecated)(struct wpan_phy *wpan_phy,
					       struct net_device *dev);
	int	(*add_virtual_intf)(struct wpan_phy *wpan_phy,
				    const char *name,
				    enum nl802154_iftype type,
				    __le64 extended_addr);
	int	(*del_virtual_intf)(struct wpan_phy *wpan_phy,
				    struct wpan_dev *wpan_dev);
	int	(*set_channel)(struct wpan_phy *wpan_phy, u8 page, u8 channel);
	int	(*set_cca_mode)(struct wpan_phy *wpan_phy,
				const struct wpan_phy_cca *cca);
	int	(*set_pan_id)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev, __le16 pan_id);
	int	(*set_short_addr)(struct wpan_phy *wpan_phy,
				  struct wpan_dev *wpan_dev, __le16 short_addr);
	int	(*set_backoff_exponent)(struct wpan_phy *wpan_phy,
					struct wpan_dev *wpan_dev, u8 min_be,
					u8 max_be);
	int	(*set_max_csma_backoffs)(struct wpan_phy *wpan_phy,
					 struct wpan_dev *wpan_dev,
					 u8 max_csma_backoffs);
	int	(*set_max_frame_retries)(struct wpan_phy *wpan_phy,
					 struct wpan_dev *wpan_dev,
					 s8 max_frame_retries);
	int	(*set_lbt_mode)(struct wpan_phy *wpan_phy,
				struct wpan_dev *wpan_dev, bool mode);
};

struct wpan_phy_cca {
	enum nl802154_cca_modes mode;
	enum nl802154_cca_opts opt;
};

struct wpan_phy {
	struct mutex pib_lock;

	/* If multiple wpan_phys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee802154_ptr, you won't
	 * know whether it points to a wpan_phy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wpan_phy or not.
	 */
	const void *privid;

	/*
	 * This is a PIB according to 802.15.4-2011.
	 * We do not provide timing-related variables, as they
	 * aren't used outside of driver
	 */
	u8 current_channel;
	u8 current_page;
	u32 channels_supported[IEEE802154_MAX_PAGE + 1];
	s8 transmit_power;
	struct wpan_phy_cca cca;

	__le64 perm_extended_addr;

	s32 cca_ed_level;

	/* PHY depended MAC PIB values */

	/* 802.15.4 acronym: Tdsym in usec */
	u8 symbol_duration;
	/* lifs and sifs periods timing */
	u16 lifs_period;
	u16 sifs_period;

	struct device dev;

	char priv[0] __aligned(NETDEV_ALIGN);
};

struct wpan_dev {
	struct wpan_phy *wpan_phy;
	int iftype;

	/* the remainder of this struct should be private to cfg802154 */
	struct list_head list;
	struct net_device *netdev;

	u32 identifier;

	/* MAC PIB */
	__le16 pan_id;
	__le16 short_addr;
	__le64 extended_addr;

	/* MAC BSN field */
	u8 bsn;
	/* MAC DSN field */
	u8 dsn;

	u8 min_be;
	u8 max_be;
	u8 csma_retries;
	s8 frame_retries;

	bool lbt;

	bool promiscuous_mode;
};

#define to_phy(_dev)	container_of(_dev, struct wpan_phy, dev)

struct wpan_phy *
wpan_phy_new(const struct cfg802154_ops *ops, size_t priv_size);
static inline void wpan_phy_set_dev(struct wpan_phy *phy, struct device *dev)
{
	phy->dev.parent = dev;
}

int wpan_phy_register(struct wpan_phy *phy);
void wpan_phy_unregister(struct wpan_phy *phy);
void wpan_phy_free(struct wpan_phy *phy);
/* Same semantics as for class_for_each_device */
int wpan_phy_for_each(int (*fn)(struct wpan_phy *phy, void *data), void *data);

static inline void *wpan_phy_priv(struct wpan_phy *phy)
{
	BUG_ON(!phy);
	return &phy->priv;
}

struct wpan_phy *wpan_phy_find(const char *str);

static inline void wpan_phy_put(struct wpan_phy *phy)
{
	put_device(&phy->dev);
}

static inline const char *wpan_phy_name(struct wpan_phy *phy)
{
	return dev_name(&phy->dev);
}

#endif /* __NET_CFG802154_H */
