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

static int
mac802154_netdev_register(struct wpan_phy *phy, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local;
	int err;

	local = wpan_phy_priv(phy);

	sdata->dev = dev;
	sdata->local = local;

	dev->needed_headroom = local->hw.extra_tx_headroom;

	SET_NETDEV_DEV(dev, &local->phy->dev);

	err = register_netdev(dev);
	if (err < 0)
		return err;

	rtnl_lock();
	mutex_lock(&local->iflist_mtx);
	list_add_tail_rcu(&sdata->list, &local->interfaces);
	mutex_unlock(&local->iflist_mtx);
	rtnl_unlock();

	return 0;
}

static void
mac802154_del_iface(struct wpan_phy *phy, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	ASSERT_RTNL();

	BUG_ON(sdata->local->phy != phy);

	mutex_lock(&sdata->local->iflist_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->local->iflist_mtx);

	synchronize_rcu();
	unregister_netdevice(sdata->dev);
}

static struct net_device *
mac802154_add_iface(struct wpan_phy *phy, const char *name, int type)
{
	struct net_device *dev;
	int err = -ENOMEM;

	switch (type) {
	case IEEE802154_DEV_MONITOR:
		dev = alloc_netdev(sizeof(struct ieee802154_sub_if_data),
				   name, NET_NAME_UNKNOWN,
				   mac802154_monitor_setup);
		break;
	case IEEE802154_DEV_WPAN:
		dev = alloc_netdev(sizeof(struct ieee802154_sub_if_data),
				   name, NET_NAME_UNKNOWN,
				   mac802154_wpan_setup);
		break;
	default:
		dev = NULL;
		err = -EINVAL;
		break;
	}
	if (!dev)
		goto err;

	err = mac802154_netdev_register(phy, dev);
	if (err)
		goto err_free;

	dev_hold(dev); /* we return an incremented device refcount */
	return dev;

err_free:
	free_netdev(dev);
err:
	return ERR_PTR(err);
}

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

	local->phy->add_iface = mac802154_add_iface;
	local->phy->del_iface = mac802154_del_iface;

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
