/*
 * HT handling
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2010, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "wme.h"

/**
 * DOC: TX A-MPDU aggregation
 *
 * Aggregation on the TX side requires setting the hardware flag
 * %IEEE80211_HW_AMPDU_AGGREGATION. The driver will then be handed
 * packets with a flag indicating A-MPDU aggregation. The driver
 * or device is responsible for actually aggregating the frames,
 * as well as deciding how many and which to aggregate.
 *
 * When TX aggregation is started by some subsystem (usually the rate
 * control algorithm would be appropriate) by calling the
 * ieee80211_start_tx_ba_session() function, the driver will be
 * notified via its @ampdu_action function, with the
 * %IEEE80211_AMPDU_TX_START action.
 *
 * In response to that, the driver is later required to call the
 * ieee80211_start_tx_ba_cb_irqsafe() function, which will really
 * start the aggregation session after the peer has also responded.
 * If the peer responds negatively, the session will be stopped
 * again right away. Note that it is possible for the aggregation
 * session to be stopped before the driver has indicated that it
 * is done setting it up, in which case it must not indicate the
 * setup completion.
 *
 * Also note that, since we also need to wait for a response from
 * the peer, the driver is notified of the completion of the
 * handshake by the %IEEE80211_AMPDU_TX_OPERATIONAL action to the
 * @ampdu_action callback.
 *
 * Similarly, when the aggregation session is stopped by the peer
 * or something calling ieee80211_stop_tx_ba_session(), the driver's
 * @ampdu_action function will be called with the action
 * %IEEE80211_AMPDU_TX_STOP. In this case, the call must not fail,
 * and the driver must later call ieee80211_stop_tx_ba_cb_irqsafe().
 * Note that the sta can get destroyed before the BA tear down is
 * complete.
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

	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
	    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
		memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		memcpy(mgmt->bssid, sdata->u.ibss.bssid, ETH_ALEN);

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

	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_bar(struct ieee80211_vif *vif, u8 *ra, u16 tid, u16 ssn)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_bar *bar;
	u16 bar_control = 0;

	skb = dev_alloc_skb(sizeof(*bar) + local->hw.extra_tx_headroom);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	bar = (struct ieee80211_bar *)skb_put(skb, sizeof(*bar));
	memset(bar, 0, sizeof(*bar));
	bar->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					 IEEE80211_STYPE_BACK_REQ);
	memcpy(bar->ra, ra, ETH_ALEN);
	memcpy(bar->ta, sdata->vif.addr, ETH_ALEN);
	bar_control |= (u16)IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL;
	bar_control |= (u16)IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA;
	bar_control |= (u16)(tid << IEEE80211_BAR_CTRL_TID_INFO_SHIFT);
	bar->control = cpu_to_le16(bar_control);
	bar->start_seq_num = cpu_to_le16(ssn);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb_tid(sdata, skb, tid);
}
EXPORT_SYMBOL(ieee80211_send_bar);

void ieee80211_assign_tid_tx(struct sta_info *sta, int tid,
			     struct tid_ampdu_tx *tid_tx)
{
	lockdep_assert_held(&sta->ampdu_mlme.mtx);
	lockdep_assert_held(&sta->lock);
	rcu_assign_pointer(sta->ampdu_mlme.tid_tx[tid], tid_tx);
}

/*
 * When multiple aggregation sessions on multiple stations
 * are being created/destroyed simultaneously, we need to
 * refcount the global queue stop caused by that in order
 * to not get into a situation where one of the aggregation
 * setup or teardown re-enables queues before the other is
 * ready to handle that.
 *
 * These two functions take care of this issue by keeping
 * a global "agg_queue_stop" refcount.
 */
static void __acquires(agg_queue)
ieee80211_stop_queue_agg(struct ieee80211_sub_if_data *sdata, int tid)
{
	int queue = sdata->vif.hw_queue[ieee80211_ac_from_tid(tid)];

	/* we do refcounting here, so don't use the queue reason refcounting */

	if (atomic_inc_return(&sdata->local->agg_queue_stop[queue]) == 1)
		ieee80211_stop_queue_by_reason(
			&sdata->local->hw, queue,
			IEEE80211_QUEUE_STOP_REASON_AGGREGATION,
			false);
	__acquire(agg_queue);
}

