// SPDX-License-Identifier: GPL-2.0-only
/*
 * HT handling
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2010, Intel Corporation
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2021 Intel Corporation
 */

/**
 * DOC: RX A-MPDU aggregation
 *
 * Aggregation on the RX side requires only implementing the
 * @ampdu_action callback that is invoked to start/stop any
 * block-ack sessions for RX aggregation.
 *
 * When RX aggregation is started by the peer, the driver is
 * notified via @ampdu_action function, with the
 * %IEEE80211_AMPDU_RX_START action, and may reject the request
 * in which case a negative response is sent to the peer, if it
 * accepts it a positive response is sent.
 *
 * While the session is active, the device/driver are required
 * to de-aggregate frames and pass them up one by one to mac80211,
 * which will handle the reorder buffer.
 *
 * When the aggregation session is stopped again by the peer or
 * ourselves, the driver's @ampdu_action function will be called
 * with the action %IEEE80211_AMPDU_RX_STOP. In this case, the
 * call must not fail.
 */

#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

static void ieee80211_free_tid_rx(struct rcu_head *h)
{
	struct tid_ampdu_rx *tid_rx =
		container_of(h, struct tid_ampdu_rx, rcu_head);
	int i;

	for (i = 0; i < tid_rx->buf_size; i++)
		__skb_queue_purge(&tid_rx->reorder_buf[i]);
	kfree(tid_rx->reorder_buf);
	kfree(tid_rx->reorder_time);
	kfree(tid_rx);
}

void ___ieee80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				     u16 initiator, u16 reason, bool tx)
{
	struct ieee80211_local *local = sta->local;
	struct tid_ampdu_rx *tid_rx;
	struct ieee80211_ampdu_params params = {
		.sta = &sta->sta,
		.action = IEEE80211_AMPDU_RX_STOP,
		.tid = tid,
		.amsdu = false,
		.timeout = 0,
		.ssn = 0,
	};

	lockdep_assert_held(&sta->ampdu_mlme.mtx);

	tid_rx = rcu_dereference_protected(sta->ampdu_mlme.tid_rx[tid],
					lockdep_is_held(&sta->ampdu_mlme.mtx));

	if (!test_bit(tid, sta->ampdu_mlme.agg_session_valid))
		return;

	RCU_INIT_POINTER(sta->ampdu_mlme.tid_rx[tid], NULL);
	__clear_bit(tid, sta->ampdu_mlme.agg_session_valid);

	ht_dbg(sta->sdata,
	       "Rx BA session stop requested for %pM tid %u %s reason: %d\n",
	       sta->sta.addr, tid,
	       initiator == WLAN_BACK_RECIPIENT ? "recipient" : "initiator",
	       (int)reason);

	if (drv_ampdu_action(local, sta->sdata, &params))
		sdata_info(sta->sdata,
			   "HW problem - can not stop rx aggregation for %pM tid %d\n",
			   sta->sta.addr, tid);

	/* check if this is a self generated aggregation halt */
	if (initiator == WLAN_BACK_RECIPIENT && tx)
		ieee80211_send_delba(sta->sdata, sta->sta.addr,
				     tid, WLAN_BACK_RECIPIENT, reason);

	/*
	 * return here in case tid_rx is not assigned - which will happen if
	 * IEEE80211_HW_SUPPORTS_REORDERING_BUFFER is set.
	 */
	if (!tid_rx)
		return;

	del_timer_sync(&tid_rx->session_timer);

	/* make sure ieee80211_sta_reorder_release() doesn't re-arm the timer */
	spin_lock_bh(&tid_rx->reorder_lock);
	tid_rx->removed = true;
	spin_unlock_bh(&tid_rx->reorder_lock);
	del_timer_sync(&tid_rx->reorder_timer);

	call_rcu(&tid_rx->rcu_head, ieee80211_free_tid_rx);
}

void __ieee80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				    u16 initiator, u16 reason, bool tx)
{
	mutex_lock(&sta->ampdu_mlme.mtx);
	___ieee80211_stop_rx_ba_session(sta, tid, initiator, reason, tx);
	mutex_unlock(&sta->ampdu_mlme.mtx);
}

