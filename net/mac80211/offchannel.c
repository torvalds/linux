/*
 * Off-channel operation helpers
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/export.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

/*
 * Tell our hardware to disable PS.
 * Optionally inform AP that we will go to sleep so that it will buffer
 * the frames while we are doing off-channel work.  This is optional
 * because we *may* be doing work on-operating channel, and want our
 * hardware unconditionally awake, but still let the AP send us normal frames.
 */
static void ieee80211_offchannel_ps_enable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	local->offchannel_ps_enabled = false;

	/* FIXME: what to do when local->pspolling is true? */

	del_timer_sync(&local->dynamic_ps_timer);
	del_timer_sync(&ifmgd->bcn_mon_timer);
	del_timer_sync(&ifmgd->conn_mon_timer);

	cancel_work_sync(&local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->offchannel_ps_enabled = true;
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	if (!local->offchannel_ps_enabled ||
	    !ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK))
		/*
		 * If power save was enabled, no need to send a nullfunc
		 * frame because AP knows that we are sleeping. But if the
		 * hardware is creating the nullfunc frame for power save
		 * status (ie. IEEE80211_HW_PS_NULLFUNC_STACK is not
		 * enabled) and power save was enabled, the firmware just
		 * sent a null frame with power save disabled. So we need
		 * to send a new nullfunc frame to inform the AP that we
		 * are again sleeping.
		 */
		ieee80211_send_nullfunc(local, sdata, true);
}

/* inform AP that we are awake again, unless power save is enabled */
static void ieee80211_offchannel_ps_disable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	if (!local->ps_sdata)
		ieee80211_send_nullfunc(local, sdata, false);
	else if (local->offchannel_ps_enabled) {
		/*
		 * In !IEEE80211_HW_PS_NULLFUNC_STACK case the hardware
		 * will send a nullfunc frame with the powersave bit set
		 * even though the AP already knows that we are sleeping.
		 * This could be avoided by sending a null frame with power
		 * save bit disabled before enabling the power save, but
		 * this doesn't gain anything.
		 *
		 * When IEEE80211_HW_PS_NULLFUNC_STACK is enabled, no need
		 * to send a nullfunc frame because AP already knows that
		 * we are sleeping, let's just enable power save mode in
		 * hardware.
		 */
		/* TODO:  Only set hardware if CONF_PS changed?
		 * TODO:  Should we set offchannel_ps_enabled to false?
		 */
		local->hw.conf.flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	} else if (local->hw.conf.dynamic_ps_timeout > 0) {
		/*
		 * If IEEE80211_CONF_PS was not set and the dynamic_ps_timer
		 * had been running before leaving the operating channel,
		 * restart the timer now and send a nullfunc frame to inform
		 * the AP that we are awake.
		 */
		ieee80211_send_nullfunc(local, sdata, false);
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));
	}

	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);
}

void ieee80211_offchannel_stop_vifs(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	if (WARN_ON(local->use_chanctx))
		return;

	/*
	 * notify the AP about us leaving the channel and stop all
	 * STA interfaces.
	 */

	/*
	 * Stop queues and transmit all frames queued by the driver
	 * before sending nullfunc to enable powersave at the AP.
	 */
	ieee80211_stop_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL,
					false);
	ieee80211_flush_queues(local, NULL, false);

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

		/* Check to see if we should disable beaconing. */
		if (sdata->vif.bss_conf.enable_beacon) {
			set_bit(SDATA_STATE_OFFCHANNEL_BEACON_STOPPED,
				&sdata->state);
			sdata->vif.bss_conf.enable_beacon = false;
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
		}

		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    sdata->u.mgd.associated)
			ieee80211_offchannel_ps_enable(sdata);
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_return(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	if (WARN_ON(local->use_chanctx))
		return;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			clear_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

		if (!ieee80211_sdata_running(sdata))
			continue;

		/* Tell AP we're back */
		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    sdata->u.mgd.associated)
			ieee80211_offchannel_ps_disable(sdata);

		if (test_and_clear_bit(SDATA_STATE_OFFCHANNEL_BEACON_STOPPED,
				       &sdata->state)) {
			sdata->vif.bss_conf.enable_beacon = true;
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
		}
	}
	mutex_unlock(&local->iflist_mtx);

	ieee80211_wake_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL,
					false);
}

