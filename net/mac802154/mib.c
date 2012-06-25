/*
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
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Sergey Lapin <slapin@ossfans.org>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/if_arp.h>

#include <net/mac802154.h>
#include <net/wpan-phy.h>

#include "mac802154.h"

struct phy_chan_notify_work {
	struct work_struct work;
	struct net_device *dev;
};

struct hw_addr_filt_notify_work {
	struct work_struct work;
	struct net_device *dev;
	unsigned long changed;
};

struct mac802154_priv *mac802154_slave_get_priv(struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	return priv->hw;
}

static void hw_addr_notify(struct work_struct *work)
{
	struct hw_addr_filt_notify_work *nw = container_of(work,
			struct hw_addr_filt_notify_work, work);
	struct mac802154_priv *hw = mac802154_slave_get_priv(nw->dev);
	int res;

	res = hw->ops->set_hw_addr_filt(&hw->hw,
					&hw->hw.hw_filt,
					nw->changed);
	if (res)
		pr_debug("failed changed mask %lx\n", nw->changed);

	kfree(nw);

	return;
}

static void set_hw_addr_filt(struct net_device *dev, unsigned long changed)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct hw_addr_filt_notify_work *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	INIT_WORK(&work->work, hw_addr_notify);
	work->dev = dev;
	work->changed = changed;
	queue_work(priv->hw->dev_workqueue, &work->work);

	return;
}

void mac802154_dev_set_short_addr(struct net_device *dev, u16 val)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&priv->mib_lock);
	priv->short_addr = val;
	spin_unlock_bh(&priv->mib_lock);

	if ((priv->hw->ops->set_hw_addr_filt) &&
	    (priv->hw->hw.hw_filt.short_addr != priv->short_addr)) {
		priv->hw->hw.hw_filt.short_addr = priv->short_addr;
		set_hw_addr_filt(dev, IEEE802515_AFILT_SADDR_CHANGED);
	}
}

void mac802154_dev_set_ieee_addr(struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct mac802154_priv *mac = priv->hw;

	if (mac->ops->set_hw_addr_filt &&
	    memcmp(mac->hw.hw_filt.ieee_addr,
		   dev->dev_addr, IEEE802154_ADDR_LEN)) {
		memcpy(mac->hw.hw_filt.ieee_addr,
		       dev->dev_addr, IEEE802154_ADDR_LEN);
		set_hw_addr_filt(dev, IEEE802515_AFILT_IEEEADDR_CHANGED);
	}
}

u16 mac802154_dev_get_pan_id(const struct net_device *dev)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	u16 ret;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&priv->mib_lock);
	ret = priv->pan_id;
	spin_unlock_bh(&priv->mib_lock);

	return ret;
}

void mac802154_dev_set_pan_id(struct net_device *dev, u16 val)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&priv->mib_lock);
	priv->pan_id = val;
	spin_unlock_bh(&priv->mib_lock);

	if ((priv->hw->ops->set_hw_addr_filt) &&
	    (priv->hw->hw.hw_filt.pan_id != priv->pan_id)) {
		priv->hw->hw.hw_filt.pan_id = priv->pan_id;
		set_hw_addr_filt(dev, IEEE802515_AFILT_PANID_CHANGED);
	}
}

static void phy_chan_notify(struct work_struct *work)
{
	struct phy_chan_notify_work *nw = container_of(work,
					  struct phy_chan_notify_work, work);
	struct mac802154_priv *hw = mac802154_slave_get_priv(nw->dev);
	struct mac802154_sub_if_data *priv = netdev_priv(nw->dev);
	int res;

	res = hw->ops->set_channel(&hw->hw, priv->page, priv->chan);
	if (res)
		pr_debug("set_channel failed\n");

	kfree(nw);
}

void mac802154_dev_set_page_channel(struct net_device *dev, u8 page, u8 chan)
{
	struct mac802154_sub_if_data *priv = netdev_priv(dev);
	struct phy_chan_notify_work *work;

	BUG_ON(dev->type != ARPHRD_IEEE802154);

	spin_lock_bh(&priv->mib_lock);
	priv->page = page;
	priv->chan = chan;
	spin_unlock_bh(&priv->mib_lock);

	if (priv->hw->phy->current_channel != priv->chan ||
	    priv->hw->phy->current_page != priv->page) {
		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (!work)
			return;

		INIT_WORK(&work->work, phy_chan_notify);
		work->dev = dev;
		queue_work(priv->hw->dev_workqueue, &work->work);
	}
}