void ieee80211_stop_rx_ba_session(struct ieee80211_vif *vif, u16 ba_rx_bitmap,
				  const u8 *addr)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct sta_info *sta;
	int i;

	rcu_read_lock();
	sta = sta_info_get_bss(sdata, addr);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		if (ba_rx_bitmap & BIT(i))
			set_bit(i, sta->ampdu_mlme.tid_rx_stop_requested);

	ieee80211_queue_work(&sta->local->hw, &sta->ampdu_mlme.work);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_stop_rx_ba_session);

/*
 * After accepting the AddBA Request we activated a timer,
 * resetting it after each frame that arrives from the originator.
 */
static void sta_rx_agg_session_timer_expired(struct timer_list *t)
{
	struct tid_ampdu_rx *tid_rx = from_timer(tid_rx, t, session_timer);
	struct sta_info *sta = tid_rx->sta;
	u8 tid = tid_rx->tid;
	unsigned long timeout;

	timeout = tid_rx->last_rx + TU_TO_JIFFIES(tid_rx->timeout);
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&tid_rx->session_timer, timeout);
		return;
	}

	ht_dbg(sta->sdata, "RX session timer expired on %pM tid %d\n",
	       sta->sta.addr, tid);

	set_bit(tid, sta->ampdu_mlme.tid_rx_timer_expired);
	ieee80211_queue_work(&sta->local->hw, &sta->ampdu_mlme.work);
}

static void sta_rx_agg_reorder_timer_expired(struct timer_list *t)
{
	struct tid_ampdu_rx *tid_rx = from_timer(tid_rx, t, reorder_timer);

	rcu_read_lock();
	ieee80211_release_reorder_timeout(tid_rx->sta, tid_rx->tid);
	rcu_read_unlock();
}

static void ieee80211_add_addbaext(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb,
				   const struct ieee80211_addba_ext_ie *req,
				   u16 buf_size)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_addba_ext_ie *resp;
	const struct ieee80211_sta_he_cap *he_cap;
	u8 frag_level, cap_frag_level;
	u8 *pos;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return;
	he_cap = ieee80211_get_he_iftype_cap(sband,
					     ieee80211_vif_type_p2p(&sdata->vif));
	if (!he_cap)
		return;

	pos = skb_put_zero(skb, 2 + sizeof(struct ieee80211_addba_ext_ie));
	*pos++ = WLAN_EID_ADDBA_EXT;
	*pos++ = sizeof(struct ieee80211_addba_ext_ie);
	resp = (struct ieee80211_addba_ext_ie *)pos;
	resp->data = req->data & IEEE80211_ADDBA_EXT_NO_FRAG;

	frag_level = u32_get_bits(req->data,
				  IEEE80211_ADDBA_EXT_FRAG_LEVEL_MASK);
	cap_frag_level = u32_get_bits(he_cap->he_cap_elem.mac_cap_info[0],
				      IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_MASK);
	if (frag_level > cap_frag_level)
		frag_level = cap_frag_level;
	resp->data |= u8_encode_bits(frag_level,
				     IEEE80211_ADDBA_EXT_FRAG_LEVEL_MASK);
	resp->data |= u8_encode_bits(buf_size >> IEEE80211_ADDBA_EXT_BUF_SIZE_SHIFT,
				     IEEE80211_ADDBA_EXT_BUF_SIZE_MASK);
}

