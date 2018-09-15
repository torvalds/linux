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

void ieee802154_xmit_worker(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, tx_work);
	struct sk_buff *skb = local->tx_skb;
	struct net_device *dev = skb->dev;
	int res;

	res = drv_xmit_sync(local, skb);
	if (res)
		goto err_tx;

	ieee802154_xmit_complete(&local->hw, skb, false);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return;

err_tx:
	/* Restart the netif queue on each sub_if_data object. */
	ieee802154_wake_queue(&local->hw);
	kfree_skb(skb);
	netdev_dbg(dev, "transmission failed\n");
}

static netdev_tx_t
ieee802154_tx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	int ret;

	if (!(local->hw.flags & IEEE802154_HW_TX_OMIT_CKSUM)) {
		struct sk_buff *nskb;
		u16 crc;

		if (unlikely(skb_tailroom(skb) < IEEE802154_FCS_LEN)) {
			nskb = skb_copy_expand(skb, 0, IEEE802154_FCS_LEN,
					       GFP_ATOMIC);
			if (likely(nskb)) {
				consume_skb(skb);
				skb = nskb;
			} else {
				goto err_tx;
			}
		}

		crc = crc_ccitt(0, skb->data, skb->len);
		put_unaligned_le16(crc, skb_put(skb, 2));
	}

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
		local->tx_skb = skb;
		queue_work(local->workqueue, &local->tx_work);
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

	/* TODO we should move it to wpan_dev_hard_header and dev_hard_header
	 * functions. The reason is wireshark will show a mac header which is
	 * with security fields but the payload is not encrypted.
	 */
	rc = mac802154_llsec_encrypt(&sdata->sec, skb);
	if (rc) {
		netdev_warn(dev, "encryption failed: %i\n", rc);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->skb_iif = dev->ifindex;

	return ieee802154_tx(sdata->local, skb);
}
