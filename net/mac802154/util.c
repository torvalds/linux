// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/mac80211/util.c
 */

#include "ieee802154_i.h"
#include "driver-ops.h"

/* privid for wpan_phys to determine whether they belong to us or not */
const void *const mac802154_wpan_phy_privid = &mac802154_wpan_phy_privid;

/**
 * ieee802154_wake_queue - wake ieee802154 queue
 * @hw: main hardware object
 *
 * Tranceivers usually have either one transmit framebuffer or one framebuffer
 * for both transmitting and receiving. Hence, the core currently only handles
 * one frame at a time for each phy, which means we had to stop the queue to
 * avoid new skb to come during the transmission. The queue then needs to be
 * woken up after the operation.
 */
static void ieee802154_wake_queue(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata;

	rcu_read_lock();
	clear_bit(WPAN_PHY_FLAG_STATE_QUEUE_STOPPED, &local->phy->flags);
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!sdata->dev)
			continue;

		netif_wake_queue(sdata->dev);
	}
	rcu_read_unlock();
}

/**
 * ieee802154_stop_queue - stop ieee802154 queue
 * @hw: main hardware object
 *
 * Tranceivers usually have either one transmit framebuffer or one framebuffer
 * for both transmitting and receiving. Hence, the core currently only handles
 * one frame at a time for each phy, which means we need to tell upper layers to
 * stop giving us new skbs while we are busy with the transmitted one. The queue
 * must then be stopped before transmitting.
 */
static void ieee802154_stop_queue(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!sdata->dev)
			continue;

		netif_stop_queue(sdata->dev);
	}
	rcu_read_unlock();
}

void ieee802154_hold_queue(struct ieee802154_local *local)
{
	unsigned long flags;

	spin_lock_irqsave(&local->phy->queue_lock, flags);
	if (!atomic_fetch_inc(&local->phy->hold_txs))
		ieee802154_stop_queue(&local->hw);
	spin_unlock_irqrestore(&local->phy->queue_lock, flags);
}

void ieee802154_release_queue(struct ieee802154_local *local)
{
	unsigned long flags;

	spin_lock_irqsave(&local->phy->queue_lock, flags);
	if (atomic_dec_and_test(&local->phy->hold_txs))
		ieee802154_wake_queue(&local->hw);
	spin_unlock_irqrestore(&local->phy->queue_lock, flags);
}

void ieee802154_disable_queue(struct ieee802154_local *local)
{
	struct ieee802154_sub_if_data *sdata;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!sdata->dev)
			continue;

		netif_tx_disable(sdata->dev);
	}
	rcu_read_unlock();
}

enum hrtimer_restart ieee802154_xmit_ifs_timer(struct hrtimer *timer)
{
	struct ieee802154_local *local =
		container_of(timer, struct ieee802154_local, ifs_timer);

	ieee802154_release_queue(local);

	return HRTIMER_NORESTART;
}

void ieee802154_xmit_complete(struct ieee802154_hw *hw, struct sk_buff *skb,
			      bool ifs_handling)
{
	struct ieee802154_local *local = hw_to_local(hw);

	local->tx_result = IEEE802154_SUCCESS;

	if (ifs_handling) {
		u8 max_sifs_size;

		/* If transceiver sets CRC on his own we need to use lifs
		 * threshold len above 16 otherwise 18, because it's not
		 * part of skb->len.
		 */
		if (hw->flags & IEEE802154_HW_TX_OMIT_CKSUM)
			max_sifs_size = IEEE802154_MAX_SIFS_FRAME_SIZE -
					IEEE802154_FCS_LEN;
		else
			max_sifs_size = IEEE802154_MAX_SIFS_FRAME_SIZE;

		if (skb->len > max_sifs_size)
			hrtimer_start(&local->ifs_timer,
				      hw->phy->lifs_period * NSEC_PER_USEC,
				      HRTIMER_MODE_REL);
		else
			hrtimer_start(&local->ifs_timer,
				      hw->phy->sifs_period * NSEC_PER_USEC,
				      HRTIMER_MODE_REL);
	} else {
		ieee802154_release_queue(local);
	}

	dev_consume_skb_any(skb);
	if (atomic_dec_and_test(&hw->phy->ongoing_txs))
		wake_up(&hw->phy->sync_txq);
}
EXPORT_SYMBOL(ieee802154_xmit_complete);

void ieee802154_xmit_error(struct ieee802154_hw *hw, struct sk_buff *skb,
			   int reason)
{
	struct ieee802154_local *local = hw_to_local(hw);

	local->tx_result = reason;
	ieee802154_release_queue(local);
	dev_kfree_skb_any(skb);
	if (atomic_dec_and_test(&hw->phy->ongoing_txs))
		wake_up(&hw->phy->sync_txq);
}
EXPORT_SYMBOL(ieee802154_xmit_error);

void ieee802154_xmit_hw_error(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	ieee802154_xmit_error(hw, skb, IEEE802154_SYSTEM_ERROR);
}
EXPORT_SYMBOL(ieee802154_xmit_hw_error);

void ieee802154_stop_device(struct ieee802154_local *local)
{
	flush_workqueue(local->workqueue);
	hrtimer_cancel(&local->ifs_timer);
	drv_stop(local);
}