static void ieee80211_send_addba_resp(struct sta_info *sta, u8 *da, u16 tid,
				      u8 dialog_token, u16 status, u16 policy,
				      u16 buf_size, u16 timeout,
				      const struct ieee80211_addba_ext_ie *addbaext)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	bool amsdu = ieee80211_hw_check(&local->hw, SUPPORTS_AMSDU_IN_AMPDU);
	u16 capab;

	skb = dev_alloc_skb(sizeof(*mgmt) +
		    2 + sizeof(struct ieee80211_addba_ext_ie) +
		    local->hw.extra_tx_headroom);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = skb_put_zero(skb, 24);
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

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.addba_resp));
	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.addba_resp.action_code = WLAN_ACTION_ADDBA_RESP;
	mgmt->u.action.u.addba_resp.dialog_token = dialog_token;

	capab = u16_encode_bits(amsdu, IEEE80211_ADDBA_PARAM_AMSDU_MASK);
	capab |= u16_encode_bits(policy, IEEE80211_ADDBA_PARAM_POLICY_MASK);
	capab |= u16_encode_bits(tid, IEEE80211_ADDBA_PARAM_TID_MASK);
	capab |= u16_encode_bits(buf_size, IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK);

	mgmt->u.action.u.addba_resp.capab = cpu_to_le16(capab);
	mgmt->u.action.u.addba_resp.timeout = cpu_to_le16(timeout);
	mgmt->u.action.u.addba_resp.status = cpu_to_le16(status);

	if (sta->sta.deflink.he_cap.has_he && addbaext)
		ieee80211_add_addbaext(sdata, skb, addbaext, buf_size);

	ieee80211_tx_skb(sdata, skb);
}

