/*
 * HT handling
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2008, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <net/wireless.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "wme.h"

int ieee80211_ht_cap_ie_to_ht_info(struct ieee80211_ht_cap *ht_cap_ie,
				   struct ieee80211_ht_info *ht_info)
{

	if (ht_info == NULL)
		return -EINVAL;

	memset(ht_info, 0, sizeof(*ht_info));

	if (ht_cap_ie) {
		u8 ampdu_info = ht_cap_ie->ampdu_params_info;

		ht_info->ht_supported = 1;
		ht_info->cap = le16_to_cpu(ht_cap_ie->cap_info);
		ht_info->ampdu_factor =
			ampdu_info & IEEE80211_HT_CAP_AMPDU_FACTOR;
		ht_info->ampdu_density =
			(ampdu_info & IEEE80211_HT_CAP_AMPDU_DENSITY) >> 2;
		memcpy(ht_info->supp_mcs_set, ht_cap_ie->supp_mcs_set, 16);
	} else
		ht_info->ht_supported = 0;

	return 0;
}

int ieee80211_ht_addt_info_ie_to_ht_bss_info(
			struct ieee80211_ht_addt_info *ht_add_info_ie,
			struct ieee80211_ht_bss_info *bss_info)
{
	if (bss_info == NULL)
		return -EINVAL;

	memset(bss_info, 0, sizeof(*bss_info));

	if (ht_add_info_ie) {
		u16 op_mode;
		op_mode = le16_to_cpu(ht_add_info_ie->operation_mode);

		bss_info->primary_channel = ht_add_info_ie->control_chan;
		bss_info->bss_cap = ht_add_info_ie->ht_param;
		bss_info->bss_op_mode = (u8)(op_mode & 0xff);
	}

	return 0;
}

void ieee80211_send_addba_request(struct ieee80211_sub_if_data *sdata, const u8 *da,
				u16 tid, u8 dialog_token, u16 start_seq_num,
				u16 agg_size, u16 timeout)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 capab;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);

	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer "
				"for addba request frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP)
		memcpy(mgmt->bssid, sdata->dev->dev_addr, ETH_ALEN);
	else
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.addba_req));

	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.addba_req.action_code = WLAN_ACTION_ADDBA_REQ;

	mgmt->u.action.u.addba_req.dialog_token = dialog_token;
	capab = (u16)(1 << 1);		/* bit 1 aggregation policy */
	capab |= (u16)(tid << 2); 	/* bit 5:2 TID number */
	capab |= (u16)(agg_size << 6);	/* bit 15:6 max size of aggergation */

	mgmt->u.action.u.addba_req.capab = cpu_to_le16(capab);

	mgmt->u.action.u.addba_req.timeout = cpu_to_le16(timeout);
	mgmt->u.action.u.addba_req.start_seq_num =
					cpu_to_le16(start_seq_num << 4);

	ieee80211_sta_tx(sdata, skb, 0);
}

void ieee80211_send_delba(struct ieee80211_sub_if_data *sdata, const u8 *da, u16 tid,
			  u16 initiator, u16 reason_code)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 params;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);

	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer "
					"for delba frame\n", sdata->dev->name);
		return;
	}

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP)
		memcpy(mgmt->bssid, sdata->dev->dev_addr, ETH_ALEN);
	else
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.delba));

	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.delba.action_code = WLAN_ACTION_DELBA;
	params = (u16)(initiator << 11); 	/* bit 11 initiator */
	params |= (u16)(tid << 12); 		/* bit 15:12 TID number */

	mgmt->u.action.u.delba.params = cpu_to_le16(params);
	mgmt->u.action.u.delba.reason_code = cpu_to_le16(reason_code);

	ieee80211_sta_tx(sdata, skb, 0);
}

