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

int mac802154_slave_open(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_sub_if_data *subif;
	struct ieee802154_local *local = sdata->local;
	int res = 0;

	ASSERT_RTNL();

	if (sdata->type == IEEE802154_DEV_WPAN) {
		mutex_lock(&sdata->local->iflist_mtx);
		list_for_each_entry(subif, &sdata->local->interfaces, list) {
			if (subif != sdata && subif->type == sdata->type &&
			    subif->running) {
				mutex_unlock(&sdata->local->iflist_mtx);
				return -EBUSY;
			}
		}
		mutex_unlock(&sdata->local->iflist_mtx);
	}

	mutex_lock(&sdata->local->iflist_mtx);
	sdata->running = true;
	mutex_unlock(&sdata->local->iflist_mtx);

	if (local->open_count++ == 0) {
		res = local->ops->start(&local->hw);
		WARN_ON(res);
		if (res)
			goto err;
	}

	if (local->ops->ieee_addr) {
		__le64 addr = ieee802154_devaddr_from_raw(dev->dev_addr);

		res = local->ops->ieee_addr(&local->hw, addr);
		WARN_ON(res);
		if (res)
			goto err;
		mac802154_dev_set_ieee_addr(dev);
	}

	netif_start_queue(dev);
	return 0;
err:
	sdata->local->open_count--;

	return res;
}

int mac802154_slave_close(struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	struct ieee802154_local *local = sdata->local;

	ASSERT_RTNL();

	netif_stop_queue(dev);

	mutex_lock(&sdata->local->iflist_mtx);
	sdata->running = false;
	mutex_unlock(&sdata->local->iflist_mtx);

	if (!--local->open_count)
		local->ops->stop(&local->hw);

	return 0;
}

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

	mutex_lock(&local->iflist_mtx);
	if (!local->running) {
		mutex_unlock(&local->iflist_mtx);
		return -ENODEV;
	}
	mutex_unlock(&local->iflist_mtx);

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

static int mac802154_set_txpower(struct wpan_phy *phy, int db)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_txpower(&local->hw, db);
}

static int mac802154_set_lbt(struct wpan_phy *phy, bool on)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_lbt(&local->hw, on);
}

static int mac802154_set_cca_mode(struct wpan_phy *phy, u8 mode)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_cca_mode(&local->hw, mode);
}

static int mac802154_set_cca_ed_level(struct wpan_phy *phy, s32 level)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_cca_ed_level(&local->hw, level);
}

static int mac802154_set_csma_params(struct wpan_phy *phy, u8 min_be,
				     u8 max_be, u8 retries)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_csma_params(&local->hw, min_be, max_be, retries);
}

static int mac802154_set_frame_retries(struct wpan_phy *phy, s8 retries)
{
	struct ieee802154_local *local = wpan_phy_priv(phy);

	return local->ops->set_frame_retries(&local->hw, retries);
}

struct ieee802154_hw *
ieee802154_alloc_hw(size_t priv_data_len, struct ieee802154_ops *ops)
{
	struct wpan_phy *phy;
	struct ieee802154_local *local;
	size_t priv_size;

	if (!ops || !ops->xmit || !ops->ed || !ops->start ||
	    !ops->stop || !ops->set_channel) {
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

	phy = wpan_phy_alloc(priv_size);
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

	if (hw->flags & IEEE802154_HW_TXPOWER) {
		if (!local->ops->set_txpower)
			goto out;

		local->phy->set_txpower = mac802154_set_txpower;
	}

	if (hw->flags & IEEE802154_HW_LBT) {
		if (!local->ops->set_lbt)
			goto out;

		local->phy->set_lbt = mac802154_set_lbt;
	}

	if (hw->flags & IEEE802154_HW_CCA_MODE) {
		if (!local->ops->set_cca_mode)
			goto out;

		local->phy->set_cca_mode = mac802154_set_cca_mode;
	}

	if (hw->flags & IEEE802154_HW_CCA_ED_LEVEL) {
		if (!local->ops->set_cca_ed_level)
			goto out;

		local->phy->set_cca_ed_level = mac802154_set_cca_ed_level;
	}

	if (hw->flags & IEEE802154_HW_CSMA_PARAMS) {
		if (!local->ops->set_csma_params)
			goto out;

		local->phy->set_csma_params = mac802154_set_csma_params;
	}

	if (hw->flags & IEEE802154_HW_FRAME_RETRIES) {
		if (!local->ops->set_frame_retries)
			goto out;

		local->phy->set_frame_retries = mac802154_set_frame_retries;
	}

	local->dev_workqueue =
		create_singlethread_workqueue(wpan_phy_name(local->phy));
	if (!local->dev_workqueue) {
		rc = -ENOMEM;
		goto out;
	}

	wpan_phy_set_dev(local->phy, local->hw.parent);

	local->phy->add_iface = mac802154_add_iface;
	local->phy->del_iface = mac802154_del_iface;

	rc = wpan_phy_register(local->phy);
	if (rc < 0)
		goto out_wq;

	rtnl_lock();

	mutex_lock(&local->iflist_mtx);
	local->running = MAC802154_DEVICE_RUN;
	mutex_unlock(&local->iflist_mtx);

	rtnl_unlock();

	return 0;

out_wq:
	destroy_workqueue(local->dev_workqueue);
out:
	return rc;
}
EXPORT_SYMBOL(ieee802154_register_hw);

void ieee802154_unregister_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata, *next;

	flush_workqueue(local->dev_workqueue);
	destroy_workqueue(local->dev_workqueue);

	rtnl_lock();

	mutex_lock(&local->iflist_mtx);
	local->running = MAC802154_DEVICE_STOPPED;
	mutex_unlock(&local->iflist_mtx);

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
