// SPDX-License-Identifier: GPL-2.0-only
/*
 * Off-channel operation helpers
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2019, 2022-2024 Intel Corporation
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
	bool offchannel_ps_enabled = false;

	/* FIXME: what to do when local->pspolling is true? */

	del_timer_sync(&local->dynamic_ps_timer);
	del_timer_sync(&ifmgd->bcn_mon_timer);
	del_timer_sync(&ifmgd->conn_mon_timer);

	wiphy_work_cancel(local->hw.wiphy, &local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		offchannel_ps_enabled = true;
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	if (!offchannel_ps_enabled ||
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

/* inform AP that we are awake again */
static void ieee80211_offchannel_ps_disable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	if (!local->ps_sdata)
		ieee80211_send_nullfunc(local, sdata, false);
	else if (local->hw.conf.dynamic_ps_timeout > 0) {
		/*
		 * the dynamic_ps_timer had been running before leaving the
		 * operating channel, restart the timer now and send a nullfunc
		 * frame to inform the AP that we are awake so that AP sends
		 * the buffered packets (if any).
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

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(!local->emulate_chanctx))
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

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE ||
		    sdata->vif.type == NL80211_IFTYPE_NAN)
			continue;

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

		/* Check to see if we should disable beaconing. */
		if (sdata->vif.bss_conf.enable_beacon) {
			set_bit(SDATA_STATE_OFFCHANNEL_BEACON_STOPPED,
				&sdata->state);
			sdata->vif.bss_conf.enable_beacon = false;
			ieee80211_link_info_change_notify(
				sdata, &sdata->deflink,
				BSS_CHANGED_BEACON_ENABLED);
		}

		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    sdata->u.mgd.associated)
			ieee80211_offchannel_ps_enable(sdata);
	}
}

void ieee80211_offchannel_return(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(!local->emulate_chanctx))
		return;

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
			ieee80211_link_info_change_notify(
				sdata, &sdata->deflink,
				BSS_CHANGED_BEACON_ENABLED);
		}
	}

	ieee80211_wake_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL,
					false);
}

static void ieee80211_roc_notify_destroy(struct ieee80211_roc_work *roc)
{
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
	else
		cfg80211_tx_mgmt_expired(&roc->sdata->wdev,
					 roc->mgmt_tx_cookie,
					 roc->chan, GFP_KERNEL);

	list_del(&roc->list);
	kfree(roc);
}

static unsigned long ieee80211_end_finished_rocs(struct ieee80211_local *local,
						 unsigned long now)
{
	struct ieee80211_roc_work *roc, *tmp;
	long remaining_dur_min = LONG_MAX;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		long remaining;

		if (!roc->started)
			break;

		remaining = roc->start_time +
			    msecs_to_jiffies(roc->duration) -
			    now;

		/* In case of HW ROC, it is possible that the HW finished the
		 * ROC session before the actual requested time. In such a case
		 * end the ROC session (disregarding the remaining time).
		 */
		if (roc->abort || roc->hw_begun || remaining <= 0)
			ieee80211_roc_notify_destroy(roc);
		else
			remaining_dur_min = min(remaining_dur_min, remaining);
	}

	return remaining_dur_min;
}

static bool ieee80211_recalc_sw_work(struct ieee80211_local *local,
				     unsigned long now)
{
	long dur = ieee80211_end_finished_rocs(local, now);

	if (dur == LONG_MAX)
		return false;

	wiphy_delayed_work_queue(local->hw.wiphy, &local->roc_work, dur);
	return true;
}

static void ieee80211_handle_roc_started(struct ieee80211_roc_work *roc,
					 unsigned long start_time)
{
	if (WARN_ON(roc->notified))
		return;

	roc->start_time = start_time;
	roc->started = true;

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

static void ieee80211_hw_roc_start(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_start);
	struct ieee80211_roc_work *roc;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(roc, &local->roc_list, list) {
		if (!roc->started)
			break;

		roc->hw_begun = true;
		ieee80211_handle_roc_started(roc, local->hw_roc_start_time);
	}
}

void ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw_roc_start_time = jiffies;

	trace_api_ready_on_channel(local);

	wiphy_work_queue(hw->wiphy, &local->hw_roc_start);
}
EXPORT_SYMBOL_GPL(ieee80211_ready_on_channel);

