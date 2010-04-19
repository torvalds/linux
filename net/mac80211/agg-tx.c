/*
 * HT handling
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2009, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "wme.h"

/**
 * DOC: TX aggregation
 *
 * Aggregation on the TX side requires setting the hardware flag
 * %IEEE80211_HW_AMPDU_AGGREGATION as well as, if present, the @ampdu_queues
 * hardware parameter to the number of hardware AMPDU queues. If there are no
 * hardware queues then the driver will (currently) have to do all frame
 * buffering.
 *
 * When TX aggregation is started by some subsystem (usually the rate control
 * algorithm would be appropriate) by calling the
 * ieee80211_start_tx_ba_session() function, the driver will be notified via
 * its @ampdu_action function, with the %IEEE80211_AMPDU_TX_START action.
 *
 * In response to that, the driver is later required to call the
 * ieee80211_start_tx_ba_cb() (or ieee80211_start_tx_ba_cb_irqsafe())
 * function, which will start the aggregation session.
 *
 * Similarly, when the aggregation session is stopped by
 * ieee80211_stop_tx_ba_session(), the driver's @ampdu_action function will
 * be called with the action %IEEE80211_AMPDU_TX_STOP. In this case, the
 * call must not fail, and the driver must later call ieee80211_stop_tx_ba_cb()
 * (or ieee80211_stop_tx_ba_cb_irqsafe()).
 */

static void ieee80211_send_addba_request(struct ieee80211_sub_if_data *sdata,
					 const u8 *da, u16 tid,
					 u8 dialog_token, u16 start_seq_num,
					 u16 agg_size, u16 timeout)
{
	struct ieee80211_local *local = sdata->local;
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
	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		memcpy(mgmt->bssid, sdata->dev->dev_addr, ETH_ALEN);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);

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

	ieee80211_tx_skb(sdata, skb, 1);
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

	ieee80211_tx_skb(sdata, skb, 0);
}

int ___ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				    enum ieee80211_back_parties initiator)
{
	struct ieee80211_local *local = sta->local;
	int ret;
	u8 *state;

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Tx BA session stop requested for %pM tid %u\n",
	       sta->sta.addr, tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	state = &sta->ampdu_mlme.tid_state_tx[tid];

	if (*state == HT_AGG_STATE_OPERATIONAL)
		sta->ampdu_mlme.addba_req_num[tid] = 0;

	*state = HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);

	ret = drv_ampdu_action(local, IEEE80211_AMPDU_TX_STOP,
			       &sta->sta, tid, NULL);

	/* HW shall not deny going back to legacy */
	if (WARN_ON(ret)) {
		/*
		 * We may have pending packets get stuck in this case...
		 * Not bothering with a workaround for now.
		 */
	}

	return ret;
}

/*
 * After sending add Block Ack request we activated a timer until
 * add Block Ack response will arrive from the recipient.
 * If this timer expires sta_addba_resp_timer_expired will be executed.
 */
static void sta_addba_resp_timer_expired(unsigned long data)
{
	/* not an elegant detour, but there is no choice as the timer passes
	 * only one argument, and both sta_info and TID are needed, so init
	 * flow in sta_info_create gives the TID as data, while the timer_to_id
	 * array gives the sta through container_of */
	u16 tid = *(u8 *)data;
	struct sta_info *sta = container_of((void *)data,
		struct sta_info, timer_to_tid[tid]);
	u8 *state;

	state = &sta->ampdu_mlme.tid_state_tx[tid];

	/* check if the TID waits for addBA response */
	spin_lock_bh(&sta->lock);
	if ((*state & (HT_ADDBA_REQUESTED_MSK | HT_ADDBA_RECEIVED_MSK |
		       HT_AGG_STATE_REQ_STOP_BA_MSK)) !=
						HT_ADDBA_REQUESTED_MSK) {
		spin_unlock_bh(&sta->lock);
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "timer expired on tid %d but we are not "
				"(or no longer) expecting addBA response there",
			tid);
#endif
		return;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "addBA response timer expired on tid %d\n", tid);
#endif

	___ieee80211_stop_tx_ba_session(sta, tid, WLAN_BACK_INITIATOR);
	spin_unlock_bh(&sta->lock);
}

static inline int ieee80211_ac_from_tid(int tid)
{
	return ieee802_1d_to_ac[tid & 7];
}