static void __releases(agg_queue)
ieee80211_wake_queue_agg(struct ieee80211_sub_if_data *sdata, int tid)
{
	int queue = sdata->vif.hw_queue[ieee80211_ac_from_tid(tid)];

	if (atomic_dec_return(&sdata->local->agg_queue_stop[queue]) == 0)
		ieee80211_wake_queue_by_reason(
			&sdata->local->hw, queue,
			IEEE80211_QUEUE_STOP_REASON_AGGREGATION,
			false);
	__release(agg_queue);
}

static void
ieee80211_agg_stop_txq(struct sta_info *sta, int tid)
{
	struct ieee80211_txq *txq = sta->sta.txq[tid];
	struct txq_info *txqi;

	if (!txq)
		return;

	txqi = to_txq_info(txq);

	/* Lock here to protect against further seqno updates on dequeue */
	spin_lock_bh(&txqi->queue.lock);
	set_bit(IEEE80211_TXQ_STOP, &txqi->flags);
	spin_unlock_bh(&txqi->queue.lock);
}

static void
ieee80211_agg_start_txq(struct sta_info *sta, int tid, bool enable)
{
	struct ieee80211_txq *txq = sta->sta.txq[tid];
	struct txq_info *txqi;

	if (!txq)
		return;

	txqi = to_txq_info(txq);

	if (enable)
		set_bit(IEEE80211_TXQ_AMPDU, &txqi->flags);
	else
		clear_bit(IEEE80211_TXQ_AMPDU, &txqi->flags);

	clear_bit(IEEE80211_TXQ_STOP, &txqi->flags);
	drv_wake_tx_queue(sta->sdata->local, txqi);
}

/*
 * splice packets from the STA's pending to the local pending,
 * requires a call to ieee80211_agg_splice_finish later
 */
static void __acquires(agg_queue)
ieee80211_agg_splice_packets(struct ieee80211_sub_if_data *sdata,
			     struct tid_ampdu_tx *tid_tx, u16 tid)
{
	struct ieee80211_local *local = sdata->local;
	int queue = sdata->vif.hw_queue[ieee80211_ac_from_tid(tid)];
	unsigned long flags;

	ieee80211_stop_queue_agg(sdata, tid);

	if (WARN(!tid_tx,
		 "TID %d gone but expected when splicing aggregates from the pending queue\n",
		 tid))
		return;

	if (!skb_queue_empty(&tid_tx->pending)) {
		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		/* copy over remaining packets */
		skb_queue_splice_tail_init(&tid_tx->pending,
					   &local->pending[queue]);
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	}
}

static void __releases(agg_queue)
ieee80211_agg_splice_finish(struct ieee80211_sub_if_data *sdata, u16 tid)
{
	ieee80211_wake_queue_agg(sdata, tid);
}

static void ieee80211_remove_tid_tx(struct sta_info *sta, int tid)
{
	struct tid_ampdu_tx *tid_tx;

	lockdep_assert_held(&sta->ampdu_mlme.mtx);
	lockdep_assert_held(&sta->lock);

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	/*
	 * When we get here, the TX path will not be lockless any more wrt.
	 * aggregation, since the OPERATIONAL bit has long been cleared.
	 * Thus it will block on getting the lock, if it occurs. So if we
	 * stop the queue now, we will not get any more packets, and any
	 * that might be being processed will wait for us here, thereby
	 * guaranteeing that no packets go to the tid_tx pending queue any
	 * more.
	 */

	ieee80211_agg_splice_packets(sta->sdata, tid_tx, tid);

	/* future packets must not find the tid_tx struct any more */
	ieee80211_assign_tid_tx(sta, tid, NULL);

	ieee80211_agg_splice_finish(sta->sdata, tid);
	ieee80211_agg_start_txq(sta, tid, false);

	kfree_rcu(tid_tx, rcu_head);
}

