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

#include <net/mac802154.h>
#include <net/route.h>
#include <net/wpan-phy.h>

#include "mac802154.h"

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

	wpan_phy_free(priv->phy);

	mutex_destroy(&priv->slaves_mtx);
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

	flush_workqueue(priv->dev_workqueue);
	destroy_workqueue(priv->dev_workqueue);

	rtnl_lock();

	mutex_lock(&priv->slaves_mtx);
	priv->running = MAC802154_DEVICE_STOPPED;
	mutex_unlock(&priv->slaves_mtx);

	rtnl_unlock();

	wpan_phy_unregister(priv->phy);
}
EXPORT_SYMBOL(ieee802154_unregister_device);

MODULE_DESCRIPTION("IEEE 802.15.4 implementation");
MODULE_LICENSE("GPL v2");