int ieee80211_start_tx_ba_session(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	u8 *state;
	int ret = 0;
	u16 start_seq_num;

	if (WARN_ON(!local->ops->ampdu_action))
		return -EINVAL;

	if ((tid >= STA_TID_NUM) || !(hw->flags & IEEE80211_HW_AMPDU_AGGREGATION))
		return -EINVAL;

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Open BA session requested for %pM tid %u\n",
	       ra, tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find the station\n");
#endif
		ret = -ENOENT;
		goto unlock;
	}

	/*
	 * The aggregation code is not prepared to handle
	 * anything but STA/AP due to the BSSID handling.
	 * IBSS could work in the code but isn't supported
	 * by drivers or the standard.
	 */
	if (sta->sdata->vif.type != NL80211_IFTYPE_STATION &&
	    sta->sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
	    sta->sdata->vif.type != NL80211_IFTYPE_AP) {
		ret = -EINVAL;
		goto unlock;
	}

	if (test_sta_flags(sta, WLAN_STA_SUSPEND)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Suspend in progress. "
		       "Denying BA session request\n");
#endif
		ret = -EINVAL;
		goto unlock;
	}

	spin_lock_bh(&sta->lock);
	spin_lock(&local->ampdu_lock);

	sdata = sta->sdata;

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

	/*
	 * While we're asking the driver about the aggregation,
	 * stop the AC queue so that we don't have to worry
	 * about frames that came in while we were doing that,
	 * which would require us to put them to the AC pending
	 * afterwards which just makes the code more complex.
	 */
	ieee80211_stop_queue_by_reason(
		&local->hw, ieee80211_ac_from_tid(tid),
		IEEE80211_QUEUE_STOP_REASON_AGGREGATION);

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
		goto err_wake_queue;
	}

	skb_queue_head_init(&sta->ampdu_mlme.tid_tx[tid]->pending);

	/* Tx timer */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.function =
			sta_addba_resp_timer_expired;
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.data =
			(unsigned long)&sta->timer_to_tid[tid];
	init_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);

	/* Ok, the Addba frame hasn't been sent yet, but if the driver calls the
	 * call back right away, it must see that the flow has begun */
	*state |= HT_ADDBA_REQUESTED_MSK;

	start_seq_num = sta->tid_seq[tid];

	ret = drv_ampdu_action(local, IEEE80211_AMPDU_TX_START,
			       &sta->sta, tid, &start_seq_num);

	if (ret) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - HW unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		*state = HT_AGG_STATE_IDLE;
		goto err_free;
	}

	/* Driver vetoed or OKed, but we can take packets again now */
	ieee80211_wake_queue_by_reason(
		&local->hw, ieee80211_ac_from_tid(tid),
		IEEE80211_QUEUE_STOP_REASON_AGGREGATION);

	spin_unlock(&local->ampdu_lock);
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
	sta->ampdu_mlme.addba_req_num[tid]++;
	/* activate the timer for the recipient's addBA response */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.expires =
				jiffies + ADDBA_RESP_INTERVAL;
	add_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "activated addBA response timer on tid %d\n", tid);
#endif
	goto unlock;

 err_free:
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
 err_wake_queue:
	ieee80211_wake_queue_by_reason(
		&local->hw, ieee80211_ac_from_tid(tid),
		IEEE80211_QUEUE_STOP_REASON_AGGREGATION);
 err_unlock_sta:
	spin_unlock(&local->ampdu_lock);
	spin_unlock_bh(&sta->lock);
 unlock:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_session);

/*
 * splice packets from the STA's pending to the local pending,
 * requires a call to ieee80211_agg_splice_finish and holding
 * local->ampdu_lock across both calls.
 */
static void ieee80211_agg_splice_packets(struct ieee80211_local *local,
					 struct sta_info *sta, u16 tid)
{
	unsigned long flags;
	u16 queue = ieee80211_ac_from_tid(tid);

	ieee80211_stop_queue_by_reason(
		&local->hw, queue,
		IEEE80211_QUEUE_STOP_REASON_AGGREGATION);

	if (!(sta->ampdu_mlme.tid_state_tx[tid] & HT_ADDBA_REQUESTED_MSK))
		return;

	if (WARN(!sta->ampdu_mlme.tid_tx[tid],
		 "TID %d gone but expected when splicing aggregates from"
		 "the pending queue\n", tid))
		return;

	if (!skb_queue_empty(&sta->ampdu_mlme.tid_tx[tid]->pending)) {
		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		/* copy over remaining packets */
		skb_queue_splice_tail_init(
			&sta->ampdu_mlme.tid_tx[tid]->pending,
			&local->pending[queue]);
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	}
}

static void ieee80211_agg_splice_finish(struct ieee80211_local *local,
					struct sta_info *sta, u16 tid)
{
	u16 queue = ieee80211_ac_from_tid(tid);