void ieee80211_send_bar(struct ieee80211_sub_if_data *sdata, u8 *ra, u16 tid, u16 ssn)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_bar *bar;
	u16 bar_control = 0;

	skb = dev_alloc_skb(sizeof(*bar) + local->hw.extra_tx_headroom);
	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer for "
			"bar frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);
	bar = (struct ieee80211_bar *)skb_put(skb, sizeof(*bar));
	memset(bar, 0, sizeof(*bar));
	bar->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					 IEEE80211_STYPE_BACK_REQ);
	memcpy(bar->ra, ra, ETH_ALEN);
	memcpy(bar->ta, sdata->dev->dev_addr, ETH_ALEN);
	bar_control |= (u16)IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL;
	bar_control |= (u16)IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA;
	bar_control |= (u16)(tid << 12);
	bar->control = cpu_to_le16(bar_control);
	bar->start_seq_num = cpu_to_le16(ssn);

	ieee80211_sta_tx(sdata, skb, 0);
}

void ieee80211_sta_stop_rx_ba_session(struct ieee80211_sub_if_data *sdata, u8 *ra, u16 tid,
					u16 initiator, u16 reason)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	int ret, i;
	DECLARE_MAC_BUF(mac);

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	/* check if TID is in operational state */
	spin_lock_bh(&sta->lock);
	if (sta->ampdu_mlme.tid_state_rx[tid]
				!= HT_AGG_STATE_OPERATIONAL) {
		spin_unlock_bh(&sta->lock);
		rcu_read_unlock();
		return;
	}
	sta->ampdu_mlme.tid_state_rx[tid] =
		HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);
	spin_unlock_bh(&sta->lock);

	/* stop HW Rx aggregation. ampdu_action existence
	 * already verified in session init so we add the BUG_ON */
	BUG_ON(!local->ops->ampdu_action);

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Rx BA session stop requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_RX_STOP,
					ra, tid, NULL);
	if (ret)
		printk(KERN_DEBUG "HW problem - can not stop rx "
				"aggregation for tid %d\n", tid);

	/* shutdown timer has not expired */
	if (initiator != WLAN_BACK_TIMER)
		del_timer_sync(&sta->ampdu_mlme.tid_rx[tid]->session_timer);

	/* check if this is a self generated aggregation halt */
	if (initiator == WLAN_BACK_RECIPIENT || initiator == WLAN_BACK_TIMER)
		ieee80211_send_delba(sdata, ra, tid, 0, reason);

	/* free the reordering buffer */
	for (i = 0; i < sta->ampdu_mlme.tid_rx[tid]->buf_size; i++) {
		if (sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i]) {
			/* release the reordered frames */
			dev_kfree_skb(sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i]);
			sta->ampdu_mlme.tid_rx[tid]->stored_mpdu_num--;
			sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i] = NULL;
		}
	}
	/* free resources */
	kfree(sta->ampdu_mlme.tid_rx[tid]->reorder_buf);
	kfree(sta->ampdu_mlme.tid_rx[tid]);
	sta->ampdu_mlme.tid_rx[tid] = NULL;
	sta->ampdu_mlme.tid_state_rx[tid] = HT_AGG_STATE_IDLE;

	rcu_read_unlock();
}


/*
 * After sending add Block Ack request we activated a timer until
 * add Block Ack response will arrive from the recipient.
 * If this timer expires sta_addba_resp_timer_expired will be executed.
 */
void sta_addba_resp_timer_expired(unsigned long data)
{
	/* not an elegant detour, but there is no choice as the timer passes
	 * only one argument, and both sta_info and TID are needed, so init
	 * flow in sta_info_create gives the TID as data, while the timer_to_id
	 * array gives the sta through container_of */
	u16 tid = *(u8 *)data;
	struct sta_info *temp_sta = container_of((void *)data,
		struct sta_info, timer_to_tid[tid]);

	struct ieee80211_local *local = temp_sta->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	u8 *state;

	rcu_read_lock();

	sta = sta_info_get(local, temp_sta->addr);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	/* check if the TID waits for addBA response */
	spin_lock_bh(&sta->lock);
	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
		spin_unlock_bh(&sta->lock);
		*state = HT_AGG_STATE_IDLE;
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "timer expired on tid %d but we are not "
				"expecting addBA response there", tid);
