/*
 * MAC commands interface
 *
 * Copyright 2007-2012 Siemens AG
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <net/ieee802154_netdev.h>
#include <net/wpan-phy.h>
#include <net/mac802154.h>

#include "mac802154.h"

struct wpan_phy *mac802154_get_phy(const struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return to_phy(get_device(&priv->hw->phy->dev));
}

struct ieee802154_reduced_mlme_ops mac802154_mlme_reduced = {
	.get_phy = mac802154_get_phy,
};