static void _ieee80211_start_next_roc(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc, *tmp;
	enum ieee80211_roc_type type;
	u32 min_dur, max_dur;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(list_empty(&local->roc_list)))
		return;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (WARN_ON(roc->started))
		return;

	min_dur = roc->duration;
	max_dur = roc->duration;
	type = roc->type;

	list_for_each_entry(tmp, &local->roc_list, list) {
		if (tmp == roc)
			continue;
		if (tmp->sdata != roc->sdata || tmp->chan != roc->chan)
			break;
		max_dur = max(tmp->duration, max_dur);
		min_dur = min(tmp->duration, min_dur);
		type = max(tmp->type, type);
	}

	if (local->ops->remain_on_channel) {
		int ret = drv_remain_on_channel(local, roc->sdata, roc->chan,
						max_dur, type);

		if (ret) {
			wiphy_warn(local->hw.wiphy,
				   "failed to start next HW ROC (%d)\n", ret);
			/*
			 * queue the work struct again to avoid recursion
			 * when multiple failures occur
			 */
			list_for_each_entry(tmp, &local->roc_list, list) {
				if (tmp->sdata != roc->sdata ||
				    tmp->chan != roc->chan)
					break;
				tmp->started = true;
				tmp->abort = true;
			}
			wiphy_work_queue(local->hw.wiphy, &local->hw_roc_done);
			return;
		}

		/* we'll notify about the start once the HW calls back */
		list_for_each_entry(tmp, &local->roc_list, list) {
			if (tmp->sdata != roc->sdata || tmp->chan != roc->chan)
				break;
			tmp->started = true;
		}
	} else {
		/* If actually operating on the desired channel (with at least
		 * 20 MHz channel width) don't stop all the operations but still
		 * treat it as though the ROC operation started properly, so
		 * other ROC operations won't interfere with this one.
		 *
		 * Note: scan can't run, tmp_channel is what we use, so this
		 * must be the currently active channel.
		 */
		roc->on_channel = roc->chan == local->hw.conf.chandef.chan &&
				  local->hw.conf.chandef.width != NL80211_CHAN_WIDTH_5 &&
				  local->hw.conf.chandef.width != NL80211_CHAN_WIDTH_10;

		/* start this ROC */
		ieee80211_recalc_idle(local);

		if (!roc->on_channel) {
			ieee80211_offchannel_stop_vifs(local);

			local->tmp_channel = roc->chan;
			ieee80211_hw_conf_chan(local);
		}

		wiphy_delayed_work_queue(local->hw.wiphy, &local->roc_work,
					 msecs_to_jiffies(min_dur));

		/* tell userspace or send frame(s) */
		list_for_each_entry(tmp, &local->roc_list, list) {
			if (tmp->sdata != roc->sdata || tmp->chan != roc->chan)
				break;

			tmp->on_channel = roc->on_channel;
			ieee80211_handle_roc_started(tmp, jiffies);
		}
	}
}

void ieee80211_start_next_roc(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (list_empty(&local->roc_list)) {
		ieee80211_run_deferred_scan(local);
		return;
	}

	/* defer roc if driver is not started (i.e. during reconfig) */
	if (local->in_reconfig)
		return;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (WARN_ON_ONCE(roc->started))
		return;

	if (local->ops->remain_on_channel) {
		_ieee80211_start_next_roc(local);
	} else {
		/* delay it a bit */
		wiphy_delayed_work_queue(local->hw.wiphy, &local->roc_work,
					 round_jiffies_relative(HZ / 2));
	}
}

void ieee80211_reconfig_roc(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc, *tmp;

	/*
	 * In the software implementation can just continue with the
	 * interruption due to reconfig, roc_work is still queued if
	 * needed.
	 */
	if (!local->ops->remain_on_channel)
		return;

	/* flush work so nothing from the driver is still pending */
	wiphy_work_flush(local->hw.wiphy, &local->hw_roc_start);
	wiphy_work_flush(local->hw.wiphy, &local->hw_roc_done);

	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (!roc->started)
			break;

		if (!roc->hw_begun) {
			/* it didn't start in HW yet, so we can restart it */
			roc->started = false;
			continue;
		}

		/* otherwise destroy it and tell userspace */
		ieee80211_roc_notify_destroy(roc);
	}

	ieee80211_start_next_roc(local);
}

static void __ieee80211_roc_work(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc;
	bool on_channel;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(local->ops->remain_on_channel))
		return;

	roc = list_first_entry_or_null(&local->roc_list,
				       struct ieee80211_roc_work, list);
	if (!roc)
		return;

	if (!roc->started) {
		WARN_ON(!local->emulate_chanctx);
		_ieee80211_start_next_roc(local);
	} else {
		on_channel = roc->on_channel;
		if (ieee80211_recalc_sw_work(local, jiffies))
			return;

		/* careful - roc pointer became invalid during recalc */

		if (!on_channel) {
			ieee80211_flush_queues(local, NULL, false);

			local->tmp_channel = NULL;
			ieee80211_hw_conf_chan(local);

			ieee80211_offchannel_return(local);
		}

		ieee80211_recalc_idle(local);
		ieee80211_start_next_roc(local);
	}
}