static void ieee80211_handle_roc_started(struct ieee80211_roc_work *roc)
{
	if (roc->notified)
		return;

	if (roc->mgmt_tx_cookie) {
		if (!WARN_ON(!roc->frame)) {
			ieee80211_tx_skb_tid_band(roc->sdata, roc->frame, 7,
						  roc->chan->band);
			roc->frame = NULL;
		}
	} else {
		cfg80211_ready_on_channel(&roc->sdata->wdev, roc->cookie,
					  roc->chan, roc->req_duration,
					  GFP_KERNEL);
	}

	roc->notified = true;
}

static void ieee80211_hw_roc_start(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_start);
	struct ieee80211_roc_work *roc, *dep, *tmp;

	mutex_lock(&local->mtx);

	if (list_empty(&local->roc_list))
		goto out_unlock;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (!roc->started)
		goto out_unlock;

	roc->hw_begun = true;
	roc->hw_start_time = local->hw_roc_start_time;

	ieee80211_handle_roc_started(roc);
	list_for_each_entry_safe(dep, tmp, &roc->dependents, list) {
		ieee80211_handle_roc_started(dep);

		if (dep->duration > roc->duration) {
			u32 dur = dep->duration;
			dep->duration = dur - roc->duration;
			roc->duration = dur;
			list_move(&dep->list, &roc->list);
		}
	}
 out_unlock:
	mutex_unlock(&local->mtx);
}

void ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw_roc_start_time = jiffies;

	trace_api_ready_on_channel(local);

	ieee80211_queue_work(hw, &local->hw_roc_start);
}
EXPORT_SYMBOL_GPL(ieee80211_ready_on_channel);

void ieee80211_start_next_roc(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc;

	lockdep_assert_held(&local->mtx);

	if (list_empty(&local->roc_list)) {
		ieee80211_run_deferred_scan(local);
		return;
	}

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (WARN_ON_ONCE(roc->started))
		return;

	if (local->ops->remain_on_channel) {
		int ret, duration = roc->duration;

		/* XXX: duplicated, see ieee80211_start_roc_work() */
		if (!duration)
			duration = 10;

		ret = drv_remain_on_channel(local, roc->sdata, roc->chan,
					    duration, roc->type);

		roc->started = true;

		if (ret) {
			wiphy_warn(local->hw.wiphy,
				   "failed to start next HW ROC (%d)\n", ret);
			/*
			 * queue the work struct again to avoid recursion
			 * when multiple failures occur
			 */
			ieee80211_remain_on_channel_expired(&local->hw);
		}
	} else {
		/* delay it a bit */
		ieee80211_queue_delayed_work(&local->hw, &roc->work,
					     round_jiffies_relative(HZ/2));
	}
}

static void ieee80211_roc_notify_destroy(struct ieee80211_roc_work *roc,
					 bool free)
{
	struct ieee80211_roc_work *dep, *tmp;

	if (WARN_ON(roc->to_be_freed))
		return;

	/* was never transmitted */
	if (roc->frame) {
		cfg80211_mgmt_tx_status(&roc->sdata->wdev, roc->mgmt_tx_cookie,
					roc->frame->data, roc->frame->len,
					false, GFP_KERNEL);
		ieee80211_free_txskb(&roc->sdata->local->hw, roc->frame);
	}

	if (!roc->mgmt_tx_cookie)
		cfg80211_remain_on_channel_expired(&roc->sdata->wdev,
						   roc->cookie, roc->chan,
						   GFP_KERNEL);

	list_for_each_entry_safe(dep, tmp, &roc->dependents, list)
		ieee80211_roc_notify_destroy(dep, true);

	if (free)
		kfree(roc);
	else
		roc->to_be_freed = true;
}

