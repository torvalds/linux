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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include <net/netlink.h>
#include <linux/nl802154.h>
#include <net/mac802154.h>
#include <net/route.h>
#include <net/wpan-phy.h>

#include "mac802154.h"

int mac802154_slave_open(struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct mac802154_priv *ipriv = priv->hw;
	int res = 0;

	if (ipriv->open_count++ == 0) {
		res = ipriv->ops->start(&ipriv->hw);
		WARN_ON(res);
		if (res)
			goto err;
	}

	if (ipriv->ops->ieee_addr) {
		res = ipriv->ops->ieee_addr(&ipriv->hw, dev->dev_addr);
		WARN_ON(res);
		if (res)
			goto err;
		mac802154_dev_set_ieee_addr(dev);
	}

	netif_start_queue(dev);
	return 0;
err:
	priv->hw->open_count--;

	return res;
}

int mac802154_slave_close(struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct mac802154_priv *ipriv = priv->hw;

	netif_stop_queue(dev);

	if (!--ipriv->open_count)
		ipriv->ops->stop(&ipriv->hw);

	return 0;
}

static int
mac802154_netdev_register(struct wpan_phy *phy, struct net_device *dev)
{
	struct mac802154_sub_if_data *priv;
	struct mac802154_priv *ipriv;
	int err;

	ipriv = wpan_phy_priv(phy);

	priv = netdev_priv(dev);
	priv->dev = dev;
	priv->hw = ipriv;

	dev->needed_headroom = ipriv->hw.extra_tx_headroom;

	SET_NETDEV_DEV(dev, &ipriv->phy->dev);

	mutex_lock(&ipriv->slaves_mtx);
	if (!ipriv->running) {
		mutex_unlock(&ipriv->slaves_mtx);
		return -ENODEV;
	}
	mutex_unlock(&ipriv->slaves_mtx);

	err = register_netdev(dev);
	if (err < 0)
		return err;

	rtnl_lock();
	mutex_lock(&ipriv->slaves_mtx);
	list_add_tail_rcu(&priv->list, &ipriv->slaves);
	mutex_unlock(&ipriv->slaves_mtx);
	rtnl_unlock();

	return 0;
}

static void
mac802154_del_iface(struct wpan_phy *phy, struct net_device *dev)
{
	struct mac802154_sub_if_data *sdata;
	ASSERT_RTNL();

	sdata = netdev_priv(dev);

	BUG_ON(sdata->hw->phy != phy);

	mutex_lock(&sdata->hw->slaves_mtx);
	list_del_rcu(&sdata->list);
	mutex_unlock(&sdata->hw->slaves_mtx);

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
		dev = alloc_netdev(sizeof(struct mac802154_sub_if_data),
				   name, mac802154_monitor_setup);
		break;
	case IEEE802154_DEV_WPAN:
		dev = alloc_netdev(sizeof(struct mac802154_sub_if_data),
				   name, mac802154_wpan_setup);
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

struct ieee802154_dev *
ieee802154_alloc_device(size_t priv_data_len, struct ieee802154_ops *ops)
{
	struct wpan_phy *phy;
	struct mac802154_priv *priv;
	size_t priv_size;

	if (!ops || !ops->xmit || !ops->ed || !ops->start ||
	    !ops->stop || !ops->set_channel) {
		printk(KERN_ERR
		       "undefined IEEE802.15.4 device operations\n");
		return NULL;
	}

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wpan_phy priv data for both our mac802154_priv and for
	 * the driver's private data
	 *
	 * in memory it'll be like this:
	 *
	 * +-----------------------+
	 * | struct wpan_phy       |
	 * +-----------------------+
	 * | struct mac802154_priv |
	 * +-----------------------+
	 * | driver's private data |
	 * +-----------------------+
	 *
	 * Due to ieee802154 layer isn't aware of driver and MAC structures,
	 * so lets allign them here.
	 */

	priv_size = ALIGN(sizeof(*priv), NETDEV_ALIGN) + priv_data_len;

	phy = wpan_phy_alloc(priv_size);
	if (!phy) {
		printk(KERN_ERR
		       "failure to allocate master IEEE802.15.4 device\n");
		return NULL;
	}

	priv = wpan_phy_priv(phy);
	priv->hw.phy = priv->phy = phy;
	priv->hw.priv = (char *)priv + ALIGN(sizeof(*priv), NETDEV_ALIGN);
	priv->ops = ops;

	INIT_LIST_HEAD(&priv->slaves);
	mutex_init(&priv->slaves_mtx);

	return &priv->hw;
}
EXPORT_SYMBOL(ieee802154_alloc_device);

void ieee802154_free_device(struct ieee802154_dev *hw)
{
	struct mac802154_priv *priv = mac802154_to_priv(hw);

	BUG_ON(!list_empty(&priv->slaves));

	mutex_destroy(&priv->slaves_mtx);

	wpan_phy_free(priv->phy);
}
EXPORT_SYMBOL(ieee802154_free_device);

int ieee802154_register_device(struct ieee802154_dev *dev)
{
	struct mac802154_priv *priv = mac802154_to_priv(dev);
	int rc = -ENOMEM;

	priv->dev_workqueue =
		create_singlethread_workqueue(wpan_phy_name(priv->phy));
	if (!priv->dev_workqueue)
		goto out;

	wpan_phy_set_dev(priv->phy, priv->hw.parent);

	priv->phy->add_iface = mac802154_add_iface;
	priv->phy->del_iface = mac802154_del_iface;

	rc = wpan_phy_register(priv->phy);
	if (rc < 0)
		goto out_wq;

	rtnl_lock();

	mutex_lock(&priv->slaves_mtx);
	priv->running = MAC802154_DEVICE_RUN;
	mutex_unlock(&priv->slaves_mtx);

	rtnl_unlock();

	return 0;

out_wq:
	destroy_workqueue(priv->dev_workqueue);
out:
	return rc;
}
EXPORT_SYMBOL(ieee802154_register_device);

void ieee802154_unregister_device(struct ieee802154_dev *dev)
{
	struct mac802154_priv *priv = mac802154_to_priv(dev);
	struct mac802154_sub_if_data *sdata, *next;

	flush_workqueue(priv->dev_workqueue);
	destroy_workqueue(priv->dev_workqueue);

	rtnl_lock();

	mutex_lock(&priv->slaves_mtx);
	priv->running = MAC802154_DEVICE_STOPPED;
	mutex_unlock(&priv->slaves_mtx);

	list_for_each_entry_safe(sdata, next, &priv->slaves, list) {
		mutex_lock(&sdata->hw->slaves_mtx);
		list_del(&sdata->list);
		mutex_unlock(&sdata->hw->slaves_mtx);

		unregister_netdevice(sdata->dev);
	}

	rtnl_unlock();

	wpan_phy_unregister(priv->phy);
}
EXPORT_SYMBOL(ieee802154_unregister_device);

MODULE_DESCRIPTION("IEEE 802.15.4 implementation");
MODULE_LICENSE("GPL v2");