static void ieee80211_roc_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, roc_work.work);

	lockdep_assert_wiphy(local->hw.wiphy);

	__ieee80211_roc_work(local);
}

static void ieee80211_hw_roc_done(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_done);

	lockdep_assert_wiphy(local->hw.wiphy);

	ieee80211_end_finished_rocs(local, jiffies);

	/* if there's another roc, start it now */
	ieee80211_start_next_roc(local);
}

void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_remain_on_channel_expired(local);

	wiphy_work_queue(hw->wiphy, &local->hw_roc_done);
}
EXPORT_SYMBOL_GPL(ieee80211_remain_on_channel_expired);

static bool
ieee80211_coalesce_hw_started_roc(struct ieee80211_local *local,
				  struct ieee80211_roc_work *new_roc,
				  struct ieee80211_roc_work *cur_roc)
{
	unsigned long now = jiffies;
	unsigned long remaining;

	if (WARN_ON(!cur_roc->started))
		return false;

	/* if it was scheduled in the hardware, but not started yet,
	 * we can only combine if the older one had a longer duration
	 */
	if (!cur_roc->hw_begun && new_roc->duration > cur_roc->duration)
		return false;

	remaining = cur_roc->start_time +
		    msecs_to_jiffies(cur_roc->duration) -
		    now;

	/* if it doesn't fit entirely, schedule a new one */
	if (new_roc->duration > jiffies_to_msecs(remaining))
		return false;

	/* add just after the current one so we combine their finish later */
	list_add(&new_roc->list, &cur_roc->list);

	/* if the existing one has already begun then let this one also
	 * begin, otherwise they'll both be marked properly by the work
	 * struct that runs once the driver notifies us of the beginning
	 */
	if (cur_roc->hw_begun) {
		new_roc->hw_begun = true;
		ieee80211_handle_roc_started(new_roc, now);
	}

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
	bool queued = false, combine_started = true;
	int ret;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (channel->freq_offset)
		/* this may work, but is untested */
		return -EOPNOTSUPP;

	if (!local->emulate_chanctx && !local->ops->remain_on_channel)
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

	/* if there's no need to queue, handle it immediately */
	if (list_empty(&local->roc_list) &&
	    !local->scanning && !ieee80211_is_radar_required(local)) {
		/* if not HW assist, just queue & schedule work */
		if (!local->ops->remain_on_channel) {
			list_add_tail(&roc->list, &local->roc_list);
			wiphy_delayed_work_queue(local->hw.wiphy,
						 &local->roc_work, 0);
		} else {
			/* otherwise actually kick it off here
			 * (for error handling)
			 */
			ret = drv_remain_on_channel(local, sdata, channel,
						    duration, type);
			if (ret) {
				kfree(roc);
				return ret;
			}
			roc->started = true;
			list_add_tail(&roc->list, &local->roc_list);
		}

		return 0;
	}

	/* otherwise handle queueing */

	list_for_each_entry(tmp, &local->roc_list, list) {
		if (tmp->chan != channel || tmp->sdata != sdata)
			continue;

		/*
		 * Extend this ROC if possible: If it hasn't started, add
		 * just after the new one to combine.
		 */
		if (!tmp->started) {
			list_add(&roc->list, &tmp->list);
			queued = true;
			break;
		}

		if (!combine_started)
			continue;

		if (!local->ops->remain_on_channel) {
			/* If there's no hardware remain-on-channel, and
			 * doing so won't push us over the maximum r-o-c
			 * we allow, then we can just add the new one to
			 * the list and mark it as having started now.
			 * If it would push over the limit, don't try to
			 * combine with other started ones (that haven't
			 * been running as long) but potentially sort it
			 * with others that had the same fate.
			 */
			unsigned long now = jiffies;
			u32 elapsed = jiffies_to_msecs(now - tmp->start_time);
			struct wiphy *wiphy = local->hw.wiphy;
			u32 max_roc = wiphy->max_remain_on_channel_duration;

			if (elapsed + roc->duration > max_roc) {
				combine_started = false;
				continue;
			}

			list_add(&roc->list, &tmp->list);
			queued = true;
			roc->on_channel = tmp->on_channel;
			ieee80211_handle_roc_started(roc, now);
			ieee80211_recalc_sw_work(local, now);
			break;
		}

		queued = ieee80211_coalesce_hw_started_roc(local, roc, tmp);
		if (queued)
			break;
		/* if it wasn't queued, perhaps it can be combined with
		 * another that also couldn't get combined previously,
		 * but no need to check for already started ones, since
		 * that can't work.
		 */
		combine_started = false;
	}

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