static void ieee80211_sw_roc_work(struct work_struct *work)
{
	struct ieee80211_roc_work *roc =
		container_of(work, struct ieee80211_roc_work, work.work);
	struct ieee80211_sub_if_data *sdata = roc->sdata;
	struct ieee80211_local *local = sdata->local;
	bool started, on_channel;

	mutex_lock(&local->mtx);

	if (roc->to_be_freed)
		goto out_unlock;

	if (roc->abort)
		goto finish;

	if (WARN_ON(list_empty(&local->roc_list)))
		goto out_unlock;

	if (WARN_ON(roc != list_first_entry(&local->roc_list,
					    struct ieee80211_roc_work,
					    list)))
		goto out_unlock;

	if (!roc->started) {
		struct ieee80211_roc_work *dep;

		WARN_ON(local->use_chanctx);

		/* If actually operating on the desired channel (with at least
		 * 20 MHz channel width) don't stop all the operations but still
		 * treat it as though the ROC operation started properly, so
		 * other ROC operations won't interfere with this one.
		 */
		roc->on_channel = roc->chan == local->_oper_chandef.chan &&
				  local->_oper_chandef.width != NL80211_CHAN_WIDTH_5 &&
				  local->_oper_chandef.width != NL80211_CHAN_WIDTH_10;

		/* start this ROC */
		ieee80211_recalc_idle(local);

		if (!roc->on_channel) {
			ieee80211_offchannel_stop_vifs(local);

			local->tmp_channel = roc->chan;
			ieee80211_hw_config(local, 0);
		}

		/* tell userspace or send frame */
		ieee80211_handle_roc_started(roc);
		list_for_each_entry(dep, &roc->dependents, list)
			ieee80211_handle_roc_started(dep);

		/* if it was pure TX, just finish right away */
		if (!roc->duration)
			goto finish;

		roc->started = true;
		ieee80211_queue_delayed_work(&local->hw, &roc->work,
					     msecs_to_jiffies(roc->duration));
	} else {
		/* finish this ROC */
 finish:
		list_del(&roc->list);
		started = roc->started;
		on_channel = roc->on_channel;
		ieee80211_roc_notify_destroy(roc, !roc->abort);

		if (started && !on_channel) {
			ieee80211_flush_queues(local, NULL, false);

			local->tmp_channel = NULL;
			ieee80211_hw_config(local, 0);

			ieee80211_offchannel_return(local);
		}

		ieee80211_recalc_idle(local);

		if (started)
			ieee80211_start_next_roc(local);
		else if (list_empty(&local->roc_list))
			ieee80211_run_deferred_scan(local);
	}

 out_unlock:
	mutex_unlock(&local->mtx);
}

static void ieee80211_hw_roc_done(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_done);
	struct ieee80211_roc_work *roc;

	mutex_lock(&local->mtx);

	if (list_empty(&local->roc_list))
		goto out_unlock;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (!roc->started)
		goto out_unlock;

	list_del(&roc->list);

	ieee80211_roc_notify_destroy(roc, true);

	/* if there's another roc, start it now */
	ieee80211_start_next_roc(local);

 out_unlock:
	mutex_unlock(&local->mtx);
}

void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_remain_on_channel_expired(local);

	ieee80211_queue_work(hw, &local->hw_roc_done);
}
EXPORT_SYMBOL_GPL(ieee80211_remain_on_channel_expired);

static bool ieee80211_coalesce_started_roc(struct ieee80211_local *local,
					   struct ieee80211_roc_work *new_roc,
					   struct ieee80211_roc_work *cur_roc)
{
	unsigned long now = jiffies;
	unsigned long remaining = cur_roc->hw_start_time +
				  msecs_to_jiffies(cur_roc->duration) -
				  now;

	if (WARN_ON(!cur_roc->started || !cur_roc->hw_begun))
		return false;

	/* if it doesn't fit entirely, schedule a new one */
	if (new_roc->duration > jiffies_to_msecs(remaining))
		return false;

	ieee80211_handle_roc_started(new_roc);

	/* add to dependents so we send the expired event properly */
	list_add_tail(&new_roc->list, &cur_roc->dependents);
	return true;
}

static int ieee80211_start_roc_work(struct ieee80211_local *local,
				    struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_channel *channel,
				    unsigned int duration, u64 *cookie,
				    struct sk_buff *txskb,
				    enum ieee80211_roc_type type)
{
	struct ieee80211_roc_work *roc, *tmp;
	bool queued = false;
	int ret;

	lockdep_assert_held(&local->mtx);

	if (local->use_chanctx && !local->ops->remain_on_channel)
		return -EOPNOTSUPP;

	roc = kzalloc(sizeof(*roc), GFP_KERNEL);
	if (!roc)
		return -ENOMEM;

