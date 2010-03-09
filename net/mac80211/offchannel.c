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
#include <net/mac80211.h>
#include "ieee80211_i.h"

/*
 * inform AP that we will go to sleep so that it will buffer the frames
 * while we scan
 */
static void ieee80211_offchannel_ps_enable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	local->offchannel_ps_enabled = false;

	/* FIXME: what to do when local->pspolling is true? */

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->offchannel_ps_enabled = true;
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	if (!(local->offchannel_ps_enabled) ||
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
}

void ieee80211_offchannel_stop_beaconing(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		/* disable beaconing */
		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);

		/*
		 * only handle non-STA interfaces here, STA interfaces
		 * are handled in ieee80211_offchannel_stop_station(),
		 * e.g., from the background scan state machine.
		 *
		 * In addition, do not stop monitor interface to allow it to be
		 * used from user space controlled off-channel operations.
		 */
		if (sdata->vif.type != NL80211_IFTYPE_STATION &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR)
			netif_tx_stop_all_queues(sdata->dev);
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_stop_station(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	/*
	 * notify the AP about us leaving the channel and stop all STA interfaces
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			netif_tx_stop_all_queues(sdata->dev);
			if (sdata->u.mgd.associated)
				ieee80211_offchannel_ps_enable(sdata);
		}
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_return(struct ieee80211_local *local,
				 bool enable_beaconing)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		/* Tell AP we're back */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated)
				ieee80211_offchannel_ps_disable(sdata);
		}

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			netif_tx_wake_all_queues(sdata->dev);

		/* re-enable beaconing */
		if (enable_beaconing &&
		    (sdata->vif.type == NL80211_IFTYPE_AP ||
		     sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		     sdata->vif.type == NL80211_IFTYPE_MESH_POINT))
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
	}
	mutex_unlock(&local->iflist_mtx);
}
