// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2007-2012 Siemens AG
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
#include <linux/unaligned.h>

#include <net/rtnetlink.h>
#include <net/ieee802154_netdev.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"

void ieee802154_xmit_sync_worker(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, sync_tx_work);
	struct sk_buff *skb = local->tx_skb;
	struct net_device *dev = skb->dev;
	int res;

	res = drv_xmit_sync(local, skb);
	if (res)
		goto err_tx;

	DEV_STATS_INC(dev, tx_packets);
	DEV_STATS_ADD(dev, tx_bytes, skb->len);

	ieee802154_xmit_complete(&local->hw, skb, false);

	return;

err_tx:
	/* Restart the netif queue on each sub_if_data object. */
	ieee802154_release_queue(local);
	if (atomic_dec_and_test(&local->phy->ongoing_txs))
		wake_up(&local->phy->sync_txq);
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
				goto err_free_skb;
			}
		}

		crc = crc_ccitt(0, skb->data, skb->len);
		put_unaligned_le16(crc, skb_put(skb, 2));
	}

	/* Stop the netif queue on each sub_if_data object. */
	ieee802154_hold_queue(local);
	atomic_inc(&local->phy->ongoing_txs);

	/* Drivers should preferably implement the async callback. In some rare
	 * cases they only provide a sync callback which we will use as a
	 * fallback.
	 */
	if (local->ops->xmit_async) {
		unsigned int len = skb->len;

		ret = drv_xmit_async(local, skb);
		if (ret)
			goto err_wake_netif_queue;

		DEV_STATS_INC(dev, tx_packets);
		DEV_STATS_ADD(dev, tx_bytes, len);
	} else {
		local->tx_skb = skb;
		queue_work(local->workqueue, &local->sync_tx_work);
	}

	return NETDEV_TX_OK;

err_wake_netif_queue:
	ieee802154_release_queue(local);
	if (atomic_dec_and_test(&local->phy->ongoing_txs))
		wake_up(&local->phy->sync_txq);
err_free_skb:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int ieee802154_sync_queue(struct ieee802154_local *local)
{
	int ret;

	ieee802154_hold_queue(local);
	ieee802154_disable_queue(local);
	wait_event(local->phy->sync_txq, !atomic_read(&local->phy->ongoing_txs));
	ret = local->tx_result;
	ieee802154_release_queue(local);

	return ret;
}

int ieee802154_sync_and_hold_queue(struct ieee802154_local *local)
{
	int ret;

	ieee802154_hold_queue(local);
	ret = ieee802154_sync_queue(local);
	set_bit(WPAN_PHY_FLAG_STATE_QUEUE_STOPPED, &local->phy->flags);

	return ret;
}

int ieee802154_mlme_op_pre(struct ieee802154_local *local)
{
	return ieee802154_sync_and_hold_queue(local);
}

int ieee802154_mlme_tx_locked(struct ieee802154_local *local,
			      struct ieee802154_sub_if_data *sdata,
			      struct sk_buff *skb)
{
	/* Avoid possible calls to ->ndo_stop() when we asynchronously perform
	 * MLME transmissions.
	 */
	ASSERT_RTNL();

	/* Ensure the device was not stopped, otherwise error out */
	if (!local->open_count)
		return -ENETDOWN;

	/* Warn if the ieee802154 core thinks MLME frames can be sent while the
	 * net interface expects this cannot happen.
	 */
	if (WARN_ON_ONCE(!netif_running(sdata->dev)))
		return -ENETDOWN;

	ieee802154_tx(local, skb);
	return ieee802154_sync_queue(local);
}

int ieee802154_mlme_tx(struct ieee802154_local *local,
		       struct ieee802154_sub_if_data *sdata,
		       struct sk_buff *skb)
{
	int ret;

	rtnl_lock();
	ret = ieee802154_mlme_tx_locked(local, sdata, skb);
	rtnl_unlock();

	return ret;
}

void ieee802154_mlme_op_post(struct ieee802154_local *local)
{
	ieee802154_release_queue(local);
}

int ieee802154_mlme_tx_one_locked(struct ieee802154_local *local,
				  struct ieee802154_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	int ret;

	ieee802154_mlme_op_pre(local);
	ret = ieee802154_mlme_tx_locked(local, sdata, skb);
	ieee802154_mlme_op_post(local);

	return ret;
}

static bool ieee802154_queue_is_stopped(struct ieee802154_local *local)
{
	return test_bit(WPAN_PHY_FLAG_STATE_QUEUE_STOPPED, &local->phy->flags);
}

static netdev_tx_t
ieee802154_hot_tx(struct ieee802154_local *local, struct sk_buff *skb)
{
	/* Warn if the net interface tries to transmit frames while the
	 * ieee802154 core assumes the queue is stopped.
	 */
	WARN_ON_ONCE(ieee802154_queue_is_stopped(local));

	return ieee802154_tx(local, skb);
}

netdev_tx_t
ieee802154_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee802154_sub_if_data *sdata = IEEE802154_DEV_TO_SUB_IF(dev);

	skb->skb_iif = dev->ifindex;

	return ieee802154_hot_tx(sdata->local, skb);
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

	return ieee802154_hot_tx(sdata->local, skb);
}