int ___ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				    enum ieee80211_agg_stop_reason reason)
{
	struct ieee80211_local *local = sta->local;
	struct tid_ampdu_tx *tid_tx;
	enum ieee80211_ampdu_mlme_action action;
	int ret;

	lockdep_assert_held(&sta->ampdu_mlme.mtx);

	switch (reason) {
	case AGG_STOP_DECLINED:
	case AGG_STOP_LOCAL_REQUEST:
	case AGG_STOP_PEER_REQUEST:
		action = IEEE80211_AMPDU_TX_STOP_CONT;
		break;
	case AGG_STOP_DESTROY_STA:
		action = IEEE80211_AMPDU_TX_STOP_FLUSH;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	spin_lock_bh(&sta->lock);

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);
	if (!tid_tx) {
		spin_unlock_bh(&sta->lock);
		return -ENOENT;
	}

	/*
	 * if we're already stopping ignore any new requests to stop
	 * unless we're destroying it in which case notify the driver
	 */
	if (test_bit(HT_AGG_STATE_STOPPING, &tid_tx->state)) {
		spin_unlock_bh(&sta->lock);
		if (reason != AGG_STOP_DESTROY_STA)
			return -EALREADY;
		ret = drv_ampdu_action(local, sta->sdata,
				       IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
				       &sta->sta, tid, NULL, 0);
		WARN_ON_ONCE(ret);
		return 0;
	}

	if (test_bit(HT_AGG_STATE_WANT_START, &tid_tx->state)) {
		/* not even started yet! */
		ieee80211_assign_tid_tx(sta, tid, NULL);
		spin_unlock_bh(&sta->lock);
		kfree_rcu(tid_tx, rcu_head);
		return 0;
	}

	set_bit(HT_AGG_STATE_STOPPING, &tid_tx->state);

	spin_unlock_bh(&sta->lock);

	ht_dbg(sta->sdata, "Tx BA session stop requested for %pM tid %u\n",
	       sta->sta.addr, tid);

	del_timer_sync(&tid_tx->addba_resp_timer);
	del_timer_sync(&tid_tx->session_timer);

	/*
	 * After this packets are no longer handed right through
	 * to the driver but are put onto tid_tx->pending instead,
	 * with locking to ensure proper access.
	 */
	clear_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state);

	/*
	 * There might be a few packets being processed right now (on
	 * another CPU) that have already gotten past the aggregation
	 * check when it was still OPERATIONAL and consequently have
	 * IEEE80211_TX_CTL_AMPDU set. In that case, this code might
	 * call into the driver at the same time or even before the
	 * TX paths calls into it, which could confuse the driver.
	 *
	 * Wait for all currently running TX paths to finish before
	 * telling the driver. New packets will not go through since
	 * the aggregation session is no longer OPERATIONAL.
	 */
	synchronize_net();

	tid_tx->stop_initiator = reason == AGG_STOP_PEER_REQUEST ?
					WLAN_BACK_RECIPIENT :
					WLAN_BACK_INITIATOR;
	tid_tx->tx_stop = reason == AGG_STOP_LOCAL_REQUEST;

	ret = drv_ampdu_action(local, sta->sdata, action,
			       &sta->sta, tid, NULL, 0);

	/* HW shall not deny going back to legacy */
	if (WARN_ON(ret)) {
		/*
		 * We may have pending packets get stuck in this case...
		 * Not bothering with a workaround for now.
		 */
	}

	/*
	 * In the case of AGG_STOP_DESTROY_STA, the driver won't
	 * necessarily call ieee80211_stop_tx_ba_cb(), so this may
	 * seem like we can leave the tid_tx data pending forever.
	 * This is true, in a way, but "forever" is only until the
	 * station struct is actually destroyed. In the meantime,
	 * leaving it around ensures that we don't transmit packets
	 * to the driver on this TID which might confuse it.
	 */

	return 0;
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
	struct tid_ampdu_tx *tid_tx;

	/* check if the TID waits for addBA response */
	rcu_read_lock();
	tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[tid]);
	if (!tid_tx ||
	    test_bit(HT_AGG_STATE_RESPONSE_RECEIVED, &tid_tx->state)) {
		rcu_read_unlock();
		ht_dbg(sta->sdata,
		       "timer expired on %pM tid %d but we are not (or no longer) expecting addBA response there\n",
		       sta->sta.addr, tid);
		return;
	}

	ht_dbg(sta->sdata, "addBA response timer expired on %pM tid %d\n",
	       sta->sta.addr, tid);

	ieee80211_stop_tx_ba_session(&sta->sta, tid);
	rcu_read_unlock();
}