void ___ieee80211_start_rx_ba_session(struct sta_info *sta,
				      u8 dialog_token, u16 timeout,
				      u16 start_seq_num, u16 ba_policy, u16 tid,
				      u16 buf_size, bool tx, bool auto_seq,
				      const struct ieee80211_addba_ext_ie *addbaext)
{
	struct ieee80211_local *local = sta->sdata->local;
	struct tid_ampdu_rx *tid_agg_rx;
	struct ieee80211_ampdu_params params = {
		.sta = &sta->sta,
		.action = IEEE80211_AMPDU_RX_START,
		.tid = tid,
		.amsdu = false,
		.timeout = timeout,
		.ssn = start_seq_num,
	};
	int i, ret = -EOPNOTSUPP;
	u16 status = WLAN_STATUS_REQUEST_DECLINED;
	u16 max_buf_size;

	if (tid >= IEEE80211_FIRST_TSPEC_TSID) {
		ht_dbg(sta->sdata,
		       "STA %pM requests BA session on unsupported tid %d\n",
		       sta->sta.addr, tid);
		goto end;
	}

	if (!sta->sta.deflink.ht_cap.ht_supported &&
	    sta->sdata->vif.bss_conf.chandef.chan->band != NL80211_BAND_6GHZ) {
		ht_dbg(sta->sdata,
		       "STA %pM erroneously requests BA session on tid %d w/o QoS\n",
		       sta->sta.addr, tid);
		/* send a response anyway, it's an error case if we get here */
		goto end;
	}

	if (test_sta_flag(sta, WLAN_STA_BLOCK_BA)) {
		ht_dbg(sta->sdata,
		       "Suspend in progress - Denying ADDBA request (%pM tid %d)\n",
		       sta->sta.addr, tid);
		goto end;
	}

	if (sta->sta.deflink.eht_cap.has_eht)
		max_buf_size = IEEE80211_MAX_AMPDU_BUF_EHT;
	else if (sta->sta.deflink.he_cap.has_he)
		max_buf_size = IEEE80211_MAX_AMPDU_BUF_HE;
	else
		max_buf_size = IEEE80211_MAX_AMPDU_BUF_HT;

	/* sanity check for incoming parameters:
	 * check if configuration can support the BA policy
	 * and if buffer size does not exceeds max value */
	/* XXX: check own ht delayed BA capability?? */
	if (((ba_policy != 1) &&
	     (!(sta->sta.deflink.ht_cap.cap & IEEE80211_HT_CAP_DELAY_BA))) ||
	    (buf_size > max_buf_size)) {
		status = WLAN_STATUS_INVALID_QOS_PARAM;
		ht_dbg_ratelimited(sta->sdata,
				   "AddBA Req with bad params from %pM on tid %u. policy %d, buffer size %d\n",
				   sta->sta.addr, tid, ba_policy, buf_size);
		goto end;
	}
	/* determine default buffer size */
	if (buf_size == 0)
		buf_size = max_buf_size;

	/* make sure the size doesn't exceed the maximum supported by the hw */
	if (buf_size > sta->sta.max_rx_aggregation_subframes)
		buf_size = sta->sta.max_rx_aggregation_subframes;
	params.buf_size = buf_size;

	ht_dbg(sta->sdata, "AddBA Req buf_size=%d for %pM\n",
	       buf_size, sta->sta.addr);

	/* examine state machine */
	lockdep_assert_held(&sta->ampdu_mlme.mtx);

	if (test_bit(tid, sta->ampdu_mlme.agg_session_valid)) {
		if (sta->ampdu_mlme.tid_rx_token[tid] == dialog_token) {
			struct tid_ampdu_rx *tid_rx;

			ht_dbg_ratelimited(sta->sdata,
					   "updated AddBA Req from %pM on tid %u\n",
					   sta->sta.addr, tid);
			/* We have no API to update the timeout value in the
			 * driver so reject the timeout update if the timeout
			 * changed. If it did not change, i.e., no real update,
			 * just reply with success.
			 */
			rcu_read_lock();
			tid_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[tid]);
			if (tid_rx && tid_rx->timeout == timeout)
				status = WLAN_STATUS_SUCCESS;
			else
				status = WLAN_STATUS_REQUEST_DECLINED;
			rcu_read_unlock();
			goto end;
		}

		ht_dbg_ratelimited(sta->sdata,
				   "unexpected AddBA Req from %pM on tid %u\n",
				   sta->sta.addr, tid);

		/* delete existing Rx BA session on the same tid */
		___ieee80211_stop_rx_ba_session(sta, tid, WLAN_BACK_RECIPIENT,
						WLAN_STATUS_UNSPECIFIED_QOS,
						false);
	}

	if (ieee80211_hw_check(&local->hw, SUPPORTS_REORDERING_BUFFER)) {
		ret = drv_ampdu_action(local, sta->sdata, &params);
		ht_dbg(sta->sdata,
		       "Rx A-MPDU request on %pM tid %d result %d\n",
		       sta->sta.addr, tid, ret);
		if (!ret)
			status = WLAN_STATUS_SUCCESS;
		goto end;
	}

	/* prepare A-MPDU MLME for Rx aggregation */
	tid_agg_rx = kzalloc(sizeof(*tid_agg_rx), GFP_KERNEL);
	if (!tid_agg_rx)
		goto end;

	spin_lock_init(&tid_agg_rx->reorder_lock);

	/* rx timer */
	timer_setup(&tid_agg_rx->session_timer,
		    sta_rx_agg_session_timer_expired, TIMER_DEFERRABLE);

	/* rx reorder timer */
	timer_setup(&tid_agg_rx->reorder_timer,
		    sta_rx_agg_reorder_timer_expired, 0);

	/* prepare reordering buffer */
	tid_agg_rx->reorder_buf =
		kcalloc(buf_size, sizeof(struct sk_buff_head), GFP_KERNEL);
	tid_agg_rx->reorder_time =
		kcalloc(buf_size, sizeof(unsigned long), GFP_KERNEL);
	if (!tid_agg_rx->reorder_buf || !tid_agg_rx->reorder_time) {
		kfree(tid_agg_rx->reorder_buf);
		kfree(tid_agg_rx->reorder_time);
		kfree(tid_agg_rx);
		goto end;
	}

	for (i = 0; i < buf_size; i++)
		__skb_queue_head_init(&tid_agg_rx->reorder_buf[i]);

	ret = drv_ampdu_action(local, sta->sdata, &params);
	ht_dbg(sta->sdata, "Rx A-MPDU request on %pM tid %d result %d\n",
	       sta->sta.addr, tid, ret);
	if (ret) {
		kfree(tid_agg_rx->reorder_buf);
		kfree(tid_agg_rx->reorder_time);
		kfree(tid_agg_rx);
		goto end;
	}

	/* update data */
	tid_agg_rx->ssn = start_seq_num;
	tid_agg_rx->head_seq_num = start_seq_num;
	tid_agg_rx->buf_size = buf_size;
	tid_agg_rx->timeout = timeout;
	tid_agg_rx->stored_mpdu_num = 0;
	tid_agg_rx->auto_seq = auto_seq;
	tid_agg_rx->started = false;
	tid_agg_rx->reorder_buf_filtered = 0;
	tid_agg_rx->tid = tid;
	tid_agg_rx->sta = sta;
	status = WLAN_STATUS_SUCCESS;

	/* activate it for RX */
	rcu_assign_pointer(sta->ampdu_mlme.tid_rx[tid], tid_agg_rx);

	if (timeout) {
		mod_timer(&tid_agg_rx->session_timer, TU_TO_EXP_TIME(timeout));
		tid_agg_rx->last_rx = jiffies;
	}

