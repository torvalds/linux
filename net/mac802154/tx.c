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
struct wpan_xmit_cb {
	struct sk_buff *skb;
	struct work_struct work;
	struct ieee802154_local *local;
};

static inline struct wpan_xmit_cb *wpan_xmit_cb(const struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct wpan_xmit_cb));

	return (struct wpan_xmit_cb *)skb->cb;
}

static void mac802154_xmit_worker(struct work_struct *work)
{
	struct wpan_xmit_cb *cb = container_of(work, struct wpan_xmit_cb, work);
	struct ieee802154_local *local = cb->local;
	struct ieee802154_sub_if_data *sdata;
	struct sk_buff *skb = cb->skb;
	int res;

	res = local->ops->xmit(&local->hw, skb);
	if (res)
		pr_debug("transmission failed\n");

	/* Restart the netif queue on each sub_if_data object. */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		netif_wake_queue(sdata->dev);
	rcu_read_unlock();

	dev_kfree_skb(skb);
}

static netdev_tx_t
mac802154_tx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct ieee802154_sub_if_data *sdata;
	struct wpan_xmit_cb *cb = wpan_xmit_cb(skb);

	mac802154_monitors_rx(local, skb);

	if (!(local->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		u16 crc = crc_ccitt(0, skb->data, skb->len);
		u8 *data = skb_put(skb, 2);

		data[0] = crc & 0xff;
		data[1] = crc >> 8;
	}

	if (skb_cow_head(skb, local->hw.extra_tx_headroom))
		goto err_tx;

	/* Stop the netif queue on each sub_if_data object. */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		netif_stop_queue(sdata->dev);
	rcu_read_unlock();

	INIT_WORK(&cb->work, mac802154_xmit_worker);
	cb->skb = skb;
	cb->local = local;

	queue_work(local->workqueue, &cb->work);

	return NETDEV_TX_OK;

err_tx:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

netdev_tx_t mac802154_monitor_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb);
}

netdev_tx_t mac802154_wpan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int rc;

	rc = mac802154_llsec_encrypt(&sdata->sec, skb);
	if (rc) {
		pr_warn("encryption failed: %i\n", rc);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return mac802154_tx(sdata->local, skb);
}