	/*
	 * If the duration is zero, then the driver
	 * wouldn't actually do anything. Set it to
	 * 10 for now.
	 *
	 * TODO: cancel the off-channel operation
	 *       when we get the SKB's TX status and
	 *       the wait time was zero before.
	 */
	if (!duration)
		duration = 10;

	roc->chan = channel;
	roc->duration = duration;
	roc->req_duration = duration;
	roc->frame = txskb;
	roc->type = type;
	roc->sdata = sdata;
	INIT_DELAYED_WORK(&roc->work, ieee80211_sw_roc_work);
	INIT_LIST_HEAD(&roc->dependents);

	/*
	 * cookie is either the roc cookie (for normal roc)
	 * or the SKB (for mgmt TX)
	 */
	if (!txskb) {
		roc->cookie = ieee80211_mgmt_tx_cookie(local);
		*cookie = roc->cookie;
	} else {
		roc->mgmt_tx_cookie = *cookie;
	}

	/* if there's one pending or we're scanning, queue this one */
	if (!list_empty(&local->roc_list) ||
	    local->scanning || ieee80211_is_radar_required(local))
		goto out_check_combine;

	/* if not HW assist, just queue & schedule work */
	if (!local->ops->remain_on_channel) {
		ieee80211_queue_delayed_work(&local->hw, &roc->work, 0);
		goto out_queue;
	}

	/* otherwise actually kick it off here (for error handling) */

	ret = drv_remain_on_channel(local, sdata, channel, duration, type);
	if (ret) {
		kfree(roc);
		return ret;
	}

	roc->started = true;
	goto out_queue;

 out_check_combine:
	list_for_each_entry(tmp, &local->roc_list, list) {
		if (tmp->chan != channel || tmp->sdata != sdata)
			continue;

		/*
		 * Extend this ROC if possible:
		 *
		 * If it hasn't started yet, just increase the duration
		 * and add the new one to the list of dependents.
		 * If the type of the new ROC has higher priority, modify the
		 * type of the previous one to match that of the new one.
		 */
		if (!tmp->started) {
			list_add_tail(&roc->list, &tmp->dependents);
			tmp->duration = max(tmp->duration, roc->duration);
			tmp->type = max(tmp->type, roc->type);
			queued = true;
			break;
		}

		/* If it has already started, it's more difficult ... */
		if (local->ops->remain_on_channel) {
			/*
			 * In the offloaded ROC case, if it hasn't begun, add
			 * this new one to the dependent list to be handled
			 * when the master one begins. If it has begun,
			 * check if it fits entirely within the existing one,
			 * in which case it will just be dependent as well.
			 * Otherwise, schedule it by itself.
			 */
			if (!tmp->hw_begun) {
				list_add_tail(&roc->list, &tmp->dependents);
				queued = true;
				break;
			}

			if (ieee80211_coalesce_started_roc(local, roc, tmp))
				queued = true;
		} else if (del_timer_sync(&tmp->work.timer)) {
			unsigned long new_end;

			/*
			 * In the software ROC case, cancel the timer, if
			 * that fails then the finish work is already
			 * queued/pending and thus we queue the new ROC
			 * normally, if that succeeds then we can extend
			 * the timer duration and TX the frame (if any.)
			 */

			list_add_tail(&roc->list, &tmp->dependents);
			queued = true;

			new_end = jiffies + msecs_to_jiffies(roc->duration);

			/* ok, it was started & we canceled timer */
			if (time_after(new_end, tmp->work.timer.expires))
				mod_timer(&tmp->work.timer, new_end);
			else
				add_timer(&tmp->work.timer);

			ieee80211_handle_roc_started(roc);
		}
		break;
	}

 out_queue:
	if (!queued)
		list_add_tail(&roc->list, &local->roc_list);

	return 0;
}

int ieee80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct ieee80211_channel *chan,
				unsigned int duration, u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;
	int ret;

	mutex_lock(&local->mtx);
	ret = ieee80211_start_roc_work(local, sdata, chan,
				       duration, cookie, NULL,
				       IEEE80211_ROC_TYPE_NORMAL);
	mutex_unlock(&local->mtx);

	return ret;
}

