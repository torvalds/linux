// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAC commands interface
 *
 * Copyright 2007-2012 Siemens AG
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ieee802154.h>

#include <net/ieee802154_netdev.h>
#include <net/cfg802154.h>
#include <net/mac802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

static int mac802154_mlme_start_req(struct net_device *dev,
				    struct ieee802154_addr *addr,
				    u8 channel, u8 page,
				    u8 bcn_ord, u8 sf_ord,
				    u8 pan_coord, u8 blx,
				    u8 coord_realign)
{
	struct ieee802154_llsec_params params;
	int changed = 0;

	ASSERT_RTNL();

	BUG_ON(addr->mode != IEEE802154_ADDR_SHORT);

	dev->ieee802154_ptr->pan_id = addr->pan_id;
	dev->ieee802154_ptr->short_addr = addr->short_addr;
	mac802154_dev_set_page_channel(dev, page, channel);

	params.pan_id = addr->pan_id;
	changed |= IEEE802154_LLSEC_PARAM_PAN_ID;

	params.hwaddr = ieee802154_devaddr_from_raw(dev->dev_addr);
	changed |= IEEE802154_LLSEC_PARAM_HWADDR;

	params.coord_hwaddr = params.hwaddr;
	changed |= IEEE802154_LLSEC_PARAM_COORD_HWADDR;

	params.coord_shortaddr = addr->short_addr;
	changed |= IEEE802154_LLSEC_PARAM_COORD_SHORTADDR;

	return mac802154_set_params(dev, &params, changed);
}

static int mac802154_set_mac_params(struct net_device *dev,
				    const struct ieee802154_mac_params *params)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	int ret;

	ASSERT_RTNL();

	/* PHY */
	wpan_dev->wpan_phy->transmit_power = params->transmit_power;
	wpan_dev->wpan_phy->cca = params->cca;
	wpan_dev->wpan_phy->cca_ed_level = params->cca_ed_level;

	/* MAC */
	wpan_dev->min_be = params->min_be;
	wpan_dev->max_be = params->max_be;
	wpan_dev->csma_retries = params->csma_retries;
	wpan_dev->frame_retries = params->frame_retries;
	wpan_dev->lbt = params->lbt;

	if (local->hw.phy->flags & WPAN_PHY_FLAG_TXPOWER) {
		ret = drv_set_tx_power(local, params->transmit_power);
		if (ret < 0)
			return ret;
	}

	if (local->hw.phy->flags & WPAN_PHY_FLAG_CCA_MODE) {
		ret = drv_set_cca_mode(local, &params->cca);
		if (ret < 0)
			return ret;
	}

	if (local->hw.phy->flags & WPAN_PHY_FLAG_CCA_ED_LEVEL) {
		ret = drv_set_cca_ed_level(local, params->cca_ed_level);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void mac802154_get_mac_params(struct net_device *dev,
				     struct ieee802154_mac_params *params)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;

	ASSERT_RTNL();

	/* PHY */
	params->transmit_power = wpan_dev->wpan_phy->transmit_power;
	params->cca = wpan_dev->wpan_phy->cca;
	params->cca_ed_level = wpan_dev->wpan_phy->cca_ed_level;

	/* MAC */
	params->min_be = wpan_dev->min_be;
	params->max_be = wpan_dev->max_be;
	params->csma_retries = wpan_dev->csma_retries;
	params->frame_retries = wpan_dev->frame_retries;
	params->lbt = wpan_dev->lbt;
}

static const struct ieee802154_llsec_ops mac802154_llsec_ops = {
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

struct ieee802154_mlme_ops mac802154_mlme_wpan = {
	.start_req = mac802154_mlme_start_req,

	.llsec = &mac802154_llsec_ops,

	.set_mac_params = mac802154_set_mac_params,
	.get_mac_params = mac802154_get_mac_params,
};