void ieee80211_tx_ba_session_handle_start(struct sta_info *sta, int tid)
{
	struct tid_ampdu_tx *tid_tx;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u16 start_seq_num;
	int ret;

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	/*
	 * Start queuing up packets for this aggregation session.
	 * We're going to release them once the driver is OK with
	 * that.
	 */
	clear_bit(HT_AGG_STATE_WANT_START, &tid_tx->state);

	ieee80211_agg_stop_txq(sta, tid);

	/*
	 * Make sure no packets are being processed. This ensures that
	 * we have a valid starting sequence number and that in-flight
	 * packets have been flushed out and no packets for this TID
	 * will go into the driver during the ampdu_action call.
	 */
	synchronize_net();

	start_seq_num = sta->tid_seq[tid] >> 4;

	ret = drv_ampdu_action(local, sdata, IEEE80211_AMPDU_TX_START,
			       &sta->sta, tid, &start_seq_num, 0);
	if (ret) {
		ht_dbg(sdata,
		       "BA request denied - HW unavailable for %pM tid %d\n",
		       sta->sta.addr, tid);
		spin_lock_bh(&sta->lock);
		ieee80211_agg_splice_packets(sdata, tid_tx, tid);
		ieee80211_assign_tid_tx(sta, tid, NULL);
		ieee80211_agg_splice_finish(sdata, tid);
		spin_unlock_bh(&sta->lock);

		ieee80211_agg_start_txq(sta, tid, false);

		kfree_rcu(tid_tx, rcu_head);
		return;
	}

	/* activate the timer for the recipient's addBA response */
	mod_timer(&tid_tx->addba_resp_timer, jiffies + ADDBA_RESP_INTERVAL);
	ht_dbg(sdata, "activated addBA response timer on %pM tid %d\n",
	       sta->sta.addr, tid);

	spin_lock_bh(&sta->lock);
	sta->ampdu_mlme.last_addba_req_time[tid] = jiffies;
	sta->ampdu_mlme.addba_req_num[tid]++;
	spin_unlock_bh(&sta->lock);

	/* send AddBA request */
	ieee80211_send_addba_request(sdata, sta->sta.addr, tid,
				     tid_tx->dialog_token, start_seq_num,
				     local->hw.max_tx_aggregation_subframes,
				     tid_tx->timeout);
}

/*
 * After accepting the AddBA Response we activated a timer,
 * resetting it after each frame that we send.
 */
static void sta_tx_agg_session_timer_expired(unsigned long data)
{
	/* not an elegant detour, but there is no choice as the timer passes
	 * only one argument, and various sta_info are needed here, so init
	 * flow in sta_info_create gives the TID as data, while the timer_to_id
	 * array gives the sta through container_of */
	u8 *ptid = (u8 *)data;
	u8 *timer_to_id = ptid - *ptid;
	struct sta_info *sta = container_of(timer_to_id, struct sta_info,
					 timer_to_tid[0]);
	struct tid_ampdu_tx *tid_tx;
	unsigned long timeout;

	rcu_read_lock();
	tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[*ptid]);
	if (!tid_tx || test_bit(HT_AGG_STATE_STOPPING, &tid_tx->state)) {
		rcu_read_unlock();
		return;
	}

	timeout = tid_tx->last_tx + TU_TO_JIFFIES(tid_tx->timeout);
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&tid_tx->session_timer, timeout);
		rcu_read_unlock();
		return;
	}

	rcu_read_unlock();

	ht_dbg(sta->sdata, "tx session timer expired on %pM tid %d\n",
	       sta->sta.addr, (u16)*ptid);

	ieee80211_stop_tx_ba_session(&sta->sta, *ptid);
}

