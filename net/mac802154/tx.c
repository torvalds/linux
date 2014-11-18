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
#include <asm/unaligned.h>

#include <net/rtnetlink.h>
#include <net/ieee802154_netdev.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

/* IEEE 802.15.4 transceivers can sleep during the xmit session, so process
 * packets through the workqueue.
 */
struct ieee802154_xmit_cb {
	struct sk_buff *skb;
	struct work_struct work;
	struct ieee802154_local *local;
};

static struct ieee802154_xmit_cb ieee802154_xmit_cb;

static void ieee802154_xmit_worker(struct work_struct *work)
{
	struct ieee802154_xmit_cb *cb =
		container_of(work, struct ieee802154_xmit_cb, work);
	struct ieee802154_local *local = cb->local;
	struct sk_buff *skb = cb->skb;
	struct net_device *dev = skb->dev;
	int res;

	rtnl_lock();

	/* check if ifdown occurred while schedule */
	if (!netif_running(dev))
		goto err_tx;

	res = drv_xmit_sync(local, skb);
	if (res)
		goto err_tx;

	ieee802154_xmit_complete(&local->hw, skb, false);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	rtnl_unlock();

	return;

err_tx:
	/* Restart the netif queue on each sub_if_data object. */
	ieee802154_wake_queue(&local->hw);
	rtnl_unlock();
	kfree_skb(skb);
	netdev_dbg(dev, "transmission failed\n");
}

static netdev_tx_t
ieee802154_tx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	int ret;

	if (!(local->hw.flags & IEEE802154_HW_TX_OMIT_CKSUM)) {
		u16 crc = crc_ccitt(0, skb->data, skb->len);

		put_unaligned_le16(crc, skb_put(skb, 2));
	}

	if (skb_cow_head(skb, local->hw.extra_tx_headroom))
		goto err_tx;

	/* Stop the netif queue on each sub_if_data object. */
	ieee802154_stop_queue(&local->hw);

	/* async is priority, otherwise sync is fallback */
	if (local->ops->xmit_async) {
		ret = drv_xmit_async(local, skb);
		if (ret) {
			ieee802154_wake_queue(&local->hw);
			goto err_tx;
		}

		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
	} else {
		INIT_WORK(&ieee802154_xmit_cb.work, ieee802154_xmit_worker);
		ieee802154_xmit_cb.skb = skb;
		ieee802154_xmit_cb.local = local;

		queue_work(local->workqueue, &ieee802154_xmit_cb.work);
	}

	return NETDEV_TX_OK;

err_tx:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

netdev_tx_t
ieee802154_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	skb->skb_iif = dev->ifindex;

	return ieee802154_tx(sdata->local, skb);
}

netdev_tx_t
ieee802154_subif_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);
	int rc;

	rc = mac802154_llsec_encrypt(&sdata->sec, skb);
	if (rc) {
		netdev_warn(dev, "encryption failed: %i\n", rc);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;

	return ieee802154_tx(sdata->local, skb);
}
