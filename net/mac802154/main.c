/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * Written by:
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 *
 * Based on the code from 'linux-zigbee.sourceforge.net' project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include <net/netlink.h>
#include <linux/nl802154.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/route.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "cfg.h"

static void ieee802154_tasklet_handler(unsigned long data)
{
	struct ieee802154_local *local = (struct ieee802154_local *)data;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&local->skb_queue))) {
		switch (skb->pkt_type) {
		case IEEE802154_RX_MSG:
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack.
			 */
			skb->pkt_type = 0;
			ieee802154_rx(&local->hw, skb);
			break;
		default:
			WARN(1, "mac802154: Packet is of unknown type %d\n",
			     skb->pkt_type);
			kfree_skb(skb);
			break;
		}
	}
}

struct ieee802154_hw *
ieee802154_alloc_hw(size_t priv_data_len, const struct ieee802154_ops *ops)
{
	struct wpan_phy *phy;
	struct ieee802154_local *local;
	size_t priv_size;

	if (!ops || !(ops->xmit_async || ops->xmit_sync) || !ops->ed ||
	    !ops->start || !ops->stop || !ops->set_channel) {
		pr_err("undefined IEEE802.15.4 device operations\n");
		return NULL;
	}

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wpan_phy priv data for both our ieee802154_local and for
	 * the driver's private data
	 *
	 * in memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wpan_phy         |
	 * +-------------------------+
	 * | struct ieee802154_local |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 * Due to ieee802154 layer isn't aware of driver and MAC structures,
	 * so lets align them here.
	 */

	priv_size = ALIGN(sizeof(*local), NETDEV_ALIGN) + priv_data_len;

	phy = wpan_phy_alloc(&mac802154_config_ops, priv_size);
	if (!phy) {
		pr_err("failure to allocate master IEEE802.15.4 device\n");
		return NULL;
	}

	local = wpan_phy_priv(phy);
	local->phy = phy;
	local->hw.phy = local->phy;
	local->hw.priv = (char *)local + ALIGN(sizeof(*local), NETDEV_ALIGN);
	local->ops = ops;

	INIT_LIST_HEAD(&local->interfaces);
	mutex_init(&local->iflist_mtx);

	tasklet_init(&local->tasklet,
		     ieee802154_tasklet_handler,
		     (unsigned long)local);

	skb_queue_head_init(&local->skb_queue);

	return &local->hw;
}
EXPORT_SYMBOL(ieee802154_alloc_hw);

void ieee802154_free_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);

	BUG_ON(!list_empty(&local->interfaces));

	mutex_destroy(&local->iflist_mtx);

	wpan_phy_free(local->phy);
}
EXPORT_SYMBOL(ieee802154_free_hw);

int ieee802154_register_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	int rc = -ENOSYS;

	local->workqueue =
		create_singlethread_workqueue(wpan_phy_name(local->phy));
	if (!local->workqueue) {
		rc = -ENOMEM;
		goto out;
	}

	wpan_phy_set_dev(local->phy, local->hw.parent);

	rc = wpan_phy_register(local->phy);
	if (rc < 0)
		goto out_wq;

	return 0;

out_wq:
	destroy_workqueue(local->workqueue);
out:
	return rc;
}
EXPORT_SYMBOL(ieee802154_register_hw);

void ieee802154_unregister_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata, *next;

	tasklet_kill(&local->tasklet);
	flush_workqueue(local->workqueue);
	destroy_workqueue(local->workqueue);

	rtnl_lock();

	list_for_each_entry_safe(sdata, next, &local->interfaces, list) {
		mutex_lock(&sdata->local->iflist_mtx);
		list_del(&sdata->list);
		mutex_unlock(&sdata->local->iflist_mtx);

		unregister_netdevice(sdata->dev);
	}

	rtnl_unlock();

	wpan_phy_unregister(local->phy);
}
EXPORT_SYMBOL(ieee802154_unregister_hw);

MODULE_DESCRIPTION("IEEE 802.15.4 implementation");
MODULE_LICENSE("GPL v2");