#endif
		goto timer_expired_exit;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "addBA response timer expired on tid %d\n", tid);
#endif

	/* go through the state check in stop_BA_session */
	*state = HT_AGG_STATE_OPERATIONAL;
	spin_unlock_bh(&sta->lock);
	ieee80211_stop_tx_ba_session(hw, temp_sta->addr, tid,
				     WLAN_BACK_INITIATOR);

timer_expired_exit:
	rcu_read_unlock();
}

void ieee80211_sta_tear_down_BA_sessions(struct ieee80211_sub_if_data *sdata, u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	int i;

	for (i = 0; i <  STA_TID_NUM; i++) {
		ieee80211_stop_tx_ba_session(&local->hw, addr, i,
					     WLAN_BACK_INITIATOR);
		ieee80211_sta_stop_rx_ba_session(sdata, addr, i,
						 WLAN_BACK_RECIPIENT,
						 WLAN_REASON_QSTA_LEAVE_QBSS);
	}
}

int ieee80211_start_tx_ba_session(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	u16 start_seq_num;
	u8 *state;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Open BA session requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find the station\n");
#endif
		ret = -ENOENT;
		goto exit;
	}

	spin_lock_bh(&sta->lock);

	/* we have tried too many times, receiver does not want A-MPDU */
	if (sta->ampdu_mlme.addba_req_num[tid] > HT_AGG_MAX_RETRIES) {
		ret = -EBUSY;
		goto err_unlock_sta;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	/* check if the TID is not in aggregation flow already */
	if (*state != HT_AGG_STATE_IDLE) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - session is not "
				 "idle on tid %u\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		ret = -EAGAIN;
		goto err_unlock_sta;
	}

	/* prepare A-MPDU MLME for Tx aggregation */
	sta->ampdu_mlme.tid_tx[tid] =
			kmalloc(sizeof(struct tid_ampdu_tx), GFP_ATOMIC);
	if (!sta->ampdu_mlme.tid_tx[tid]) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_ERR "allocate tx mlme to tid %d failed\n",
					tid);
#endif
		ret = -ENOMEM;
		goto err_unlock_sta;
	}
	/* Tx timer */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.function =
			sta_addba_resp_timer_expired;
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.data =
			(unsigned long)&sta->timer_to_tid[tid];
	init_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);

	/* create a new queue for this aggregation */
	ret = ieee80211_ht_agg_queue_add(local, sta, tid);

	/* case no queue is available to aggregation
	 * don't switch to aggregation */
	if (ret) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - queue unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto err_unlock_queue;
	}
	sdata = sta->sdata;

	/* Ok, the Addba frame hasn't been sent yet, but if the driver calls the
	 * call back right away, it must see that the flow has begun */
	*state |= HT_ADDBA_REQUESTED_MSK;

	/* This is slightly racy because the queue isn't stopped */
	start_seq_num = sta->tid_seq[tid];

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_START,
						ra, tid, &start_seq_num);

	if (ret) {
		/* No need to requeue the packets in the agg queue, since we
		 * held the tx lock: no packet could be enqueued to the newly
		 * allocated queue */
		ieee80211_ht_agg_queue_remove(local, sta, tid, 0);
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - HW unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		*state = HT_AGG_STATE_IDLE;
		goto err_unlock_queue;
	}

	/* Will put all the packets in the new SW queue */
	ieee80211_requeue(local, ieee802_1d_to_ac[tid]);
	spin_unlock_bh(&sta->lock);

	/* send an addBA request */
	sta->ampdu_mlme.dialog_token_allocator++;
	sta->ampdu_mlme.tid_tx[tid]->dialog_token =
			sta->ampdu_mlme.dialog_token_allocator;
	sta->ampdu_mlme.tid_tx[tid]->ssn = start_seq_num;


	ieee80211_send_addba_request(sta->sdata, ra, tid,
			 sta->ampdu_mlme.tid_tx[tid]->dialog_token,
			 sta->ampdu_mlme.tid_tx[tid]->ssn,
			 0x40, 5000);
	/* activate the timer for the recipient's addBA response */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.expires =
				jiffies + ADDBA_RESP_INTERVAL;
	add_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "activated addBA response timer on tid %d\n", tid);
