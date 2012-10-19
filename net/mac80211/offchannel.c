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
	    !(local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK))
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
		ieee80211_send_nullfunc(local, sdata, 1);
}

/* inform AP that we are awake again, unless power save is enabled */
static void ieee80211_offchannel_ps_disable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	if (!local->ps_sdata)
		ieee80211_send_nullfunc(local, sdata, 0);
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
		ieee80211_send_nullfunc(local, sdata, 0);
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));
	}

	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);
}

void ieee80211_offchannel_stop_vifs(struct ieee80211_local *local,
				    bool offchannel_ps_enable)
{
	struct ieee80211_sub_if_data *sdata;

	if (WARN_ON(local->use_chanctx))
		return;

	/*
	 * notify the AP about us leaving the channel and stop all
	 * STA interfaces.
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

		/* Check to see if we should disable beaconing. */
		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR) {
			netif_tx_stop_all_queues(sdata->dev);
			if (offchannel_ps_enable &&
			    (sdata->vif.type == NL80211_IFTYPE_STATION) &&
			    sdata->u.mgd.associated)
				ieee80211_offchannel_ps_enable(sdata);
		}
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_return(struct ieee80211_local *local,
				 bool offchannel_ps_disable)
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
		if (offchannel_ps_disable &&
		    sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated)
				ieee80211_offchannel_ps_disable(sdata);
		}

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR) {
			/*
			 * This may wake up queues even though the driver
			 * currently has them stopped. This is not very
			 * likely, since the driver won't have gotten any
			 * (or hardly any) new packets while we weren't
			 * on the right channel, and even if it happens
			 * it will at most lead to queueing up one more
			 * packet per queue in mac80211 rather than on
			 * the interface qdisc.
			 */
			netif_tx_wake_all_queues(sdata->dev);
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_handle_roc_started(struct ieee80211_roc_work *roc)
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
		cfg80211_ready_on_channel(&roc->sdata->wdev, (unsigned long)roc,
					  roc->chan, roc->chan_type,
					  roc->req_duration, GFP_KERNEL);
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

		ret = drv_remain_on_channel(local, roc->chan,
					    roc->chan_type,
					    duration);

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

void ieee80211_roc_notify_destroy(struct ieee80211_roc_work *roc)
{
	struct ieee80211_roc_work *dep, *tmp;

	/* was never transmitted */
	if (roc->frame) {
		cfg80211_mgmt_tx_status(&roc->sdata->wdev,
					(unsigned long)roc->frame,
					roc->frame->data, roc->frame->len,
					false, GFP_KERNEL);
		kfree_skb(roc->frame);
	}

	if (!roc->mgmt_tx_cookie)
		cfg80211_remain_on_channel_expired(&roc->sdata->wdev,
						   (unsigned long)roc,
						   roc->chan, roc->chan_type,
						   GFP_KERNEL);

	list_for_each_entry_safe(dep, tmp, &roc->dependents, list)
		ieee80211_roc_notify_destroy(dep);

	kfree(roc);
}

void ieee80211_sw_roc_work(struct work_struct *work)
{
	struct ieee80211_roc_work *roc =
		container_of(work, struct ieee80211_roc_work, work.work);
	struct ieee80211_sub_if_data *sdata = roc->sdata;
	struct ieee80211_local *local = sdata->local;
	bool started;

	mutex_lock(&local->mtx);

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

		/* start this ROC */

		/* switch channel etc */
		ieee80211_recalc_idle(local);

		local->tmp_channel = roc->chan;
		local->tmp_channel_type = roc->chan_type;
		ieee80211_hw_config(local, 0);

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
		ieee80211_roc_notify_destroy(roc);

		if (started) {
			drv_flush(local, false);

			local->tmp_channel = NULL;
			ieee80211_hw_config(local, 0);

			ieee80211_offchannel_return(local, true);
		}

		ieee80211_recalc_idle(local);

		if (started)
			ieee80211_start_next_roc(local);
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

	ieee80211_roc_notify_destroy(roc);

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

void ieee80211_roc_setup(struct ieee80211_local *local)
{
	INIT_WORK(&local->hw_roc_start, ieee80211_hw_roc_start);
	INIT_WORK(&local->hw_roc_done, ieee80211_hw_roc_done);
	INIT_LIST_HEAD(&local->roc_list);
}

void ieee80211_roc_purge(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_roc_work *roc, *tmp;
	LIST_HEAD(tmp_list);

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (roc->sdata != sdata)
			continue;

		if (roc->started && local->ops->remain_on_channel) {
			/* can race, so ignore return value */
			drv_cancel_remain_on_channel(local);
		}

		list_move_tail(&roc->list, &tmp_list);
		roc->abort = true;
	}

	ieee80211_start_next_roc(local);
	mutex_unlock(&local->mtx);

	list_for_each_entry_safe(roc, tmp, &tmp_list, list) {
		if (local->ops->remain_on_channel) {
			list_del(&roc->list);
			ieee80211_roc_notify_destroy(roc);
		} else {
			ieee80211_queue_delayed_work(&local->hw, &roc->work, 0);

			/* work will clean up etc */
			flush_delayed_work(&roc->work);
		}
	}

	WARN_ON_ONCE(!list_empty(&tmp_list));
}