end:
	if (status == WLAN_STATUS_SUCCESS) {
		__set_bit(tid, sta->ampdu_mlme.agg_session_valid);
		__clear_bit(tid, sta->ampdu_mlme.unexpected_agg);
		sta->ampdu_mlme.tid_rx_token[tid] = dialog_token;
	}

	if (tx)
		ieee80211_send_addba_resp(sta, sta->sta.addr, tid,
					  dialog_token, status, 1, buf_size,
					  timeout, addbaext);
}

static void __ieee80211_start_rx_ba_session(struct sta_info *sta,
					    u8 dialog_token, u16 timeout,
					    u16 start_seq_num, u16 ba_policy,
					    u16 tid, u16 buf_size, bool tx,
					    bool auto_seq,
					    const struct ieee80211_addba_ext_ie *addbaext)
{
	mutex_lock(&sta->ampdu_mlme.mtx);
	___ieee80211_start_rx_ba_session(sta, dialog_token, timeout,
					 start_seq_num, ba_policy, tid,
					 buf_size, tx, auto_seq, addbaext);
	mutex_unlock(&sta->ampdu_mlme.mtx);
}

void ieee80211_process_addba_request(struct ieee80211_local *local,
				     struct sta_info *sta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u16 capab, tid, timeout, ba_policy, buf_size, start_seq_num;
	struct ieee802_11_elems *elems = NULL;
	u8 dialog_token;
	int ies_len;

	/* extract session parameters from addba request frame */
	dialog_token = mgmt->u.action.u.addba_req.dialog_token;
	timeout = le16_to_cpu(mgmt->u.action.u.addba_req.timeout);
	start_seq_num =
		le16_to_cpu(mgmt->u.action.u.addba_req.start_seq_num) >> 4;

	capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
	ba_policy = (capab & IEEE80211_ADDBA_PARAM_POLICY_MASK) >> 1;
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
	buf_size = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;

	ies_len = len - offsetof(struct ieee80211_mgmt,
				 u.action.u.addba_req.variable);
	if (ies_len) {
		elems = ieee802_11_parse_elems(mgmt->u.action.u.addba_req.variable,
					       ies_len, true, mgmt->bssid, NULL);
		if (!elems || elems->parse_error)
			goto free;
	}

	if (sta->sta.deflink.eht_cap.has_eht && elems && elems->addba_ext_ie) {
		u8 buf_size_1k = u8_get_bits(elems->addba_ext_ie->data,
					     IEEE80211_ADDBA_EXT_BUF_SIZE_MASK);

		buf_size |= buf_size_1k << IEEE80211_ADDBA_EXT_BUF_SIZE_SHIFT;
	}

	__ieee80211_start_rx_ba_session(sta, dialog_token, timeout,
					start_seq_num, ba_policy, tid,
					buf_size, true, false,
					elems ? elems->addba_ext_ie : NULL);
free:
	kfree(elems);
}

void ieee80211_manage_rx_ba_offl(struct ieee80211_vif *vif,
				 const u8 *addr, unsigned int tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	rcu_read_lock();
	sta = sta_info_get_bss(sdata, addr);
	if (!sta)
		goto unlock;

	set_bit(tid, sta->ampdu_mlme.tid_rx_manage_offl);
	ieee80211_queue_work(&local->hw, &sta->ampdu_mlme.work);
 unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_manage_rx_ba_offl);

void ieee80211_rx_ba_timer_expired(struct ieee80211_vif *vif,
				   const u8 *addr, unsigned int tid)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	rcu_read_lock();
	sta = sta_info_get_bss(sdata, addr);
	if (!sta)
		goto unlock;

	set_bit(tid, sta->ampdu_mlme.tid_rx_timer_expired);
	ieee80211_queue_work(&local->hw, &sta->ampdu_mlme.work);

 unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_rx_ba_timer_expired);
