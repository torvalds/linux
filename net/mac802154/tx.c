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
 * Written by:
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Sergey Lapin <slapin@ossfans.org>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/crc-ccitt.h>

#include <net/ieee802154_netdev.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"

/* IEEE 802.15.4 transceivers can sleep during the xmit session, so process
 * packets through the workqueue.
 */
struct xmit_work {
	struct sk_buff *skb;
	struct work_struct work;
	struct ieee802154_local *local;
	u8 chan;
	u8 page;
};

static void mac802154_xmit_worker(struct work_struct *work)
{
	struct xmit_work *xw = container_of(work, struct xmit_work, work);
	struct ieee802154_sub_if_data *sdata;
	int res;

	mutex_lock(&xw->local->phy->pib_lock);
	if (xw->local->phy->current_channel != xw->chan ||
	    xw->local->phy->current_page != xw->page) {
		res = xw->local->ops->set_channel(&xw->local->hw,
						  xw->page,
						  xw->chan);
		if (res) {
			pr_debug("set_channel failed\n");
			goto out;
		}

		xw->local->phy->current_channel = xw->chan;
		xw->local->phy->current_page = xw->page;
	}

	res = xw->local->ops->xmit(&xw->local->hw, xw->skb);
	if (res)
		pr_debug("transmission failed\n");

out:
	mutex_unlock(&xw->local->phy->pib_lock);

	/* Restart the netif queue on each sub_if_data object. */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &xw->local->interfaces, list)
		netif_wake_queue(sdata->dev);
	rcu_read_unlock();

	dev_kfree_skb(xw->skb);

	kfree(xw);
}

static netdev_tx_t mac802154_tx(struct ieee802154_local *local,
				struct sk_buff *skb, u8 page, u8 chan)
{
	struct xmit_work *work;
	struct ieee802154_sub_if_data *sdata;

	if (!(local->phy->channels_supported[page] & (1 << chan))) {
		WARN_ON(1);
		goto err_tx;
	}

	mac802154_monitors_rx(local, skb);

	if (!(local->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		u16 crc = crc_ccitt(0, skb->data, skb->len);
		u8 *data = skb_put(skb, 2);

		data[0] = crc & 0xff;
		data[1] = crc >> 8;
	}

	if (skb_cow_head(skb, local->hw.extra_tx_headroom))
		goto err_tx;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		kfree_skb(skb);
		return NETDEV_TX_BUSY;
	}

	/* Stop the netif queue on each sub_if_data object. */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		netif_stop_queue(sdata->dev);
	rcu_read_unlock();

	INIT_WORK(&work->work, mac802154_xmit_worker);
	work->skb = skb;
	work->local = local;
	work->page = page;
	work->chan = chan;

	queue_work(local->workqueue, &work->work);

	return NETDEV_TX_OK;

err_tx:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

netdev_tx_t mac802154_monitor_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	u8 chan, page;

	/* FIXME: locking */
	chan = sdata->local->phy->current_channel;
	page = sdata->local->phy->current_page;

	if (chan == MAC802154_CHAN_NONE) /* not initialized */
		return NETDEV_TX_OK;

	if (WARN_ON(page >= WPAN_NUM_PAGES) ||
	    WARN_ON(chan >= WPAN_NUM_CHANNELS))
		return NETDEV_TX_OK;

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb, page, chan);
}

netdev_tx_t mac802154_wpan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	u8 chan, page;
	int rc;

	spin_lock_bh(&sdata->mib_lock);
	chan = sdata->chan;
	page = sdata->page;
	spin_unlock_bh(&sdata->mib_lock);

	if (chan == MAC802154_CHAN_NONE ||
	    page >= WPAN_NUM_PAGES ||
	    chan >= WPAN_NUM_CHANNELS) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	rc = mac802154_llsec_encrypt(&sdata->sec, skb);
	if (rc) {
		pr_warn("encryption failed: %i\n", rc);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb, page, chan);
}
