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
#include "driver-trace.h"

/*
 * Tell our hardware to disable PS.
 * Optionally inform AP that we will go to sleep so that it will buffer
 * the frames while we are doing off-channel work.  This is optional
 * because we *may* be doing work on-operating channel, and want our
 * hardware unconditionally awake, but still let the AP send us normal frames.
 */
static void ieee80211_offchannel_ps_enable(struct ieee80211_sub_if_data *sdata,
					   bool tell_ap)
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

	if (tell_ap && (!local->offchannel_ps_enabled ||
			!(local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)))
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

	/*
	 * notify the AP about us leaving the channel and stop all
	 * STA interfaces.
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
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
				ieee80211_offchannel_ps_enable(sdata, true);
		}
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_return(struct ieee80211_local *local,
				 bool offchannel_ps_disable)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
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

static void ieee80211_hw_roc_start(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_start);
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->mtx);

	if (!local->hw_roc_channel) {
		mutex_unlock(&local->mtx);
		return;
	}

	if (local->hw_roc_skb) {
		sdata = IEEE80211_DEV_TO_SUB_IF(local->hw_roc_dev);
		ieee80211_tx_skb(sdata, local->hw_roc_skb);
		local->hw_roc_skb = NULL;
	} else {
		cfg80211_ready_on_channel(local->hw_roc_dev,
					  local->hw_roc_cookie,
					  local->hw_roc_channel,
					  local->hw_roc_channel_type,
					  local->hw_roc_duration,
					  GFP_KERNEL);
	}

	ieee80211_recalc_idle(local);

	mutex_unlock(&local->mtx);
}

void ieee80211_ready_on_channel(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_ready_on_channel(local);

	ieee80211_queue_work(hw, &local->hw_roc_start);
}
EXPORT_SYMBOL_GPL(ieee80211_ready_on_channel);

static void ieee80211_hw_roc_done(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_done);

	mutex_lock(&local->mtx);

	if (!local->hw_roc_channel) {
		mutex_unlock(&local->mtx);
		return;
	}

	if (!local->hw_roc_for_tx)
		cfg80211_remain_on_channel_expired(local->hw_roc_dev,
						   local->hw_roc_cookie,
						   local->hw_roc_channel,
						   local->hw_roc_channel_type,
						   GFP_KERNEL);

	local->hw_roc_channel = NULL;
	local->hw_roc_cookie = 0;

	ieee80211_recalc_idle(local);

	mutex_unlock(&local->mtx);
}

void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_remain_on_channel_expired(local);

	ieee80211_queue_work(hw, &local->hw_roc_done);
}
EXPORT_SYMBOL_GPL(ieee80211_remain_on_channel_expired);

void ieee80211_hw_roc_setup(struct ieee80211_local *local)
{
	INIT_WORK(&local->hw_roc_start, ieee80211_hw_roc_start);
	INIT_WORK(&local->hw_roc_done, ieee80211_hw_roc_done);
}