int ieee80211_start_tx_ba_session(struct ieee80211_sta *pubsta, u16 tid,
				  u16 timeout)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct tid_ampdu_tx *tid_tx;
	int ret = 0;

	trace_api_start_tx_ba_session(pubsta, tid);

	if (WARN(sta->reserved_tid == tid,
		 "Requested to start BA session on reserved tid=%d", tid))
		return -EINVAL;

	if (!pubsta->ht_cap.ht_supported)
		return -EINVAL;

	if (WARN_ON_ONCE(!local->ops->ampdu_action))
		return -EINVAL;

	if ((tid >= IEEE80211_NUM_TIDS) ||
	    !ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION) ||
	    ieee80211_hw_check(&local->hw, TX_AMPDU_SETUP_IN_HW))
		return -EINVAL;

	ht_dbg(sdata, "Open BA session requested for %pM tid %u\n",
	       pubsta->addr, tid);

	if (sdata->vif.type != NL80211_IFTYPE_STATION &&
	    sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
	    sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
	    sdata->vif.type != NL80211_IFTYPE_AP &&
	    sdata->vif.type != NL80211_IFTYPE_ADHOC)
		return -EINVAL;

	if (test_sta_flag(sta, WLAN_STA_BLOCK_BA)) {
		ht_dbg(sdata,
		       "BA sessions blocked - Denying BA session request %pM tid %d\n",
		       sta->sta.addr, tid);
		return -EINVAL;
	}

	/*
	 * 802.11n-2009 11.5.1.1: If the initiating STA is an HT STA, is a
	 * member of an IBSS, and has no other existing Block Ack agreement
	 * with the recipient STA, then the initiating STA shall transmit a
	 * Probe Request frame to the recipient STA and shall not transmit an
	 * ADDBA Request frame unless it receives a Probe Response frame
	 * from the recipient within dot11ADDBAFailureTimeout.
	 *
	 * The probe request mechanism for ADDBA is currently not implemented,
	 * but we only build up Block Ack session with HT STAs. This information
	 * is set when we receive a bss info from a probe response or a beacon.
	 */
	if (sta->sdata->vif.type == NL80211_IFTYPE_ADHOC &&
	    !sta->sta.ht_cap.ht_supported) {
		ht_dbg(sdata,
		       "BA request denied - IBSS STA %pM does not advertise HT support\n",
		       pubsta->addr);
		return -EINVAL;
	}

	spin_lock_bh(&sta->lock);

	/* we have tried too many times, receiver does not want A-MPDU */
	if (sta->ampdu_mlme.addba_req_num[tid] > HT_AGG_MAX_RETRIES) {
		ret = -EBUSY;
		goto err_unlock_sta;
	}

	/*
	 * if we have tried more than HT_AGG_BURST_RETRIES times we
	 * will spread our requests in time to avoid stalling connection
	 * for too long
	 */
	if (sta->ampdu_mlme.addba_req_num[tid] > HT_AGG_BURST_RETRIES &&
	    time_before(jiffies, sta->ampdu_mlme.last_addba_req_time[tid] +
			HT_AGG_RETRIES_PERIOD)) {
		ht_dbg(sdata,
		       "BA request denied - waiting a grace period after %d failed requests on %pM tid %u\n",
		       sta->ampdu_mlme.addba_req_num[tid], sta->sta.addr, tid);
		ret = -EBUSY;
		goto err_unlock_sta;
	}

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);
	/* check if the TID is not in aggregation flow already */
	if (tid_tx || sta->ampdu_mlme.tid_start_tx[tid]) {
		ht_dbg(sdata,
		       "BA request denied - session is not idle on %pM tid %u\n",
		       sta->sta.addr, tid);
		ret = -EAGAIN;
		goto err_unlock_sta;
	}

	/* prepare A-MPDU MLME for Tx aggregation */
	tid_tx = kzalloc(sizeof(struct tid_ampdu_tx), GFP_ATOMIC);
	if (!tid_tx) {
		ret = -ENOMEM;
		goto err_unlock_sta;
	}

	skb_queue_head_init(&tid_tx->pending);
	__set_bit(HT_AGG_STATE_WANT_START, &tid_tx->state);

	tid_tx->timeout = timeout;

	/* response timer */
	tid_tx->addba_resp_timer.function = sta_addba_resp_timer_expired;
	tid_tx->addba_resp_timer.data = (unsigned long)&sta->timer_to_tid[tid];
	init_timer(&tid_tx->addba_resp_timer);

	/* tx timer */
	tid_tx->session_timer.function = sta_tx_agg_session_timer_expired;
	tid_tx->session_timer.data = (unsigned long)&sta->timer_to_tid[tid];
	init_timer_deferrable(&tid_tx->session_timer);

	/* assign a dialog token */
	sta->ampdu_mlme.dialog_token_allocator++;
	tid_tx->dialog_token = sta->ampdu_mlme.dialog_token_allocator;

	/*
	 * Finally, assign it to the start array; the work item will
	 * collect it and move it to the normal array.
	 */
	sta->ampdu_mlme.tid_start_tx[tid] = tid_tx;

	ieee80211_queue_work(&local->hw, &sta->ampdu_mlme.work);

	/* this flow continues off the work */
 err_unlock_sta:
	spin_unlock_bh(&sta->lock);
	return ret;
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_session);

static void ieee80211_agg_tx_operational(struct ieee80211_local *local,
					 struct sta_info *sta, u16 tid)
{
	struct tid_ampdu_tx *tid_tx;