#endif
	goto exit;

err_unlock_queue:
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	ret = -EBUSY;
err_unlock_sta:
	spin_unlock_bh(&sta->lock);
exit:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_session);

int ieee80211_stop_tx_ba_session(struct ieee80211_hw *hw,
				 u8 *ra, u16 tid,
				 enum ieee80211_back_parties initiator)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int ret = 0;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* check if the TID is in aggregation */
	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (*state != HT_AGG_STATE_OPERATIONAL) {
		ret = -ENOENT;
		goto stop_BA_exit;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Tx BA session stop requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	ieee80211_stop_queue(hw, sta->tid_to_tx_q[tid]);

	*state = HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_STOP,
						ra, tid, NULL);

	/* case HW denied going back to legacy */
	if (ret) {
		WARN_ON(ret != -EBUSY);
		*state = HT_AGG_STATE_OPERATIONAL;
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
		goto stop_BA_exit;
	}

stop_BA_exit:
	spin_unlock_bh(&sta->lock);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_session);

void ieee80211_start_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
#endif
		return;
	}

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
#endif
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "addBA was not requested yet, state is %d\n",
				*state);
#endif
		spin_unlock_bh(&sta->lock);
		rcu_read_unlock();
		return;
	}

	WARN_ON_ONCE(*state & HT_ADDBA_DRV_READY_MSK);

	*state |= HT_ADDBA_DRV_READY_MSK;

	if (*state == HT_AGG_STATE_OPERATIONAL) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Aggregation is on for tid %d \n", tid);
#endif
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
	}
	spin_unlock_bh(&sta->lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb);

void ieee80211_stop_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u8 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int agg_queue;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
#endif
		return;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Stopping Tx BA session for %s tid %d\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
#endif
		rcu_read_unlock();
		return;
	}
	state = &sta->ampdu_mlme.tid_state_tx[tid];

	/* NOTE: no need to use sta->lock in this state check, as
	 * ieee80211_stop_tx_ba_session will let only one stop call to
	 * pass through per sta/tid
	 */
	if ((*state & HT_AGG_STATE_REQ_STOP_BA_MSK) == 0) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "unexpected callback to A-MPDU stop\n");
#endif
		rcu_read_unlock();
		return;
	}

	if (*state & HT_AGG_STATE_INITIATOR_MSK)
		ieee80211_send_delba(sta->sdata, ra, tid,
			WLAN_BACK_INITIATOR, WLAN_REASON_QSTA_NOT_USE);

	agg_queue = sta->tid_to_tx_q[tid];

	ieee80211_ht_agg_queue_remove(local, sta, tid, 1);

	/* We just requeued the all the frames that were in the
	 * removed queue, and since we might miss a softirq we do
	 * netif_schedule_queue.  ieee80211_wake_queue is not used
	 * here as this queue is not necessarily stopped
	 */
	netif_schedule_queue(netdev_get_tx_queue(local->mdev, agg_queue));
	spin_lock_bh(&sta->lock);
	*state = HT_AGG_STATE_IDLE;
	sta->ampdu_mlme.addba_req_num[tid] = 0;
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	spin_unlock_bh(&sta->lock);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb);

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				      const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping start BA session", skb->dev->name);
#endif
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_ADDBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb_irqsafe);

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				     const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping stop BA session", skb->dev->name);
#endif
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_DELBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb_irqsafe);