static int ieee80211_cancel_roc(struct ieee80211_local *local,
				u64 cookie, bool mgmt_tx)
{
	struct ieee80211_roc_work *roc, *tmp, *found = NULL;
	int ret;

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		struct ieee80211_roc_work *dep, *tmp2;

		list_for_each_entry_safe(dep, tmp2, &roc->dependents, list) {
			if (!mgmt_tx && dep->cookie != cookie)
				continue;
			else if (mgmt_tx && dep->mgmt_tx_cookie != cookie)
				continue;
			/* found dependent item -- just remove it */
			list_del(&dep->list);
			mutex_unlock(&local->mtx);

			ieee80211_roc_notify_destroy(dep, true);
			return 0;
		}

		if (!mgmt_tx && roc->cookie != cookie)
			continue;
		else if (mgmt_tx && roc->mgmt_tx_cookie != cookie)
			continue;

		found = roc;
		break;
	}

	if (!found) {
		mutex_unlock(&local->mtx);
		return -ENOENT;
	}

	/*
	 * We found the item to cancel, so do that. Note that it
	 * may have dependents, which we also cancel (and send
	 * the expired signal for.) Not doing so would be quite
	 * tricky here, but we may need to fix it later.
	 */

	if (local->ops->remain_on_channel) {
		if (found->started) {
			ret = drv_cancel_remain_on_channel(local);
			if (WARN_ON_ONCE(ret)) {
				mutex_unlock(&local->mtx);
				return ret;
			}
		}

		list_del(&found->list);

		if (found->started)
			ieee80211_start_next_roc(local);
		mutex_unlock(&local->mtx);

		ieee80211_roc_notify_destroy(found, true);
	} else {
		/* work may be pending so use it all the time */
		found->abort = true;
		ieee80211_queue_delayed_work(&local->hw, &found->work, 0);

		mutex_unlock(&local->mtx);

		/* work will clean up etc */
		flush_delayed_work(&found->work);
		WARN_ON(!found->to_be_freed);
		kfree(found);
	}

	return 0;
}

int ieee80211_cancel_remain_on_channel(struct wiphy *wiphy,
				       struct wireless_dev *wdev, u64 cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;

	return ieee80211_cancel_roc(local, cookie, false);
}

int ieee80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct cfg80211_mgmt_tx_params *params, u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct sta_info *sta;
	const struct ieee80211_mgmt *mgmt = (void *)params->buf;
	bool need_offchan = false;
	u32 flags;
	int ret;
	u8 *data;

	if (params->dont_wait_for_ack)
		flags = IEEE80211_TX_CTL_NO_ACK;
	else
		flags = IEEE80211_TX_INTFL_NL80211_FRAME_TX |
			IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (params->no_cck)
		flags |= IEEE80211_TX_CTL_NO_CCK_RATE;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
		if (!sdata->vif.bss_conf.ibss_joined)
			need_offchan = true;
		/* fall through */
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif) &&
		    !sdata->u.mesh.mesh_id_len)
			need_offchan = true;
		/* fall through */