	lockdep_assert_held(&sta->ampdu_mlme.mtx);

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	ht_dbg(sta->sdata, "Aggregation is on for %pM tid %d\n",
	       sta->sta.addr, tid);

	drv_ampdu_action(local, sta->sdata,
			 IEEE80211_AMPDU_TX_OPERATIONAL,
			 &sta->sta, tid, NULL, tid_tx->buf_size);

	/*
	 * synchronize with TX path, while splicing the TX path
	 * should block so it won't put more packets onto pending.
	 */
	spin_lock_bh(&sta->lock);

	ieee80211_agg_splice_packets(sta->sdata, tid_tx, tid);
	/*
	 * Now mark as operational. This will be visible
	 * in the TX path, and lets it go lock-free in
	 * the common case.
	 */
	set_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state);
	ieee80211_agg_splice_finish(sta->sdata, tid);

	spin_unlock_bh(&sta->lock);

	ieee80211_agg_start_txq(sta, tid, true);
}

void ieee80211_start_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u16 tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct tid_ampdu_tx *tid_tx;

	trace_api_start_tx_ba_cb(sdata, ra, tid);

	if (tid >= IEEE80211_NUM_TIDS) {
		ht_dbg(sdata, "Bad TID value: tid = %d (>= %d)\n",
		       tid, IEEE80211_NUM_TIDS);
		return;
	}

	mutex_lock(&local->sta_mtx);
	sta = sta_info_get_bss(sdata, ra);
	if (!sta) {
		mutex_unlock(&local->sta_mtx);
		ht_dbg(sdata, "Could not find station: %pM\n", ra);
		return;
	}

	mutex_lock(&sta->ampdu_mlme.mtx);
	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	if (WARN_ON(!tid_tx)) {
		ht_dbg(sdata, "addBA was not requested!\n");
		goto unlock;
	}

	if (WARN_ON(test_and_set_bit(HT_AGG_STATE_DRV_READY, &tid_tx->state)))
		goto unlock;

	if (test_bit(HT_AGG_STATE_RESPONSE_RECEIVED, &tid_tx->state))
		ieee80211_agg_tx_operational(local, sta, tid);

 unlock:
	mutex_unlock(&sta->ampdu_mlme.mtx);
	mutex_unlock(&local->sta_mtx);
}

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif,
				      const u8 *ra, u16 tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb))
		return;

	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_SDATA_QUEUE_AGG_START;
	skb_queue_tail(&sdata->skb_queue, skb);
	ieee80211_queue_work(&local->hw, &sdata->work);
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb_irqsafe);

int __ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				   enum ieee80211_agg_stop_reason reason)
{
	int ret;

	mutex_lock(&sta->ampdu_mlme.mtx);

	ret = ___ieee80211_stop_tx_ba_session(sta, tid, reason);

	mutex_unlock(&sta->ampdu_mlme.mtx);

	return ret;
}

int ieee80211_stop_tx_ba_session(struct ieee80211_sta *pubsta, u16 tid)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct tid_ampdu_tx *tid_tx;
	int ret = 0;

	trace_api_stop_tx_ba_session(pubsta, tid);

	if (!local->ops->ampdu_action)
		return -EINVAL;

	if (tid >= IEEE80211_NUM_TIDS)
		return -EINVAL;

	spin_lock_bh(&sta->lock);
	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	if (!tid_tx) {
		ret = -ENOENT;
		goto unlock;
	}

	WARN(sta->reserved_tid == tid,
	     "Requested to stop BA session on reserved tid=%d", tid);

	if (test_bit(HT_AGG_STATE_STOPPING, &tid_tx->state)) {
		/* already in progress stopping it */
		ret = 0;
		goto unlock;
	}

	set_bit(HT_AGG_STATE_WANT_STOP, &tid_tx->state);
	ieee80211_queue_work(&local->hw, &sta->ampdu_mlme.work);

 unlock:
	spin_unlock_bh(&sta->lock);
	return ret;
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_session);

