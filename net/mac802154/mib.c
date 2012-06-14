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