#endif
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		if (sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		    !ieee80211_vif_is_mesh(&sdata->vif) &&
		    !rcu_access_pointer(sdata->bss->beacon))
			need_offchan = true;
		if (!ieee80211_is_action(mgmt->frame_control) ||
		    mgmt->u.action.category == WLAN_CATEGORY_PUBLIC ||
		    mgmt->u.action.category == WLAN_CATEGORY_SELF_PROTECTED ||
		    mgmt->u.action.category == WLAN_CATEGORY_SPECTRUM_MGMT)
			break;
		rcu_read_lock();
		sta = sta_info_get(sdata, mgmt->da);
		rcu_read_unlock();
		if (!sta)
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		sdata_lock(sdata);
		if (!sdata->u.mgd.associated ||
		    (params->offchan && params->wait &&
		     local->ops->remain_on_channel &&
		     memcmp(sdata->u.mgd.associated->bssid,
			    mgmt->bssid, ETH_ALEN)))
			need_offchan = true;
		sdata_unlock(sdata);
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		need_offchan = true;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* configurations requiring offchan cannot work if no channel has been
	 * specified
	 */
	if (need_offchan && !params->chan)
		return -EINVAL;

	mutex_lock(&local->mtx);

	/* Check if the operating channel is the requested channel */
	if (!need_offchan) {
		struct ieee80211_chanctx_conf *chanctx_conf;

		rcu_read_lock();
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);

		if (chanctx_conf) {
			need_offchan = params->chan &&
				       (params->chan !=
					chanctx_conf->def.chan);
		} else if (!params->chan) {
			ret = -EINVAL;
			rcu_read_unlock();
			goto out_unlock;
		} else {
			need_offchan = true;
		}
		rcu_read_unlock();
	}

	if (need_offchan && !params->offchan) {
		ret = -EBUSY;
		goto out_unlock;
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + params->len);
	if (!skb) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	data = skb_put(skb, params->len);
	memcpy(data, params->buf, params->len);

	/* Update CSA counters */
	if (sdata->vif.csa_active &&
	    (sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_MESH_POINT ||
	     sdata->vif.type == NL80211_IFTYPE_ADHOC) &&
	    params->n_csa_offsets) {
		int i;
		struct beacon_data *beacon = NULL;

		rcu_read_lock();

		if (sdata->vif.type == NL80211_IFTYPE_AP)
			beacon = rcu_dereference(sdata->u.ap.beacon);
		else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
			beacon = rcu_dereference(sdata->u.ibss.presp);
		else if (ieee80211_vif_is_mesh(&sdata->vif))
			beacon = rcu_dereference(sdata->u.mesh.beacon);

		if (beacon)
			for (i = 0; i < params->n_csa_offsets; i++)
				data[params->csa_offsets[i]] =
					beacon->csa_current_counter;

		rcu_read_unlock();
	}

	IEEE80211_SKB_CB(skb)->flags = flags;

	skb->dev = sdata->dev;

	if (!params->dont_wait_for_ack) {
		/* make a copy to preserve the frame contents
		 * in case of encryption.
		 */
		ret = ieee80211_attach_ack_skb(local, skb, cookie, GFP_KERNEL);
		if (ret) {
			kfree_skb(skb);
			goto out_unlock;
		}
	} else {
		/* Assign a dummy non-zero cookie, it's not sent to
		 * userspace in this case but we rely on its value
		 * internally in the need_offchan case to distinguish
		 * mgmt-tx from remain-on-channel.
		 */
		*cookie = 0xffffffff;
	}

	if (!need_offchan) {
		ieee80211_tx_skb(sdata, skb);
		ret = 0;
		goto out_unlock;
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_TX_OFFCHAN |
					IEEE80211_TX_INTFL_OFFCHAN_TX_OK;
	if (ieee80211_hw_check(&local->hw, QUEUE_CONTROL))
		IEEE80211_SKB_CB(skb)->hw_queue =
			local->hw.offchannel_tx_hw_queue;

	/* This will handle all kinds of coalescing and immediate TX */
	ret = ieee80211_start_roc_work(local, sdata, params->chan,
				       params->wait, cookie, skb,
				       IEEE80211_ROC_TYPE_MGMT_TX);
	if (ret)
		ieee80211_free_txskb(&local->hw, skb);
 out_unlock:
	mutex_unlock(&local->mtx);
	return ret;
}

int ieee80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
				  struct wireless_dev *wdev, u64 cookie)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	return ieee80211_cancel_roc(local, cookie, true);
}

void ieee80211_roc_setup(struct ieee80211_local *local)
{
	INIT_WORK(&local->hw_roc_start, ieee80211_hw_roc_start);
	INIT_WORK(&local->hw_roc_done, ieee80211_hw_roc_done);
	INIT_LIST_HEAD(&local->roc_list);
}

void ieee80211_roc_purge(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_roc_work *roc, *tmp;
	LIST_HEAD(tmp_list);

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (sdata && roc->sdata != sdata)
			continue;

		if (roc->started && local->ops->remain_on_channel) {
			/* can race, so ignore return value */
			drv_cancel_remain_on_channel(local);
		}

		list_move_tail(&roc->list, &tmp_list);
		roc->abort = true;
	}
	mutex_unlock(&local->mtx);

	list_for_each_entry_safe(roc, tmp, &tmp_list, list) {
		if (local->ops->remain_on_channel) {
			list_del(&roc->list);
			ieee80211_roc_notify_destroy(roc, true);
		} else {
			ieee80211_queue_delayed_work(&local->hw, &roc->work, 0);

			/* work will clean up etc */
			flush_delayed_work(&roc->work);
			WARN_ON(!roc->to_be_freed);
			kfree(roc);
		}
	}

	WARN_ON_ONCE(!list_empty(&tmp_list));
}