void ieee80211_stop_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u8 tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct tid_ampdu_tx *tid_tx;
	bool send_delba = false;

	trace_api_stop_tx_ba_cb(sdata, ra, tid);

	if (tid >= IEEE80211_NUM_TIDS) {
		ht_dbg(sdata, "Bad TID value: tid = %d (>= %d)\n",
		       tid, IEEE80211_NUM_TIDS);
		return;
	}

	ht_dbg(sdata, "Stopping Tx BA session for %pM tid %d\n", ra, tid);

	mutex_lock(&local->sta_mtx);

	sta = sta_info_get_bss(sdata, ra);
	if (!sta) {
		ht_dbg(sdata, "Could not find station: %pM\n", ra);
		goto unlock;
	}

	mutex_lock(&sta->ampdu_mlme.mtx);
	spin_lock_bh(&sta->lock);
	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

	if (!tid_tx || !test_bit(HT_AGG_STATE_STOPPING, &tid_tx->state)) {
		ht_dbg(sdata,
		       "unexpected callback to A-MPDU stop for %pM tid %d\n",
		       sta->sta.addr, tid);
		goto unlock_sta;
	}

	if (tid_tx->stop_initiator == WLAN_BACK_INITIATOR && tid_tx->tx_stop)
		send_delba = true;

	ieee80211_remove_tid_tx(sta, tid);

 unlock_sta:
	spin_unlock_bh(&sta->lock);

	if (send_delba)
		ieee80211_send_delba(sdata, ra, tid,
			WLAN_BACK_INITIATOR, WLAN_REASON_QSTA_NOT_USE);

	mutex_unlock(&sta->ampdu_mlme.mtx);
 unlock:
	mutex_unlock(&local->sta_mtx);
}

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif,
				     const u8 *ra, u16 tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb))
		return;

	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_SDATA_QUEUE_AGG_STOP;
	skb_queue_tail(&sdata->skb_queue, skb);
	ieee80211_queue_work(&local->hw, &sdata->work);
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb_irqsafe);


void ieee80211_process_addba_resp(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct ieee80211_mgmt *mgmt,
				  size_t len)
{
	struct tid_ampdu_tx *tid_tx;
	u16 capab, tid;
	u8 buf_size;

	capab = le16_to_cpu(mgmt->u.action.u.addba_resp.capab);
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
	buf_size = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;

	mutex_lock(&sta->ampdu_mlme.mtx);

	tid_tx = rcu_dereference_protected_tid_tx(sta, tid);
	if (!tid_tx)
		goto out;

	if (mgmt->u.action.u.addba_resp.dialog_token != tid_tx->dialog_token) {
		ht_dbg(sta->sdata, "wrong addBA response token, %pM tid %d\n",
		       sta->sta.addr, tid);
		goto out;
	}

	del_timer_sync(&tid_tx->addba_resp_timer);

	ht_dbg(sta->sdata, "switched off addBA timer for %pM tid %d\n",
	       sta->sta.addr, tid);

	/*
	 * addba_resp_timer may have fired before we got here, and
	 * caused WANT_STOP to be set. If the stop then was already
	 * processed further, STOPPING might be set.
	 */
	if (test_bit(HT_AGG_STATE_WANT_STOP, &tid_tx->state) ||
	    test_bit(HT_AGG_STATE_STOPPING, &tid_tx->state)) {
		ht_dbg(sta->sdata,
		       "got addBA resp for %pM tid %d but we already gave up\n",
		       sta->sta.addr, tid);
		goto out;
	}

	/*
	 * IEEE 802.11-2007 7.3.1.14:
	 * In an ADDBA Response frame, when the Status Code field
	 * is set to 0, the Buffer Size subfield is set to a value
	 * of at least 1.
	 */
	if (le16_to_cpu(mgmt->u.action.u.addba_resp.status)
			== WLAN_STATUS_SUCCESS && buf_size) {
		if (test_and_set_bit(HT_AGG_STATE_RESPONSE_RECEIVED,
				     &tid_tx->state)) {
			/* ignore duplicate response */
			goto out;
		}

		tid_tx->buf_size = buf_size;

		if (test_bit(HT_AGG_STATE_DRV_READY, &tid_tx->state))
			ieee80211_agg_tx_operational(local, sta, tid);

		sta->ampdu_mlme.addba_req_num[tid] = 0;

		if (tid_tx->timeout) {
			mod_timer(&tid_tx->session_timer,
				  TU_TO_EXP_TIME(tid_tx->timeout));
			tid_tx->last_tx = jiffies;
		}

	} else {
		___ieee80211_stop_tx_ba_session(sta, tid, AGG_STOP_DECLINED);
	}

 out:
	mutex_unlock(&sta->ampdu_mlme.mtx);
}
