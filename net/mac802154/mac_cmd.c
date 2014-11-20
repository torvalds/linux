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

#include <net/ieee802154.h>
#include <net/ieee802154_netdev.h>
#include <net/wpan-phy.h>
#include <net/mac802154.h>
#include <net/nl802154.h>

#include "mac802154.h"

static int mac802154_mlme_start_req(struct net_device *dev,
				    struct ieee802154_addr *addr,
				    u8 channel, u8 page,
				    u8 bcn_ord, u8 sf_ord,
				    u8 pan_coord, u8 blx,
				    u8 coord_realign)
{
	struct ieee802154_mlme_ops *ops = ieee802154_mlme_ops(dev);
	int rc = 0;

	BUG_ON(addr->mode != IEEE802154_ADDR_SHORT);

	mac802154_dev_set_pan_id(dev, addr->pan_id);
	mac802154_dev_set_short_addr(dev, addr->short_addr);
	mac802154_dev_set_ieee_addr(dev);
	mac802154_dev_set_page_channel(dev, page, channel);

	if (ops->llsec) {
		struct ieee802154_llsec_params params;
		int changed = 0;

		params.coord_shortaddr = addr->short_addr;
		changed |= IEEE802154_LLSEC_PARAM_COORD_SHORTADDR;

		params.pan_id = addr->pan_id;
		changed |= IEEE802154_LLSEC_PARAM_PAN_ID;

		params.hwaddr = ieee802154_devaddr_from_raw(dev->dev_addr);
		changed |= IEEE802154_LLSEC_PARAM_HWADDR;

		params.coord_hwaddr = params.hwaddr;
		changed |= IEEE802154_LLSEC_PARAM_COORD_HWADDR;

		rc = ops->llsec->set_params(dev, &params, changed);
	}

	/* FIXME: add validation for unused parameters to be sane
	 * for SoftMAC
	 */
	ieee802154_nl_start_confirm(dev, IEEE802154_SUCCESS);

	return rc;
}

static struct wpan_phy *mac802154_get_phy(const struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return to_phy(get_device(&priv->hw->phy->dev));
}

static struct ieee802154_llsec_ops mac802154_llsec_ops = {
	.get_params = mac802154_get_params,
	.set_params = mac802154_set_params,
	.add_key = mac802154_add_key,
	.del_key = mac802154_del_key,
	.add_dev = mac802154_add_dev,
	.del_dev = mac802154_del_dev,
	.add_devkey = mac802154_add_devkey,
	.del_devkey = mac802154_del_devkey,
	.add_seclevel = mac802154_add_seclevel,
	.del_seclevel = mac802154_del_seclevel,
	.lock_table = mac802154_lock_table,
	.get_table = mac802154_get_table,
	.unlock_table = mac802154_unlock_table,
};

struct ieee802154_reduced_mlme_ops mac802154_mlme_reduced = {
	.get_phy = mac802154_get_phy,
};

struct ieee802154_mlme_ops mac802154_mlme_wpan = {
	.get_phy = mac802154_get_phy,
	.start_req = mac802154_mlme_start_req,
	.get_pan_id = mac802154_dev_get_pan_id,
	.get_short_addr = mac802154_dev_get_short_addr,
	.get_dsn = mac802154_dev_get_dsn,

	.llsec = &mac802154_llsec_ops,

	.set_mac_params = mac802154_set_mac_params,
	.get_mac_params = mac802154_get_mac_params,
};
