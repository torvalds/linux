/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/mac80211/cfg.c
 */

#include <net/rtnetlink.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"
#include "cfg.h"

static struct net_device *
ieee802154_add_iface_deprecated(struct wpan_phy *wpan_phy,
				const char *name, int type)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	struct net_device *dev;

	rtnl_lock();
	dev = ieee802154_if_add(local, name, NULL, type);
	rtnl_unlock();

	return dev;
}

static void ieee802154_del_iface_deprecated(struct wpan_phy *wpan_phy,
					    struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	ieee802154_if_remove(sdata);
}

static int
ieee802154_set_channel(struct wpan_phy *wpan_phy, const u8 page,
		       const u8 channel)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);
	int ret;

	ASSERT_RTNL();

	/* check if phy support this setting */
	if (!(wpan_phy->channels_supported[page] & BIT(channel)))
		return -EINVAL;

	ret = drv_set_channel(local, page, channel);
	if (!ret) {
		wpan_phy->current_page = page;
		wpan_phy->current_channel = channel;
	}

	return ret;
}

static int ieee802154_set_pan_id(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev, const u16 pan_id)
{
	ASSERT_RTNL();

	/* TODO
	 * I am not sure about to check here on broadcast pan_id.
	 * Broadcast is a valid setting, comment from 802.15.4:
	 * If this value is 0xffff, the device is not associated.
	 *
	 * This could useful to simple deassociate an device.
	 */
	if (pan_id == IEEE802154_PAN_ID_BROADCAST)
		return -EINVAL;

	wpan_dev->pan_id = cpu_to_le16(pan_id);
	return 0;
}

static int
ieee802154_set_backoff_exponent(struct wpan_phy *wpan_phy,
				struct wpan_dev *wpan_dev,
				const u8 min_be, const u8 max_be)
{
	struct ieee802154_local *local = wpan_phy_priv(wpan_phy);

	ASSERT_RTNL();

	if (!(local->hw.flags & IEEE802154_HW_CSMA_PARAMS))
		return -EOPNOTSUPP;

	wpan_dev->min_be = min_be;
	wpan_dev->max_be = max_be;
	return 0;
}

static int
ieee802154_set_short_addr(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
			  const u16 short_addr)
{
	ASSERT_RTNL();

	/* TODO
	 * I am not sure about to check here on broadcast short_addr.
	 * Broadcast is a valid setting, comment from 802.15.4:
	 * A value of 0xfffe indicates that the device has
	 * associated but has not been allocated an address. A
	 * value of 0xffff indicates that the device does not
	 * have a short address.
	 *
	 * I think we should allow to set these settings but
	 * don't allow to allow socket communication with it.
	 */
	if (short_addr == IEEE802154_ADDR_SHORT_UNSPEC ||
	    short_addr == IEEE802154_ADDR_SHORT_BROADCAST)
		return -EINVAL;

	wpan_dev->short_addr = cpu_to_le16(short_addr);
	return 0;
}

const struct cfg802154_ops mac802154_config_ops = {
	.add_virtual_intf_deprecated = ieee802154_add_iface_deprecated,
	.del_virtual_intf_deprecated = ieee802154_del_iface_deprecated,
	.set_channel = ieee802154_set_channel,
	.set_pan_id = ieee802154_set_pan_id,
	.set_short_addr = ieee802154_set_short_addr,
	.set_backoff_exponent = ieee802154_set_backoff_exponent,
};