	ieee80211_wake_queue_by_reason(
		&local->hw, queue,
		IEEE80211_QUEUE_STOP_REASON_AGGREGATION);
}

/* caller must hold sta->lock */
static void ieee80211_agg_tx_operational(struct ieee80211_local *local,
					 struct sta_info *sta, u16 tid)
{
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Aggregation is on for tid %d \n", tid);
#endif

	spin_lock(&local->ampdu_lock);
	ieee80211_agg_splice_packets(local, sta, tid);
	/*
	 * NB: we rely on sta->lock being taken in the TX
	 * processing here when adding to the pending queue,
	 * otherwise we could only change the state of the
	 * session to OPERATIONAL _here_.
	 */
	ieee80211_agg_splice_finish(local, sta, tid);
	spin_unlock(&local->ampdu_lock);

	drv_ampdu_action(local, IEEE80211_AMPDU_TX_OPERATIONAL,
			 &sta->sta, tid, NULL);
}

void ieee80211_start_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;

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
		printk(KERN_DEBUG "Could not find station: %pM\n", ra);
#endif
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (WARN_ON(!(*state & HT_ADDBA_REQUESTED_MSK))) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "addBA was not requested yet, state is %d\n",
				*state);
#endif
		spin_unlock_bh(&sta->lock);
		rcu_read_unlock();
		return;
	}

	if (WARN_ON(*state & HT_ADDBA_DRV_READY_MSK))
		goto out;

	*state |= HT_ADDBA_DRV_READY_MSK;

	if (*state == HT_AGG_STATE_OPERATIONAL)
		ieee80211_agg_tx_operational(local, sta, tid);

 out:
	spin_unlock_bh(&sta->lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb);

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

int __ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				   enum ieee80211_back_parties initiator)
{
	u8 *state;
	int ret;

	/* check if the TID is in aggregation */
	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (*state != HT_AGG_STATE_OPERATIONAL) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = ___ieee80211_stop_tx_ba_session(sta, tid, initiator);

 unlock:
	spin_unlock_bh(&sta->lock);
	return ret;
}

int ieee80211_stop_tx_ba_session(struct ieee80211_hw *hw,
				 u8 *ra, u16 tid,
				 enum ieee80211_back_parties initiator)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	int ret = 0;

	if (!local->ops->ampdu_action)
		return -EINVAL;

	if (tid >= STA_TID_NUM)
		return -EINVAL;

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	ret = __ieee80211_stop_tx_ba_session(sta, tid, initiator);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_session);

void ieee80211_stop_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u8 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;

	if (tid >= STA_TID_NUM) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
#endif
		return;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Stopping Tx BA session for %pM tid %d\n",
	       ra, tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find station: %pM\n", ra);
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

	spin_lock_bh(&sta->lock);
	spin_lock(&local->ampdu_lock);

	ieee80211_agg_splice_packets(local, sta, tid);

	*state = HT_AGG_STATE_IDLE;
	/* from now on packets are no longer put onto sta->pending */
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;

	ieee80211_agg_splice_finish(local, sta, tid);

	spin_unlock(&local->ampdu_lock);
	spin_unlock_bh(&sta->lock);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb);

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


void ieee80211_process_addba_resp(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct ieee80211_mgmt *mgmt,
				  size_t len)
{
	u16 capab, tid;
	u8 *state;

	capab = le16_to_cpu(mgmt->u.action.u.addba_resp.capab);
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;

	state = &sta->ampdu_mlme.tid_state_tx[tid];

	spin_lock_bh(&sta->lock);

	if (!(*state & HT_ADDBA_REQUESTED_MSK))
		goto out;

	if (mgmt->u.action.u.addba_resp.dialog_token !=
		sta->ampdu_mlme.tid_tx[tid]->dialog_token) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "wrong addBA response token, tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto out;
	}

	del_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "switched off addBA timer for tid %d \n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	if (le16_to_cpu(mgmt->u.action.u.addba_resp.status)
			== WLAN_STATUS_SUCCESS) {
		u8 curstate = *state;

		*state |= HT_ADDBA_RECEIVED_MSK;

		if (*state != curstate && *state == HT_AGG_STATE_OPERATIONAL)
			ieee80211_agg_tx_operational(local, sta, tid);

		sta->ampdu_mlme.addba_req_num[tid] = 0;
	} else {
		___ieee80211_stop_tx_ba_session(sta, tid, WLAN_BACK_INITIATOR);
	}

 out:
	spin_unlock_bh(&sta->lock);
}