	lockdep_assert_wiphy(local->hw.wiphy);

	return ieee80211_start_roc_work(local, sdata, chan,
					duration, cookie, NULL,
					IEEE80211_ROC_TYPE_NORMAL);
}

static int ieee80211_cancel_roc(struct ieee80211_local *local,
				u64 cookie, bool mgmt_tx)
{
	struct ieee80211_roc_work *roc, *tmp, *found = NULL;
	int ret;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!cookie)
		return -ENOENT;

	wiphy_work_flush(local->hw.wiphy, &local->hw_roc_start);

	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (!mgmt_tx && roc->cookie != cookie)
			continue;
		else if (mgmt_tx && roc->mgmt_tx_cookie != cookie)
			continue;

		found = roc;
		break;
	}

	if (!found) {
		return -ENOENT;
	}

	if (!found->started) {
		ieee80211_roc_notify_destroy(found);
		goto out_unlock;
	}

	if (local->ops->remain_on_channel) {
		ret = drv_cancel_remain_on_channel(local, roc->sdata);
		if (WARN_ON_ONCE(ret)) {
			return ret;
		}

		/*
		 * We could be racing against the notification from the driver:
		 *  + driver is handling the notification on CPU0
		 *  + user space is cancelling the remain on channel and
		 *    schedules the hw_roc_done worker.
		 *
		 *  Now hw_roc_done might start to run after the next roc will
		 *  start and mac80211 will think that this second roc has
		 *  ended prematurely.
		 *  Cancel the work to make sure that all the pending workers
		 *  have completed execution.
		 *  Note that this assumes that by the time the driver returns
		 *  from drv_cancel_remain_on_channel, it has completed all
		 *  the processing of related notifications.
		 */
		wiphy_work_cancel(local->hw.wiphy, &local->hw_roc_done);

		/* TODO:
		 * if multiple items were combined here then we really shouldn't
		 * cancel them all - we should wait for as much time as needed
		 * for the longest remaining one, and only then cancel ...
		 */
		list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
			if (!roc->started)
				break;
			if (roc == found)
				found = NULL;
			ieee80211_roc_notify_destroy(roc);
		}

		/* that really must not happen - it was started */
		WARN_ON(found);

		ieee80211_start_next_roc(local);
	} else {
		/* go through work struct to return to the operating channel */
		found->abort = true;
		wiphy_delayed_work_queue(local->hw.wiphy, &local->roc_work, 0);
	}

 out_unlock:

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
	struct sta_info *sta = NULL;
	const struct ieee80211_mgmt *mgmt = (void *)params->buf;
	bool need_offchan = false;
	bool mlo_sta = false;
	int link_id = -1;
	u32 flags;
	int ret;
	u8 *data;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (params->dont_wait_for_ack)
		flags = IEEE80211_TX_CTL_NO_ACK;
	else
		flags = IEEE80211_TX_INTFL_NL80211_FRAME_TX |
			IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (params->no_cck)
		flags |= IEEE80211_TX_CTL_NO_CCK_RATE;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
		if (!sdata->vif.cfg.ibss_joined)
			need_offchan = true;
#ifdef CONFIG_MAC80211_MESH
		fallthrough;
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif) &&
		    !sdata->u.mesh.mesh_id_len)
			need_offchan = true;
