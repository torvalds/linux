// SPDX-License-Identifier: GPL-2.0-only
/*
 * OCB mode implementation
 *
 * Copyright: (c) 2014 Czech Technical University in Prague
 *            (c) 2014 Volkswagen Group Research
 * Copyright (C) 2022 - 2024 Intel Corporation
 * Author:    Rostislav Lisovy <rostislav.lisovy@fel.cvut.cz>
 * Funded by: Volkswagen Group Research
 */

#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>
#include <linux/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"

#define IEEE80211_OCB_HOUSEKEEPING_INTERVAL		(60 * HZ)
#define IEEE80211_OCB_PEER_INACTIVITY_LIMIT		(240 * HZ)
#define IEEE80211_OCB_MAX_STA_ENTRIES			128

/**
 * enum ocb_deferred_task_flags - mac80211 OCB deferred tasks
 * @OCB_WORK_HOUSEKEEPING: run the periodic OCB housekeeping tasks
 *
 * These flags are used in @wrkq_flags field of &struct ieee80211_if_ocb
 */
enum ocb_deferred_task_flags {
	OCB_WORK_HOUSEKEEPING,
};

void ieee80211_ocb_rx_no_sta(struct ieee80211_sub_if_data *sdata,
			     const u8 *bssid, const u8 *addr,
			     u32 supp_rates)
{
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	int band;

	/* XXX: Consider removing the least recently used entry and
	 *      allow new one to be added.
	 */
	if (local->num_sta >= IEEE80211_OCB_MAX_STA_ENTRIES) {
		net_info_ratelimited("%s: No room for a new OCB STA entry %pM\n",
				     sdata->name, addr);
		return;
	}

	ocb_dbg(sdata, "Adding new OCB station %pM\n", addr);

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.bss_conf.chanctx_conf);
	if (WARN_ON_ONCE(!chanctx_conf)) {
		rcu_read_unlock();
		return;
	}
	band = chanctx_conf->def.chan->band;
	rcu_read_unlock();

	sta = sta_info_alloc(sdata, addr, GFP_ATOMIC);
	if (!sta)
		return;

	/* Add only mandatory rates for now */
	sband = local->hw.wiphy->bands[band];
	sta->sta.deflink.supp_rates[band] = ieee80211_mandatory_rates(sband);

	spin_lock(&ifocb->incomplete_lock);
	list_add(&sta->list, &ifocb->incomplete_stations);
	spin_unlock(&ifocb->incomplete_lock);
	wiphy_work_queue(local->hw.wiphy, &sdata->work);
}

static struct sta_info *ieee80211_ocb_finish_sta(struct sta_info *sta)
	__acquires(RCU)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u8 addr[ETH_ALEN];

	memcpy(addr, sta->sta.addr, ETH_ALEN);

	ocb_dbg(sdata, "Adding new IBSS station %pM (dev=%s)\n",
		addr, sdata->name);

	sta_info_move_state(sta, IEEE80211_STA_AUTH);
	sta_info_move_state(sta, IEEE80211_STA_ASSOC);
	sta_info_move_state(sta, IEEE80211_STA_AUTHORIZED);

	rate_control_rate_init(&sta->deflink);

	/* If it fails, maybe we raced another insertion? */
	if (sta_info_insert_rcu(sta))
		return sta_info_get(sdata, addr);
	return sta;
}

static void ieee80211_ocb_housekeeping(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;

	ocb_dbg(sdata, "Running ocb housekeeping\n");

	ieee80211_sta_expire(sdata, IEEE80211_OCB_PEER_INACTIVITY_LIMIT);

	mod_timer(&ifocb->housekeeping_timer,
		  round_jiffies(jiffies + IEEE80211_OCB_HOUSEKEEPING_INTERVAL));
}

void ieee80211_ocb_work(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;
	struct sta_info *sta;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (ifocb->joined != true)
		return;

	spin_lock_bh(&ifocb->incomplete_lock);
	while (!list_empty(&ifocb->incomplete_stations)) {
		sta = list_first_entry(&ifocb->incomplete_stations,
				       struct sta_info, list);
		list_del(&sta->list);
		spin_unlock_bh(&ifocb->incomplete_lock);

		ieee80211_ocb_finish_sta(sta);
		rcu_read_unlock();
		spin_lock_bh(&ifocb->incomplete_lock);
	}
	spin_unlock_bh(&ifocb->incomplete_lock);

	if (test_and_clear_bit(OCB_WORK_HOUSEKEEPING, &ifocb->wrkq_flags))
		ieee80211_ocb_housekeeping(sdata);
}

static void ieee80211_ocb_housekeeping_timer(struct timer_list *t)
{
	struct ieee80211_sub_if_data *sdata =
		timer_container_of(sdata, t, u.ocb.housekeeping_timer);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;

	set_bit(OCB_WORK_HOUSEKEEPING, &ifocb->wrkq_flags);

	wiphy_work_queue(local->hw.wiphy, &sdata->work);
}

void ieee80211_ocb_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;

	timer_setup(&ifocb->housekeeping_timer,
		    ieee80211_ocb_housekeeping_timer, 0);
	INIT_LIST_HEAD(&ifocb->incomplete_stations);
	spin_lock_init(&ifocb->incomplete_lock);
}

int ieee80211_ocb_join(struct ieee80211_sub_if_data *sdata,
		       struct ocb_setup *setup)
{
	struct ieee80211_chan_req chanreq = { .oper = setup->chandef };
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;
	u64 changed = BSS_CHANGED_OCB | BSS_CHANGED_BSSID;
	int err;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (ifocb->joined == true)
		return -EINVAL;

	sdata->deflink.operating_11g_mode = true;
	sdata->deflink.smps_mode = IEEE80211_SMPS_OFF;
	sdata->deflink.needed_rx_chains = sdata->local->rx_chains;

	err = ieee80211_link_use_channel(&sdata->deflink, &chanreq,
					 IEEE80211_CHANCTX_SHARED);
	if (err)
		return err;

	ieee80211_bss_info_change_notify(sdata, changed);

	ifocb->joined = true;

	set_bit(OCB_WORK_HOUSEKEEPING, &ifocb->wrkq_flags);
	wiphy_work_queue(local->hw.wiphy, &sdata->work);

	netif_carrier_on(sdata->dev);
	return 0;
}

int ieee80211_ocb_leave(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ocb *ifocb = &sdata->u.ocb;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	ifocb->joined = false;
	sta_info_flush(sdata, -1);

	spin_lock_bh(&ifocb->incomplete_lock);
	while (!list_empty(&ifocb->incomplete_stations)) {
		sta = list_first_entry(&ifocb->incomplete_stations,
				       struct sta_info, list);
		list_del(&sta->list);
		spin_unlock_bh(&ifocb->incomplete_lock);

		sta_info_free(local, sta);
		spin_lock_bh(&ifocb->incomplete_lock);
	}
	spin_unlock_bh(&ifocb->incomplete_lock);

	netif_carrier_off(sdata->dev);
	clear_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_OCB);

	ieee80211_link_release_channel(&sdata->deflink);

	skb_queue_purge(&sdata->skb_queue);

	timer_delete_sync(&sdata->u.ocb.housekeeping_timer);
	/* If the timer fired while we waited for it, it will have
	 * requeued the work. Now the work will be running again
	 * but will not rearm the timer again because it checks
	 * whether we are connected to the network or not -- at this
	 * point we shouldn't be anymore.
	 */

	return 0;
}