#endif
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		if (sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		    !ieee80211_vif_is_mesh(&sdata->vif) &&
		    !sdata->bss->active)
			need_offchan = true;

		rcu_read_lock();
		sta = sta_info_get_bss(sdata, mgmt->da);
		mlo_sta = sta && sta->sta.mlo;

		if (!ieee80211_is_action(mgmt->frame_control) ||
		    mgmt->u.action.category == WLAN_CATEGORY_PUBLIC ||
		    mgmt->u.action.category == WLAN_CATEGORY_SELF_PROTECTED ||
		    mgmt->u.action.category == WLAN_CATEGORY_SPECTRUM_MGMT) {
			rcu_read_unlock();
			break;
		}

		if (!sta) {
			rcu_read_unlock();
			return -ENOLINK;
		}
		if (params->link_id >= 0 &&
		    !(sta->sta.valid_links & BIT(params->link_id))) {
			rcu_read_unlock();
			return -ENOLINK;
		}
		link_id = params->link_id;
		rcu_read_unlock();
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (!sdata->u.mgd.associated ||
		    (params->offchan && params->wait &&
		     local->ops->remain_on_channel &&
		     memcmp(sdata->vif.cfg.ap_addr, mgmt->bssid, ETH_ALEN))) {
			need_offchan = true;
		} else if (sdata->u.mgd.associated &&
			   ether_addr_equal(sdata->vif.cfg.ap_addr, mgmt->da)) {
			sta = sta_info_get_bss(sdata, mgmt->da);
			mlo_sta = sta && sta->sta.mlo;
		}
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		need_offchan = true;
		break;
	case NL80211_IFTYPE_NAN:
	default:
		return -EOPNOTSUPP;
	}

	/* configurations requiring offchan cannot work if no channel has been
	 * specified
	 */
	if (need_offchan && !params->chan)
		return -EINVAL;

	/* Check if the operating channel is the requested channel */
	if (!params->chan && mlo_sta) {
		need_offchan = false;
	} else if (!need_offchan) {
		struct ieee80211_chanctx_conf *chanctx_conf = NULL;
		int i;

		rcu_read_lock();
		/* Check all the links first */
		for (i = 0; i < ARRAY_SIZE(sdata->vif.link_conf); i++) {
			struct ieee80211_bss_conf *conf;

			conf = rcu_dereference(sdata->vif.link_conf[i]);
			if (!conf)
				continue;

			chanctx_conf = rcu_dereference(conf->chanctx_conf);
			if (!chanctx_conf)
				continue;

			if (mlo_sta && params->chan == chanctx_conf->def.chan &&
			    ether_addr_equal(sdata->vif.addr, mgmt->sa)) {
				link_id = i;
				break;
			}

			if (ether_addr_equal(conf->addr, mgmt->sa)) {
				/* If userspace requested Tx on a specific link
				 * use the same link id if the link bss is matching
				 * the requested chan.
				 */
				if (sdata->vif.valid_links &&
				    params->link_id >= 0 && params->link_id == i &&
				    params->chan == chanctx_conf->def.chan)
					link_id = i;

				break;
			}

			chanctx_conf = NULL;
		}

		if (chanctx_conf) {
			need_offchan = params->chan &&
				       (params->chan !=
					chanctx_conf->def.chan);
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

	data = skb_put_data(skb, params->buf, params->len);

	/* Update CSA counters */
	if (sdata->vif.bss_conf.csa_active &&
	    (sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_MESH_POINT ||
	     sdata->vif.type == NL80211_IFTYPE_ADHOC) &&
	    params->n_csa_offsets) {
		int i;
		struct beacon_data *beacon = NULL;

		rcu_read_lock();

		if (sdata->vif.type == NL80211_IFTYPE_AP)
			beacon = rcu_dereference(sdata->deflink.u.ap.beacon);
		else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
			beacon = rcu_dereference(sdata->u.ibss.presp);
		else if (ieee80211_vif_is_mesh(&sdata->vif))
			beacon = rcu_dereference(sdata->u.mesh.beacon);

		if (beacon)
			for (i = 0; i < params->n_csa_offsets; i++)
				data[params->csa_offsets[i]] =
					beacon->cntdwn_current_counter;

		rcu_read_unlock();
	}

	IEEE80211_SKB_CB(skb)->flags = flags;
	IEEE80211_SKB_CB(skb)->control.flags |= IEEE80211_TX_CTRL_DONT_USE_RATE_MASK;

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
		ieee80211_tx_skb_tid(sdata, skb, 7, link_id);
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
	wiphy_work_init(&local->hw_roc_start, ieee80211_hw_roc_start);
	wiphy_work_init(&local->hw_roc_done, ieee80211_hw_roc_done);
	wiphy_delayed_work_init(&local->roc_work, ieee80211_roc_work);
	INIT_LIST_HEAD(&local->roc_list);
}

void ieee80211_roc_purge(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_roc_work *roc, *tmp;
	bool work_to_do = false;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (sdata && roc->sdata != sdata)
			continue;

		if (roc->started) {
			if (local->ops->remain_on_channel) {
				/* can race, so ignore return value */
				drv_cancel_remain_on_channel(local, roc->sdata);
				ieee80211_roc_notify_destroy(roc);
			} else {
				roc->abort = true;
				work_to_do = true;
			}
		} else {
			ieee80211_roc_notify_destroy(roc);
		}
	}
	if (work_to_do)
		__ieee80211_roc_work(local);
}
