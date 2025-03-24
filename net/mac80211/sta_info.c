// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <linux/rtnetlink.h>

#include <net/codel.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "sta_info.h"
#include "debugfs_sta.h"
#include "mesh.h"
#include "wme.h"

/**
 * DOC: STA information lifetime rules
 *
 * STA info structures (&struct sta_info) are managed in a hash table
 * for faster lookup and a list for iteration. They are managed using
 * RCU, i.e. access to the list and hash table is protected by RCU.
 *
 * Upon allocating a STA info structure with sta_info_alloc(), the caller
 * owns that structure. It must then insert it into the hash table using
 * either sta_info_insert() or sta_info_insert_rcu(); only in the latter
 * case (which acquires an rcu read section but must not be called from
 * within one) will the pointer still be valid after the call. Note that
 * the caller may not do much with the STA info before inserting it; in
 * particular, it may not start any mesh peer link management or add
 * encryption keys.
 *
 * When the insertion fails (sta_info_insert()) returns non-zero), the
 * structure will have been freed by sta_info_insert()!
 *
 * Station entries are added by mac80211 when you establish a link with a
 * peer. This means different things for the different type of interfaces
 * we support. For a regular station this mean we add the AP sta when we
 * receive an association response from the AP. For IBSS this occurs when
 * get to know about a peer on the same IBSS. For WDS we add the sta for
 * the peer immediately upon device open. When using AP mode we add stations
 * for each respective station upon request from userspace through nl80211.
 *
 * In order to remove a STA info structure, various sta_info_destroy_*()
 * calls are available.
 *
 * There is no concept of ownership on a STA entry; each structure is
 * owned by the global hash table/list until it is removed. All users of
 * the structure need to be RCU protected so that the structure won't be
 * freed before they are done using it.
 */

struct sta_link_alloc {
	struct link_sta_info info;
	struct ieee80211_link_sta sta;
	struct rcu_head rcu_head;
};

static const struct rhashtable_params sta_rht_params = {
	.nelem_hint = 3, /* start small */
	.automatic_shrinking = true,
	.head_offset = offsetof(struct sta_info, hash_node),
	.key_offset = offsetof(struct sta_info, addr),
	.key_len = ETH_ALEN,
	.max_size = CONFIG_MAC80211_STA_HASH_MAX_SIZE,
};

static const struct rhashtable_params link_sta_rht_params = {
	.nelem_hint = 3, /* start small */
	.automatic_shrinking = true,
	.head_offset = offsetof(struct link_sta_info, link_hash_node),
	.key_offset = offsetof(struct link_sta_info, addr),
	.key_len = ETH_ALEN,
	.max_size = CONFIG_MAC80211_STA_HASH_MAX_SIZE,
};

static int sta_info_hash_del(struct ieee80211_local *local,
			     struct sta_info *sta)
{
	return rhltable_remove(&local->sta_hash, &sta->hash_node,
			       sta_rht_params);
}

static int link_sta_info_hash_add(struct ieee80211_local *local,
				  struct link_sta_info *link_sta)
{
	lockdep_assert_wiphy(local->hw.wiphy);

	return rhltable_insert(&local->link_sta_hash,
			       &link_sta->link_hash_node, link_sta_rht_params);
}

static int link_sta_info_hash_del(struct ieee80211_local *local,
				  struct link_sta_info *link_sta)
{
	lockdep_assert_wiphy(local->hw.wiphy);

	return rhltable_remove(&local->link_sta_hash,
			       &link_sta->link_hash_node, link_sta_rht_params);
}

void ieee80211_purge_sta_txqs(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->sdata->local;
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
		struct txq_info *txqi;

		if (!sta->sta.txq[i])
			continue;

		txqi = to_txq_info(sta->sta.txq[i]);

		ieee80211_txq_purge(local, txqi);
	}
}

static void __cleanup_single_sta(struct sta_info *sta)
{
	int ac, i;
	struct tid_ampdu_tx *tid_tx;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ps_data *ps;

	if (test_sta_flag(sta, WLAN_STA_PS_STA) ||
	    test_sta_flag(sta, WLAN_STA_PS_DRIVER) ||
	    test_sta_flag(sta, WLAN_STA_PS_DELIVER)) {
		if (sta->sdata->vif.type == NL80211_IFTYPE_AP ||
		    sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			ps = &sdata->bss->ps;
		else if (ieee80211_vif_is_mesh(&sdata->vif))
			ps = &sdata->u.mesh.ps;
		else
			return;

		clear_sta_flag(sta, WLAN_STA_PS_STA);
		clear_sta_flag(sta, WLAN_STA_PS_DRIVER);
		clear_sta_flag(sta, WLAN_STA_PS_DELIVER);

		atomic_dec(&ps->num_sta_ps);
	}

	ieee80211_purge_sta_txqs(sta);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		local->total_ps_buffered -= skb_queue_len(&sta->ps_tx_buf[ac]);
		ieee80211_purge_tx_queue(&local->hw, &sta->ps_tx_buf[ac]);
		ieee80211_purge_tx_queue(&local->hw, &sta->tx_filtered[ac]);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_sta_cleanup(sta);

	cancel_work_sync(&sta->drv_deliver_wk);

	/*
	 * Destroy aggregation state here. It would be nice to wait for the
	 * driver to finish aggregation stop and then clean up, but for now
	 * drivers have to handle aggregation stop being requested, followed
	 * directly by station destruction.
	 */
	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		kfree(sta->ampdu_mlme.tid_start_tx[i]);
		tid_tx = rcu_dereference_raw(sta->ampdu_mlme.tid_tx[i]);
		if (!tid_tx)
			continue;
		ieee80211_purge_tx_queue(&local->hw, &tid_tx->pending);
		kfree(tid_tx);
	}
}

static void cleanup_single_sta(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;

	__cleanup_single_sta(sta);
	sta_info_free(local, sta);
}

struct rhlist_head *sta_info_hash_lookup(struct ieee80211_local *local,
					 const u8 *addr)
{
	return rhltable_lookup(&local->sta_hash, addr, sta_rht_params);
}

/* protected by RCU */
struct sta_info *sta_info_get(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	struct rhlist_head *tmp;
	struct sta_info *sta;

	rcu_read_lock();
	for_each_sta_info(local, addr, sta, tmp) {
		if (sta->sdata == sdata) {
			rcu_read_unlock();
			/* this is safe as the caller must already hold
			 * another rcu read section or the mutex
			 */
			return sta;
		}
	}
	rcu_read_unlock();
	return NULL;
}

/*
 * Get sta info either from the specified interface
 * or from one of its vlans
 */
struct sta_info *sta_info_get_bss(struct ieee80211_sub_if_data *sdata,
				  const u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	struct rhlist_head *tmp;
	struct sta_info *sta;

	rcu_read_lock();
	for_each_sta_info(local, addr, sta, tmp) {
		if (sta->sdata == sdata ||
		    (sta->sdata->bss && sta->sdata->bss == sdata->bss)) {
			rcu_read_unlock();
			/* this is safe as the caller must already hold
			 * another rcu read section or the mutex
			 */
			return sta;
		}
	}
	rcu_read_unlock();
	return NULL;
}

struct rhlist_head *link_sta_info_hash_lookup(struct ieee80211_local *local,
					      const u8 *addr)
{
	return rhltable_lookup(&local->link_sta_hash, addr,
			       link_sta_rht_params);
}

struct link_sta_info *
link_sta_info_get_bss(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	struct ieee80211_local *local = sdata->local;
	struct rhlist_head *tmp;
	struct link_sta_info *link_sta;

	rcu_read_lock();
	for_each_link_sta_info(local, addr, link_sta, tmp) {
		struct sta_info *sta = link_sta->sta;

		if (sta->sdata == sdata ||
		    (sta->sdata->bss && sta->sdata->bss == sdata->bss)) {
			rcu_read_unlock();
			/* this is safe as the caller must already hold
			 * another rcu read section or the mutex
			 */
			return link_sta;
		}
	}
	rcu_read_unlock();
	return NULL;
}

struct ieee80211_sta *
ieee80211_find_sta_by_link_addrs(struct ieee80211_hw *hw,
				 const u8 *addr,
				 const u8 *localaddr,
				 unsigned int *link_id)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct link_sta_info *link_sta;
	struct rhlist_head *tmp;

	for_each_link_sta_info(local, addr, link_sta, tmp) {
		struct sta_info *sta = link_sta->sta;
		struct ieee80211_link_data *link;
		u8 _link_id = link_sta->link_id;

		if (!localaddr) {
			if (link_id)
				*link_id = _link_id;
			return &sta->sta;
		}

		link = rcu_dereference(sta->sdata->link[_link_id]);
		if (!link)
			continue;

		if (memcmp(link->conf->addr, localaddr, ETH_ALEN))
			continue;

		if (link_id)
			*link_id = _link_id;
		return &sta->sta;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ieee80211_find_sta_by_link_addrs);

struct sta_info *sta_info_get_by_addrs(struct ieee80211_local *local,
				       const u8 *sta_addr, const u8 *vif_addr)
{
	struct rhlist_head *tmp;
	struct sta_info *sta;

	for_each_sta_info(local, sta_addr, sta, tmp) {
		if (ether_addr_equal(vif_addr, sta->sdata->vif.addr))
			return sta;
	}

	return NULL;
}

struct sta_info *sta_info_get_by_idx(struct ieee80211_sub_if_data *sdata,
				     int idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int i = 0;

	list_for_each_entry_rcu(sta, &local->sta_list, list,
				lockdep_is_held(&local->hw.wiphy->mtx)) {
		if (sdata != sta->sdata)
			continue;
		if (i < idx) {
			++i;
			continue;
		}
		return sta;
	}

	return NULL;
}

static void sta_info_free_link(struct link_sta_info *link_sta)
{
	free_percpu(link_sta->pcpu_rx_stats);
}

static void sta_remove_link(struct sta_info *sta, unsigned int link_id,
			    bool unhash)
{
	struct sta_link_alloc *alloc = NULL;
	struct link_sta_info *link_sta;

	lockdep_assert_wiphy(sta->local->hw.wiphy);

	link_sta = rcu_access_pointer(sta->link[link_id]);
	if (WARN_ON(!link_sta))
		return;

	if (unhash)
		link_sta_info_hash_del(sta->local, link_sta);

	if (test_sta_flag(sta, WLAN_STA_INSERTED))
		ieee80211_link_sta_debugfs_remove(link_sta);

	if (link_sta != &sta->deflink)
		alloc = container_of(link_sta, typeof(*alloc), info);

	sta->sta.valid_links &= ~BIT(link_id);
	RCU_INIT_POINTER(sta->link[link_id], NULL);
	RCU_INIT_POINTER(sta->sta.link[link_id], NULL);
	if (alloc) {
		sta_info_free_link(&alloc->info);
		kfree_rcu(alloc, rcu_head);
	}

	ieee80211_sta_recalc_aggregates(&sta->sta);
}

/**
 * sta_info_free - free STA
 *
 * @local: pointer to the global information
 * @sta: STA info to free
 *
 * This function must undo everything done by sta_info_alloc()
 * that may happen before sta_info_insert(). It may only be
 * called when sta_info_insert() has not been attempted (and
 * if that fails, the station is freed anyway.)
 */
void sta_info_free(struct ieee80211_local *local, struct sta_info *sta)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->link); i++) {
		struct link_sta_info *link_sta;

		link_sta = rcu_access_pointer(sta->link[i]);
		if (!link_sta)
			continue;

		sta_remove_link(sta, i, false);
	}

	/*
	 * If we had used sta_info_pre_move_state() then we might not
	 * have gone through the state transitions down again, so do
	 * it here now (and warn if it's inserted).
	 *
	 * This will clear state such as fast TX/RX that may have been
	 * allocated during state transitions.
	 */
	while (sta->sta_state > IEEE80211_STA_NONE) {
		int ret;

		WARN_ON_ONCE(test_sta_flag(sta, WLAN_STA_INSERTED));

		ret = sta_info_move_state(sta, sta->sta_state - 1);
		if (WARN_ONCE(ret, "sta_info_move_state() returned %d\n", ret))
			break;
	}

	if (sta->rate_ctrl)
		rate_control_free_sta(sta);

	sta_dbg(sta->sdata, "Destroyed STA %pM\n", sta->sta.addr);

	kfree(to_txq_info(sta->sta.txq[0]));
	kfree(rcu_dereference_raw(sta->sta.rates));
#ifdef CONFIG_MAC80211_MESH
	kfree(sta->mesh);
#endif

	sta_info_free_link(&sta->deflink);
	kfree(sta);
}

static int sta_info_hash_add(struct ieee80211_local *local,
			     struct sta_info *sta)
{
	return rhltable_insert(&local->sta_hash, &sta->hash_node,
			       sta_rht_params);
}

static void sta_deliver_ps_frames(struct work_struct *wk)
{
	struct sta_info *sta;

	sta = container_of(wk, struct sta_info, drv_deliver_wk);

	if (sta->dead)
		return;

	local_bh_disable();
	if (!test_sta_flag(sta, WLAN_STA_PS_STA))
		ieee80211_sta_ps_deliver_wakeup(sta);
	else if (test_and_clear_sta_flag(sta, WLAN_STA_PSPOLL))
		ieee80211_sta_ps_deliver_poll_response(sta);
	else if (test_and_clear_sta_flag(sta, WLAN_STA_UAPSD))
		ieee80211_sta_ps_deliver_uapsd(sta);
	local_bh_enable();
}

static int sta_prepare_rate_control(struct ieee80211_local *local,
				    struct sta_info *sta, gfp_t gfp)
{
	if (ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL))
		return 0;

	sta->rate_ctrl = local->rate_ctrl;
	sta->rate_ctrl_priv = rate_control_alloc_sta(sta->rate_ctrl,
						     sta, gfp);
	if (!sta->rate_ctrl_priv)
		return -ENOMEM;

	return 0;
}

static int sta_info_alloc_link(struct ieee80211_local *local,
			       struct link_sta_info *link_info,
			       gfp_t gfp)
{
	struct ieee80211_hw *hw = &local->hw;
	int i;

	if (ieee80211_hw_check(hw, USES_RSS)) {
		link_info->pcpu_rx_stats =
			alloc_percpu_gfp(struct ieee80211_sta_rx_stats, gfp);
		if (!link_info->pcpu_rx_stats)
			return -ENOMEM;
	}

	link_info->rx_stats.last_rx = jiffies;
	u64_stats_init(&link_info->rx_stats.syncp);

	ewma_signal_init(&link_info->rx_stats_avg.signal);
	ewma_avg_signal_init(&link_info->status_stats.avg_ack_signal);
	for (i = 0; i < ARRAY_SIZE(link_info->rx_stats_avg.chain_signal); i++)
		ewma_signal_init(&link_info->rx_stats_avg.chain_signal[i]);

	link_info->rx_omi_bw_rx = IEEE80211_STA_RX_BW_MAX;
	link_info->rx_omi_bw_tx = IEEE80211_STA_RX_BW_MAX;
	link_info->rx_omi_bw_staging = IEEE80211_STA_RX_BW_MAX;

	/*
	 * Cause (a) warning(s) if IEEE80211_STA_RX_BW_MAX != 320
	 * or if new values are added to the enum.
	 */
	switch (link_info->cur_max_bandwidth) {
	case IEEE80211_STA_RX_BW_20:
	case IEEE80211_STA_RX_BW_40:
	case IEEE80211_STA_RX_BW_80:
	case IEEE80211_STA_RX_BW_160:
	case IEEE80211_STA_RX_BW_MAX:
		/* intentionally nothing */
		break;
	}

	return 0;
}

static void sta_info_add_link(struct sta_info *sta,
			      unsigned int link_id,
			      struct link_sta_info *link_info,
			      struct ieee80211_link_sta *link_sta)
{
	link_info->sta = sta;
	link_info->link_id = link_id;
	link_info->pub = link_sta;
	link_info->pub->sta = &sta->sta;
	link_sta->link_id = link_id;
	rcu_assign_pointer(sta->link[link_id], link_info);
	rcu_assign_pointer(sta->sta.link[link_id], link_sta);

	link_sta->smps_mode = IEEE80211_SMPS_OFF;
	link_sta->agg.max_rc_amsdu_len = IEEE80211_MAX_MPDU_LEN_HT_BA;
}

static struct sta_info *
__sta_info_alloc(struct ieee80211_sub_if_data *sdata,
		 const u8 *addr, int link_id, const u8 *link_addr,
		 gfp_t gfp)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	void *txq_data;
	int size;
	int i;

	sta = kzalloc(sizeof(*sta) + hw->sta_data_size, gfp);
	if (!sta)
		return NULL;

	sta->local = local;
	sta->sdata = sdata;

	if (sta_info_alloc_link(local, &sta->deflink, gfp))
		goto free;

	if (link_id >= 0) {
		sta_info_add_link(sta, link_id, &sta->deflink,
				  &sta->sta.deflink);
		sta->sta.valid_links = BIT(link_id);
	} else {
		sta_info_add_link(sta, 0, &sta->deflink, &sta->sta.deflink);
	}

	sta->sta.cur = &sta->sta.deflink.agg;

	spin_lock_init(&sta->lock);
	spin_lock_init(&sta->ps_lock);
	INIT_WORK(&sta->drv_deliver_wk, sta_deliver_ps_frames);
	wiphy_work_init(&sta->ampdu_mlme.work, ieee80211_ba_session_work);
#ifdef CONFIG_MAC80211_MESH
	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		sta->mesh = kzalloc(sizeof(*sta->mesh), gfp);
		if (!sta->mesh)
			goto free;
		sta->mesh->plink_sta = sta;
		spin_lock_init(&sta->mesh->plink_lock);
		if (!sdata->u.mesh.user_mpm)
			timer_setup(&sta->mesh->plink_timer, mesh_plink_timer,
				    0);
		sta->mesh->nonpeer_pm = NL80211_MESH_POWER_ACTIVE;
	}
#endif

	memcpy(sta->addr, addr, ETH_ALEN);
	memcpy(sta->sta.addr, addr, ETH_ALEN);
	memcpy(sta->deflink.addr, link_addr, ETH_ALEN);
	memcpy(sta->sta.deflink.addr, link_addr, ETH_ALEN);
	sta->sta.max_rx_aggregation_subframes =
		local->hw.max_rx_aggregation_subframes;

	/* TODO link specific alloc and assignments for MLO Link STA */

	/* Extended Key ID needs to install keys for keyid 0 and 1 Rx-only.
	 * The Tx path starts to use a key as soon as the key slot ptk_idx
	 * references to is not NULL. To not use the initial Rx-only key
	 * prematurely for Tx initialize ptk_idx to an impossible PTK keyid
	 * which always will refer to a NULL key.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(sta->ptk) <= INVALID_PTK_KEYIDX);
	sta->ptk_idx = INVALID_PTK_KEYIDX;


	ieee80211_init_frag_cache(&sta->frags);

	sta->sta_state = IEEE80211_STA_NONE;

	if (sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
		sta->amsdu_mesh_control = -1;

	/* Mark TID as unreserved */
	sta->reserved_tid = IEEE80211_TID_UNRESERVED;

	sta->last_connected = ktime_get_seconds();

	size = sizeof(struct txq_info) +
	       ALIGN(hw->txq_data_size, sizeof(void *));

	txq_data = kcalloc(ARRAY_SIZE(sta->sta.txq), size, gfp);
	if (!txq_data)
		goto free;

	for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
		struct txq_info *txq = txq_data + i * size;

		/* might not do anything for the (bufferable) MMPDU TXQ */
		ieee80211_txq_init(sdata, sta, txq, i);
	}

	if (sta_prepare_rate_control(local, sta, gfp))
		goto free_txq;

	sta->airtime_weight = IEEE80211_DEFAULT_AIRTIME_WEIGHT;

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		skb_queue_head_init(&sta->ps_tx_buf[i]);
		skb_queue_head_init(&sta->tx_filtered[i]);
		sta->airtime[i].deficit = sta->airtime_weight;
		atomic_set(&sta->airtime[i].aql_tx_pending, 0);
		sta->airtime[i].aql_limit_low = local->aql_txq_limit_low[i];
		sta->airtime[i].aql_limit_high = local->aql_txq_limit_high[i];
	}

	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		sta->last_seq_ctrl[i] = cpu_to_le16(USHRT_MAX);

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		u32 mandatory = 0;
		int r;

		if (!hw->wiphy->bands[i])
			continue;

		switch (i) {
		case NL80211_BAND_2GHZ:
		case NL80211_BAND_LC:
			/*
			 * We use both here, even if we cannot really know for
			 * sure the station will support both, but the only use
			 * for this is when we don't know anything yet and send
			 * management frames, and then we'll pick the lowest
			 * possible rate anyway.
			 * If we don't include _G here, we cannot find a rate
			 * in P2P, and thus trigger the WARN_ONCE() in rate.c
			 */
			mandatory = IEEE80211_RATE_MANDATORY_B |
				    IEEE80211_RATE_MANDATORY_G;
			break;
		case NL80211_BAND_5GHZ:
			mandatory = IEEE80211_RATE_MANDATORY_A;
			break;
		case NL80211_BAND_60GHZ:
			WARN_ON(1);
			mandatory = 0;
			break;
		}

		for (r = 0; r < hw->wiphy->bands[i]->n_bitrates; r++) {
			struct ieee80211_rate *rate;

			rate = &hw->wiphy->bands[i]->bitrates[r];

			if (!(rate->flags & mandatory))
				continue;
			sta->sta.deflink.supp_rates[i] |= BIT(r);
		}
	}

	sta->cparams.ce_threshold = CODEL_DISABLED_THRESHOLD;
	sta->cparams.target = MS2TIME(20);
	sta->cparams.interval = MS2TIME(100);
	sta->cparams.ecn = true;
	sta->cparams.ce_threshold_selector = 0;
	sta->cparams.ce_threshold_mask = 0;

	sta_dbg(sdata, "Allocated STA %pM\n", sta->sta.addr);

	return sta;

free_txq:
	kfree(to_txq_info(sta->sta.txq[0]));
free:
	sta_info_free_link(&sta->deflink);
#ifdef CONFIG_MAC80211_MESH
	kfree(sta->mesh);
#endif
	kfree(sta);
	return NULL;
}

struct sta_info *sta_info_alloc(struct ieee80211_sub_if_data *sdata,
				const u8 *addr, gfp_t gfp)
{
	return __sta_info_alloc(sdata, addr, -1, addr, gfp);
}

struct sta_info *sta_info_alloc_with_link(struct ieee80211_sub_if_data *sdata,
					  const u8 *mld_addr,
					  unsigned int link_id,
					  const u8 *link_addr,
					  gfp_t gfp)
{
	return __sta_info_alloc(sdata, mld_addr, link_id, link_addr, gfp);
}

static int sta_info_insert_check(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	/*
	 * Can't be a WARN_ON because it can be triggered through a race:
	 * something inserts a STA (on one CPU) without holding the RTNL
	 * and another CPU turns off the net device.
	 */
	if (unlikely(!ieee80211_sdata_running(sdata)))
		return -ENETDOWN;

	if (WARN_ON(ether_addr_equal(sta->sta.addr, sdata->vif.addr) ||
		    !is_valid_ether_addr(sta->sta.addr)))
		return -EINVAL;

	/* The RCU read lock is required by rhashtable due to
	 * asynchronous resize/rehash.  We also require the mutex
	 * for correctness.
	 */
	rcu_read_lock();
	if (ieee80211_hw_check(&sdata->local->hw, NEEDS_UNIQUE_STA_ADDR) &&
	    ieee80211_find_sta_by_ifaddr(&sdata->local->hw, sta->addr, NULL)) {
		rcu_read_unlock();
		return -ENOTUNIQ;
	}
	rcu_read_unlock();

	return 0;
}

static int sta_info_insert_drv_state(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     struct sta_info *sta)
{
	enum ieee80211_sta_state state;
	int err = 0;

	for (state = IEEE80211_STA_NOTEXIST; state < sta->sta_state; state++) {
		err = drv_sta_state(local, sdata, sta, state, state + 1);
		if (err)
			break;
	}

	if (!err) {
		/*
		 * Drivers using legacy sta_add/sta_remove callbacks only
		 * get uploaded set to true after sta_add is called.
		 */
		if (!local->ops->sta_add)
			sta->uploaded = true;
		return 0;
	}

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		sdata_info(sdata,
			   "failed to move IBSS STA %pM to state %d (%d) - keeping it anyway\n",
			   sta->sta.addr, state + 1, err);
		err = 0;
	}

	/* unwind on error */
	for (; state > IEEE80211_STA_NOTEXIST; state--)
		WARN_ON(drv_sta_state(local, sdata, sta, state, state - 1));

	return err;
}

static void
ieee80211_recalc_p2p_go_ps_allowed(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	bool allow_p2p_go_ps = sdata->vif.p2p;
	struct sta_info *sta;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata ||
		    !test_sta_flag(sta, WLAN_STA_ASSOC))
			continue;
		if (!sta->sta.support_p2p_ps) {
			allow_p2p_go_ps = false;
			break;
		}
	}
	rcu_read_unlock();

	if (allow_p2p_go_ps != sdata->vif.bss_conf.allow_p2p_go_ps) {
		sdata->vif.bss_conf.allow_p2p_go_ps = allow_p2p_go_ps;
		ieee80211_link_info_change_notify(sdata, &sdata->deflink,
						  BSS_CHANGED_P2P_PS);
	}
}

static int sta_info_insert_finish(struct sta_info *sta) __acquires(RCU)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct station_info *sinfo = NULL;
	int err = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* check if STA exists already */
	if (sta_info_get_bss(sdata, sta->sta.addr)) {
		err = -EEXIST;
		goto out_cleanup;
	}

	sinfo = kzalloc(sizeof(struct station_info), GFP_KERNEL);
	if (!sinfo) {
		err = -ENOMEM;
		goto out_cleanup;
	}

	local->num_sta++;
	local->sta_generation++;
	smp_mb();

	/* simplify things and don't accept BA sessions yet */
	set_sta_flag(sta, WLAN_STA_BLOCK_BA);

	/* make the station visible */
	err = sta_info_hash_add(local, sta);
	if (err)
		goto out_drop_sta;

	if (sta->sta.valid_links) {
		err = link_sta_info_hash_add(local, &sta->deflink);
		if (err) {
			sta_info_hash_del(local, sta);
			goto out_drop_sta;
		}
	}

	list_add_tail_rcu(&sta->list, &local->sta_list);

	/* update channel context before notifying the driver about state
	 * change, this enables driver using the updated channel context right away.
	 */
	if (sta->sta_state >= IEEE80211_STA_ASSOC) {
		ieee80211_recalc_min_chandef(sta->sdata, -1);
		if (!sta->sta.support_p2p_ps)
			ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
	}

	/* notify driver */
	err = sta_info_insert_drv_state(local, sdata, sta);
	if (err)
		goto out_remove;

	set_sta_flag(sta, WLAN_STA_INSERTED);

	/* accept BA sessions now */
	clear_sta_flag(sta, WLAN_STA_BLOCK_BA);

	ieee80211_sta_debugfs_add(sta);
	rate_control_add_sta_debugfs(sta);
	if (sta->sta.valid_links) {
		int i;

		for (i = 0; i < ARRAY_SIZE(sta->link); i++) {
			struct link_sta_info *link_sta;

			link_sta = rcu_dereference_protected(sta->link[i],
							     lockdep_is_held(&local->hw.wiphy->mtx));

			if (!link_sta)
				continue;

			ieee80211_link_sta_debugfs_add(link_sta);
			if (sdata->vif.active_links & BIT(i))
				ieee80211_link_sta_debugfs_drv_add(link_sta);
		}
	} else {
		ieee80211_link_sta_debugfs_add(&sta->deflink);
		ieee80211_link_sta_debugfs_drv_add(&sta->deflink);
	}

	sinfo->generation = local->sta_generation;
	cfg80211_new_sta(sdata->dev, sta->sta.addr, sinfo, GFP_KERNEL);
	kfree(sinfo);

	sta_dbg(sdata, "Inserted STA %pM\n", sta->sta.addr);

	/* move reference to rcu-protected */
	rcu_read_lock();

	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_accept_plinks_update(sdata);

	ieee80211_check_fast_xmit(sta);

	return 0;
 out_remove:
	if (sta->sta.valid_links)
		link_sta_info_hash_del(local, &sta->deflink);
	sta_info_hash_del(local, sta);
	list_del_rcu(&sta->list);
 out_drop_sta:
	local->num_sta--;
	synchronize_net();
 out_cleanup:
	cleanup_single_sta(sta);
	kfree(sinfo);
	rcu_read_lock();
	return err;
}

int sta_info_insert_rcu(struct sta_info *sta) __acquires(RCU)
{
	struct ieee80211_local *local = sta->local;
	int err;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	err = sta_info_insert_check(sta);
	if (err) {
		sta_info_free(local, sta);
		rcu_read_lock();
		return err;
	}

	return sta_info_insert_finish(sta);
}

int sta_info_insert(struct sta_info *sta)
{
	int err = sta_info_insert_rcu(sta);

	rcu_read_unlock();

	return err;
}

static inline void __bss_tim_set(u8 *tim, u16 id)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __set_bit() format.
	 */
	tim[id / 8] |= (1 << (id % 8));
}

static inline void __bss_tim_clear(u8 *tim, u16 id)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __clear_bit() format.
	 */
	tim[id / 8] &= ~(1 << (id % 8));
}

static inline bool __bss_tim_get(u8 *tim, u16 id)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the test_bit() format.
	 */
	return tim[id / 8] & (1 << (id % 8));
}

static unsigned long ieee80211_tids_for_ac(int ac)
{
	/* If we ever support TIDs > 7, this obviously needs to be adjusted */
	switch (ac) {
	case IEEE80211_AC_VO:
		return BIT(6) | BIT(7);
	case IEEE80211_AC_VI:
		return BIT(4) | BIT(5);
	case IEEE80211_AC_BE:
		return BIT(0) | BIT(3);
	case IEEE80211_AC_BK:
		return BIT(1) | BIT(2);
	default:
		WARN_ON(1);
		return 0;
	}
}

static void __sta_info_recalc_tim(struct sta_info *sta, bool ignore_pending)
{
	struct ieee80211_local *local = sta->local;
	struct ps_data *ps;
	bool indicate_tim = false;
	u8 ignore_for_tim = sta->sta.uapsd_queues;
	int ac;
	u16 id = sta->sta.aid;

	if (sta->sdata->vif.type == NL80211_IFTYPE_AP ||
	    sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		if (WARN_ON_ONCE(!sta->sdata->bss))
			return;

		ps = &sta->sdata->bss->ps;
#ifdef CONFIG_MAC80211_MESH
	} else if (ieee80211_vif_is_mesh(&sta->sdata->vif)) {
		ps = &sta->sdata->u.mesh.ps;
#endif
	} else {
		return;
	}

	/* No need to do anything if the driver does all */
	if (ieee80211_hw_check(&local->hw, AP_LINK_PS) && !local->ops->set_tim)
		return;

	if (sta->dead)
		goto done;

	/*
	 * If all ACs are delivery-enabled then we should build
	 * the TIM bit for all ACs anyway; if only some are then
	 * we ignore those and build the TIM bit using only the
	 * non-enabled ones.
	 */
	if (ignore_for_tim == BIT(IEEE80211_NUM_ACS) - 1)
		ignore_for_tim = 0;

	if (ignore_pending)
		ignore_for_tim = BIT(IEEE80211_NUM_ACS) - 1;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		unsigned long tids;

		if (ignore_for_tim & ieee80211_ac_to_qos_mask[ac])
			continue;

		indicate_tim |= !skb_queue_empty(&sta->tx_filtered[ac]) ||
				!skb_queue_empty(&sta->ps_tx_buf[ac]);
		if (indicate_tim)
			break;

		tids = ieee80211_tids_for_ac(ac);

		indicate_tim |=
			sta->driver_buffered_tids & tids;
		indicate_tim |=
			sta->txq_buffered_tids & tids;
	}

 done:
	spin_lock_bh(&local->tim_lock);

	if (indicate_tim == __bss_tim_get(ps->tim, id))
		goto out_unlock;

	if (indicate_tim)
		__bss_tim_set(ps->tim, id);
	else
		__bss_tim_clear(ps->tim, id);

	if (local->ops->set_tim && !WARN_ON(sta->dead)) {
		local->tim_in_locked_section = true;
		drv_set_tim(local, &sta->sta, indicate_tim);
		local->tim_in_locked_section = false;
	}

out_unlock:
	spin_unlock_bh(&local->tim_lock);
}

void sta_info_recalc_tim(struct sta_info *sta)
{
	__sta_info_recalc_tim(sta, false);
}

static bool sta_info_buffer_expired(struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;
	int timeout;

	if (!skb)
		return false;

	info = IEEE80211_SKB_CB(skb);

	/* Timeout: (2 * listen_interval * beacon_int * 1024 / 1000000) sec */
	timeout = (sta->listen_interval *
		   sta->sdata->vif.bss_conf.beacon_int *
		   32 / 15625) * HZ;
	if (timeout < STA_TX_BUFFER_EXPIRE)
		timeout = STA_TX_BUFFER_EXPIRE;
	return time_after(jiffies, info->control.jiffies + timeout);
}


static bool sta_info_cleanup_expire_buffered_ac(struct ieee80211_local *local,
						struct sta_info *sta, int ac)
{
	unsigned long flags;
	struct sk_buff *skb;

	/*
	 * First check for frames that should expire on the filtered
	 * queue. Frames here were rejected by the driver and are on
	 * a separate queue to avoid reordering with normal PS-buffered
	 * frames. They also aren't accounted for right now in the
	 * total_ps_buffered counter.
	 */
	for (;;) {
		spin_lock_irqsave(&sta->tx_filtered[ac].lock, flags);
		skb = skb_peek(&sta->tx_filtered[ac]);
		if (sta_info_buffer_expired(sta, skb))
			skb = __skb_dequeue(&sta->tx_filtered[ac]);
		else
			skb = NULL;
		spin_unlock_irqrestore(&sta->tx_filtered[ac].lock, flags);

		/*
		 * Frames are queued in order, so if this one
		 * hasn't expired yet we can stop testing. If
		 * we actually reached the end of the queue we
		 * also need to stop, of course.
		 */
		if (!skb)
			break;
		ieee80211_free_txskb(&local->hw, skb);
	}

	/*
	 * Now also check the normal PS-buffered queue, this will
	 * only find something if the filtered queue was emptied
	 * since the filtered frames are all before the normal PS
	 * buffered frames.
	 */
	for (;;) {
		spin_lock_irqsave(&sta->ps_tx_buf[ac].lock, flags);
		skb = skb_peek(&sta->ps_tx_buf[ac]);
		if (sta_info_buffer_expired(sta, skb))
			skb = __skb_dequeue(&sta->ps_tx_buf[ac]);
		else
			skb = NULL;
		spin_unlock_irqrestore(&sta->ps_tx_buf[ac].lock, flags);

		/*
		 * frames are queued in order, so if this one
		 * hasn't expired yet (or we reached the end of
		 * the queue) we can stop testing
		 */
		if (!skb)
			break;

		local->total_ps_buffered--;
		ps_dbg(sta->sdata, "Buffered frame expired (STA %pM)\n",
		       sta->sta.addr);
		ieee80211_free_txskb(&local->hw, skb);
	}

	/*
	 * Finally, recalculate the TIM bit for this station -- it might
	 * now be clear because the station was too slow to retrieve its
	 * frames.
	 */
	sta_info_recalc_tim(sta);

	/*
	 * Return whether there are any frames still buffered, this is
	 * used to check whether the cleanup timer still needs to run,
	 * if there are no frames we don't need to rearm the timer.
	 */
	return !(skb_queue_empty(&sta->ps_tx_buf[ac]) &&
		 skb_queue_empty(&sta->tx_filtered[ac]));
}

static bool sta_info_cleanup_expire_buffered(struct ieee80211_local *local,
					     struct sta_info *sta)
{
	bool have_buffered = false;
	int ac;

	/* This is only necessary for stations on BSS/MBSS interfaces */
	if (!sta->sdata->bss &&
	    !ieee80211_vif_is_mesh(&sta->sdata->vif))
		return false;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		have_buffered |=
			sta_info_cleanup_expire_buffered_ac(local, sta, ac);

	return have_buffered;
}

static int __must_check __sta_info_destroy_part1(struct sta_info *sta)
{
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	int ret, i;

	might_sleep();

	if (!sta)
		return -ENOENT;

	local = sta->local;
	sdata = sta->sdata;

	lockdep_assert_wiphy(local->hw.wiphy);

	/*
	 * Before removing the station from the driver and
	 * rate control, it might still start new aggregation
	 * sessions -- block that to make sure the tear-down
	 * will be sufficient.
	 */
	set_sta_flag(sta, WLAN_STA_BLOCK_BA);
	ieee80211_sta_tear_down_BA_sessions(sta, AGG_STOP_DESTROY_STA);

	/*
	 * Before removing the station from the driver there might be pending
	 * rx frames on RSS queues sent prior to the disassociation - wait for
	 * all such frames to be processed.
	 */
	drv_sync_rx_queues(local, sta);

	for (i = 0; i < ARRAY_SIZE(sta->link); i++) {
		struct link_sta_info *link_sta;

		if (!(sta->sta.valid_links & BIT(i)))
			continue;

		link_sta = rcu_dereference_protected(sta->link[i],
						     lockdep_is_held(&local->hw.wiphy->mtx));

		link_sta_info_hash_del(local, link_sta);
	}

	ret = sta_info_hash_del(local, sta);
	if (WARN_ON(ret))
		return ret;

	/*
	 * for TDLS peers, make sure to return to the base channel before
	 * removal.
	 */
	if (test_sta_flag(sta, WLAN_STA_TDLS_OFF_CHANNEL)) {
		drv_tdls_cancel_channel_switch(local, sdata, &sta->sta);
		clear_sta_flag(sta, WLAN_STA_TDLS_OFF_CHANNEL);
	}

	list_del_rcu(&sta->list);
	sta->removed = true;

	if (sta->uploaded)
		drv_sta_pre_rcu_remove(local, sta->sdata, sta);

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
	    rcu_access_pointer(sdata->u.vlan.sta) == sta)
		RCU_INIT_POINTER(sdata->u.vlan.sta, NULL);

	return 0;
}

static int _sta_info_move_state(struct sta_info *sta,
				enum ieee80211_sta_state new_state,
				bool recalc)
{
	struct ieee80211_local *local = sta->local;

	might_sleep();

	if (sta->sta_state == new_state)
		return 0;

	/* check allowed transitions first */

	switch (new_state) {
	case IEEE80211_STA_NONE:
		if (sta->sta_state != IEEE80211_STA_AUTH)
			return -EINVAL;
		break;
	case IEEE80211_STA_AUTH:
		if (sta->sta_state != IEEE80211_STA_NONE &&
		    sta->sta_state != IEEE80211_STA_ASSOC)
			return -EINVAL;
		break;
	case IEEE80211_STA_ASSOC:
		if (sta->sta_state != IEEE80211_STA_AUTH &&
		    sta->sta_state != IEEE80211_STA_AUTHORIZED)
			return -EINVAL;
		break;
	case IEEE80211_STA_AUTHORIZED:
		if (sta->sta_state != IEEE80211_STA_ASSOC)
			return -EINVAL;
		break;
	default:
		WARN(1, "invalid state %d", new_state);
		return -EINVAL;
	}

	sta_dbg(sta->sdata, "moving STA %pM to state %d\n",
		sta->sta.addr, new_state);

	/* notify the driver before the actual changes so it can
	 * fail the transition if the state is increasing.
	 * The driver is required not to fail when the transition
	 * is decreasing the state, so first, do all the preparation
	 * work and only then, notify the driver.
	 */
	if (new_state > sta->sta_state &&
	    test_sta_flag(sta, WLAN_STA_INSERTED)) {
		int err = drv_sta_state(sta->local, sta->sdata, sta,
					sta->sta_state, new_state);
		if (err)
			return err;
	}

	/* reflect the change in all state variables */

	switch (new_state) {
	case IEEE80211_STA_NONE:
		if (sta->sta_state == IEEE80211_STA_AUTH)
			clear_bit(WLAN_STA_AUTH, &sta->_flags);
		break;
	case IEEE80211_STA_AUTH:
		if (sta->sta_state == IEEE80211_STA_NONE) {
			set_bit(WLAN_STA_AUTH, &sta->_flags);
		} else if (sta->sta_state == IEEE80211_STA_ASSOC) {
			clear_bit(WLAN_STA_ASSOC, &sta->_flags);
			if (recalc) {
				ieee80211_recalc_min_chandef(sta->sdata, -1);
				if (!sta->sta.support_p2p_ps)
					ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
			}
		}
		break;
	case IEEE80211_STA_ASSOC:
		if (sta->sta_state == IEEE80211_STA_AUTH) {
			set_bit(WLAN_STA_ASSOC, &sta->_flags);
			sta->assoc_at = ktime_get_boottime_ns();
			if (recalc) {
				ieee80211_recalc_min_chandef(sta->sdata, -1);
				if (!sta->sta.support_p2p_ps)
					ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
			}
		} else if (sta->sta_state == IEEE80211_STA_AUTHORIZED) {
			ieee80211_vif_dec_num_mcast(sta->sdata);
			clear_bit(WLAN_STA_AUTHORIZED, &sta->_flags);

			/*
			 * If we have encryption offload, flush (station) queues
			 * (after ensuring concurrent TX completed) so we won't
			 * transmit anything later unencrypted if/when keys are
			 * also removed, which might otherwise happen depending
			 * on how the hardware offload works.
			 */
			if (local->ops->set_key) {
				synchronize_net();
				if (local->ops->flush_sta)
					drv_flush_sta(local, sta->sdata, sta);
				else
					ieee80211_flush_queues(local,
							       sta->sdata,
							       false);
			}

			ieee80211_clear_fast_xmit(sta);
			ieee80211_clear_fast_rx(sta);
		}
		break;
	case IEEE80211_STA_AUTHORIZED:
		if (sta->sta_state == IEEE80211_STA_ASSOC) {
			ieee80211_vif_inc_num_mcast(sta->sdata);
			set_bit(WLAN_STA_AUTHORIZED, &sta->_flags);
			ieee80211_check_fast_xmit(sta);
			ieee80211_check_fast_rx(sta);
		}
		if (sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		    sta->sdata->vif.type == NL80211_IFTYPE_AP)
			cfg80211_send_layer2_update(sta->sdata->dev,
						    sta->sta.addr);
		break;
	default:
		break;
	}

	if (new_state < sta->sta_state &&
	    test_sta_flag(sta, WLAN_STA_INSERTED)) {
		int err = drv_sta_state(sta->local, sta->sdata, sta,
					sta->sta_state, new_state);

		WARN_ONCE(err,
			  "Driver is not allowed to fail if the sta_state is transitioning down the list: %d\n",
			  err);
	}

	sta->sta_state = new_state;

	return 0;
}

int sta_info_move_state(struct sta_info *sta,
			enum ieee80211_sta_state new_state)
{
	return _sta_info_move_state(sta, new_state, true);
}

static void __sta_info_destroy_part2(struct sta_info *sta, bool recalc)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct station_info *sinfo;
	int ret;

	/*
	 * NOTE: This assumes at least synchronize_net() was done
	 *	 after _part1 and before _part2!
	 */

	/*
	 * There's a potential race in _part1 where we set WLAN_STA_BLOCK_BA
	 * but someone might have just gotten past a check, and not yet into
	 * queuing the work/creating the data/etc.
	 *
	 * Do another round of destruction so that the worker is certainly
	 * canceled before we later free the station.
	 *
	 * Since this is after synchronize_rcu()/synchronize_net() we're now
	 * certain that nobody can actually hold a reference to the STA and
	 * be calling e.g. ieee80211_start_tx_ba_session().
	 */
	ieee80211_sta_tear_down_BA_sessions(sta, AGG_STOP_DESTROY_STA);

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (sta->sta_state == IEEE80211_STA_AUTHORIZED) {
		ret = _sta_info_move_state(sta, IEEE80211_STA_ASSOC, recalc);
		WARN_ON_ONCE(ret);
	}

	/* now keys can no longer be reached */
	ieee80211_free_sta_keys(local, sta);

	/* disable TIM bit - last chance to tell driver */
	__sta_info_recalc_tim(sta, true);

	sta->dead = true;

	local->num_sta--;
	local->sta_generation++;

	while (sta->sta_state > IEEE80211_STA_NONE) {
		ret = _sta_info_move_state(sta, sta->sta_state - 1, recalc);
		if (ret) {
			WARN_ON_ONCE(1);
			break;
		}
	}

	if (sta->uploaded) {
		ret = drv_sta_state(local, sdata, sta, IEEE80211_STA_NONE,
				    IEEE80211_STA_NOTEXIST);
		WARN_ON_ONCE(ret != 0);
	}

	sta_dbg(sdata, "Removed STA %pM\n", sta->sta.addr);

	sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
	if (sinfo)
		sta_set_sinfo(sta, sinfo, true);
	cfg80211_del_sta_sinfo(sdata->dev, sta->sta.addr, sinfo, GFP_KERNEL);
	kfree(sinfo);

	ieee80211_sta_debugfs_remove(sta);

	ieee80211_destroy_frag_cache(&sta->frags);

	cleanup_single_sta(sta);
}

int __must_check __sta_info_destroy(struct sta_info *sta)
{
	int err = __sta_info_destroy_part1(sta);

	if (err)
		return err;

	synchronize_net();

	__sta_info_destroy_part2(sta, true);

	return 0;
}

int sta_info_destroy_addr(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	struct sta_info *sta;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	sta = sta_info_get(sdata, addr);
	return __sta_info_destroy(sta);
}

int sta_info_destroy_addr_bss(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr)
{
	struct sta_info *sta;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	sta = sta_info_get_bss(sdata, addr);
	return __sta_info_destroy(sta);
}

static void sta_info_cleanup(struct timer_list *t)
{
	struct ieee80211_local *local = from_timer(local, t, sta_cleanup);
	struct sta_info *sta;
	bool timer_needed = false;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list)
		if (sta_info_cleanup_expire_buffered(local, sta))
			timer_needed = true;
	rcu_read_unlock();

	if (local->quiescing)
		return;

	if (!timer_needed)
		return;

	mod_timer(&local->sta_cleanup,
		  round_jiffies(jiffies + STA_INFO_CLEANUP_INTERVAL));
}

int sta_info_init(struct ieee80211_local *local)
{
	int err;

	err = rhltable_init(&local->sta_hash, &sta_rht_params);
	if (err)
		return err;

	err = rhltable_init(&local->link_sta_hash, &link_sta_rht_params);
	if (err) {
		rhltable_destroy(&local->sta_hash);
		return err;
	}

	spin_lock_init(&local->tim_lock);
	INIT_LIST_HEAD(&local->sta_list);

	timer_setup(&local->sta_cleanup, sta_info_cleanup, 0);
	return 0;
}

void sta_info_stop(struct ieee80211_local *local)
{
	del_timer_sync(&local->sta_cleanup);
	rhltable_destroy(&local->sta_hash);
	rhltable_destroy(&local->link_sta_hash);
}


int __sta_info_flush(struct ieee80211_sub_if_data *sdata, bool vlans,
		     int link_id, struct sta_info *do_not_flush_sta)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;
	LIST_HEAD(free_list);
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	WARN_ON(vlans && sdata->vif.type != NL80211_IFTYPE_AP);
	WARN_ON(vlans && !sdata->bss);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		if (sdata != sta->sdata &&
		    (!vlans || sdata->bss != sta->sdata->bss))
			continue;

		if (sta == do_not_flush_sta)
			continue;

		if (link_id >= 0 && sta->sta.valid_links &&
		    !(sta->sta.valid_links & BIT(link_id)))
			continue;

		if (!WARN_ON(__sta_info_destroy_part1(sta)))
			list_add(&sta->free_list, &free_list);

		ret++;
	}

	if (!list_empty(&free_list)) {
		bool support_p2p_ps = true;

		synchronize_net();
		list_for_each_entry_safe(sta, tmp, &free_list, free_list) {
			if (!sta->sta.support_p2p_ps)
				support_p2p_ps = false;
			__sta_info_destroy_part2(sta, false);
		}

		ieee80211_recalc_min_chandef(sdata, -1);
		if (!support_p2p_ps)
			ieee80211_recalc_p2p_go_ps_allowed(sdata);
	}

	return ret;
}

void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		unsigned long last_active = ieee80211_sta_last_active(sta);

		if (sdata != sta->sdata)
			continue;

		if (time_is_before_jiffies(last_active + exp_time)) {
			sta_dbg(sta->sdata, "expiring inactive STA %pM\n",
				sta->sta.addr);

			if (ieee80211_vif_is_mesh(&sdata->vif) &&
			    test_sta_flag(sta, WLAN_STA_PS_STA))
				atomic_dec(&sdata->u.mesh.ps.num_sta_ps);

			WARN_ON(__sta_info_destroy(sta));
		}
	}
}

struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
						   const u8 *addr,
						   const u8 *localaddr)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct rhlist_head *tmp;
	struct sta_info *sta;

	/*
	 * Just return a random station if localaddr is NULL
	 * ... first in list.
	 */
	for_each_sta_info(local, addr, sta, tmp) {
		if (localaddr &&
		    !ether_addr_equal(sta->sdata->vif.addr, localaddr))
			continue;
		if (!sta->uploaded)
			return NULL;
		return &sta->sta;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ieee80211_find_sta_by_ifaddr);

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr)
{
	struct sta_info *sta;

	if (!vif)
		return NULL;

	sta = sta_info_get_bss(vif_to_sdata(vif), addr);
	if (!sta)
		return NULL;

	if (!sta->uploaded)
		return NULL;

	return &sta->sta;
}
EXPORT_SYMBOL(ieee80211_find_sta);

/* powersave support code */
void ieee80211_sta_ps_deliver_wakeup(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct sk_buff_head pending;
	int filtered = 0, buffered = 0, ac, i;
	unsigned long flags;
	struct ps_data *ps;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss, struct ieee80211_sub_if_data,
				     u.ap);

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		ps = &sdata->bss->ps;
	else if (ieee80211_vif_is_mesh(&sdata->vif))
		ps = &sdata->u.mesh.ps;
	else
		return;

	clear_sta_flag(sta, WLAN_STA_SP);

	BUILD_BUG_ON(BITS_TO_LONGS(IEEE80211_NUM_TIDS) > 1);
	sta->driver_buffered_tids = 0;
	sta->txq_buffered_tids = 0;

	if (!ieee80211_hw_check(&local->hw, AP_LINK_PS))
		drv_sta_notify(local, sdata, STA_NOTIFY_AWAKE, &sta->sta);

	for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
		if (!sta->sta.txq[i] || !txq_has_queue(sta->sta.txq[i]))
			continue;

		schedule_and_wake_txq(local, to_txq_info(sta->sta.txq[i]));
	}

	skb_queue_head_init(&pending);

	/* sync with ieee80211_tx_h_unicast_ps_buf */
	spin_lock_bh(&sta->ps_lock);
	/* Send all buffered frames to the station */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		int count = skb_queue_len(&pending), tmp;

		spin_lock_irqsave(&sta->tx_filtered[ac].lock, flags);
		skb_queue_splice_tail_init(&sta->tx_filtered[ac], &pending);
		spin_unlock_irqrestore(&sta->tx_filtered[ac].lock, flags);
		tmp = skb_queue_len(&pending);
		filtered += tmp - count;
		count = tmp;

		spin_lock_irqsave(&sta->ps_tx_buf[ac].lock, flags);
		skb_queue_splice_tail_init(&sta->ps_tx_buf[ac], &pending);
		spin_unlock_irqrestore(&sta->ps_tx_buf[ac].lock, flags);
		tmp = skb_queue_len(&pending);
		buffered += tmp - count;
	}

	ieee80211_add_pending_skbs(local, &pending);

	/* now we're no longer in the deliver code */
	clear_sta_flag(sta, WLAN_STA_PS_DELIVER);

	/* The station might have polled and then woken up before we responded,
	 * so clear these flags now to avoid them sticking around.
	 */
	clear_sta_flag(sta, WLAN_STA_PSPOLL);
	clear_sta_flag(sta, WLAN_STA_UAPSD);
	spin_unlock_bh(&sta->ps_lock);

	atomic_dec(&ps->num_sta_ps);

	local->total_ps_buffered -= buffered;

	sta_info_recalc_tim(sta);

	ps_dbg(sdata,
	       "STA %pM aid %d sending %d filtered/%d PS frames since STA woke up\n",
	       sta->sta.addr, sta->sta.aid, filtered, buffered);

	ieee80211_check_fast_xmit(sta);
}

static void ieee80211_send_null_response(struct sta_info *sta, int tid,
					 enum ieee80211_frame_release_type reason,
					 bool call_driver, bool more_data)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_qos_hdr *nullfunc;
	struct sk_buff *skb;
	int size = sizeof(*nullfunc);
	__le16 fc;
	bool qos = sta->sta.wme;
	struct ieee80211_tx_info *info;
	struct ieee80211_chanctx_conf *chanctx_conf;

	if (qos) {
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_QOS_NULLFUNC |
				 IEEE80211_FCTL_FROMDS);
	} else {
		size -= 2;
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_NULLFUNC |
				 IEEE80211_FCTL_FROMDS);
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + size);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = skb_put(skb, size);
	nullfunc->frame_control = fc;
	nullfunc->duration_id = 0;
	memcpy(nullfunc->addr1, sta->sta.addr, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->vif.addr, ETH_ALEN);
	nullfunc->seq_ctrl = 0;

	skb->priority = tid;
	skb_set_queue_mapping(skb, ieee802_1d_to_ac[tid]);
	if (qos) {
		nullfunc->qos_ctrl = cpu_to_le16(tid);

		if (reason == IEEE80211_FRAME_RELEASE_UAPSD) {
			nullfunc->qos_ctrl |=
				cpu_to_le16(IEEE80211_QOS_CTL_EOSP);
			if (more_data)
				nullfunc->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_MOREDATA);
		}
	}

	info = IEEE80211_SKB_CB(skb);

	/*
	 * Tell TX path to send this frame even though the
	 * STA may still remain is PS mode after this frame
	 * exchange. Also set EOSP to indicate this packet
	 * ends the poll/service period.
	 */
	info->flags |= IEEE80211_TX_CTL_NO_PS_BUFFER |
		       IEEE80211_TX_STATUS_EOSP |
		       IEEE80211_TX_CTL_REQ_TX_STATUS;

	info->control.flags |= IEEE80211_TX_CTRL_PS_RESPONSE;

	if (call_driver)
		drv_allow_buffered_frames(local, sta, BIT(tid), 1,
					  reason, false);

	skb->dev = sdata->dev;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.bss_conf.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		kfree_skb(skb);
		return;
	}

	info->band = chanctx_conf->def.chan->band;
	ieee80211_xmit(sdata, sta, skb);
	rcu_read_unlock();
}

static int find_highest_prio_tid(unsigned long tids)
{
	/* lower 3 TIDs aren't ordered perfectly */
	if (tids & 0xF8)
		return fls(tids) - 1;
	/* TID 0 is BE just like TID 3 */
	if (tids & BIT(0))
		return 0;
	return fls(tids) - 1;
}

/* Indicates if the MORE_DATA bit should be set in the last
 * frame obtained by ieee80211_sta_ps_get_frames.
 * Note that driver_release_tids is relevant only if
 * reason = IEEE80211_FRAME_RELEASE_PSPOLL
 */
static bool
ieee80211_sta_ps_more_data(struct sta_info *sta, u8 ignored_acs,
			   enum ieee80211_frame_release_type reason,
			   unsigned long driver_release_tids)
{
	int ac;

	/* If the driver has data on more than one TID then
	 * certainly there's more data if we release just a
	 * single frame now (from a single TID). This will
	 * only happen for PS-Poll.
	 */
	if (reason == IEEE80211_FRAME_RELEASE_PSPOLL &&
	    hweight16(driver_release_tids) > 1)
		return true;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		if (ignored_acs & ieee80211_ac_to_qos_mask[ac])
			continue;

		if (!skb_queue_empty(&sta->tx_filtered[ac]) ||
		    !skb_queue_empty(&sta->ps_tx_buf[ac]))
			return true;
	}

	return false;
}

static void
ieee80211_sta_ps_get_frames(struct sta_info *sta, int n_frames, u8 ignored_acs,
			    enum ieee80211_frame_release_type reason,
			    struct sk_buff_head *frames,
			    unsigned long *driver_release_tids)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	int ac;

	/* Get response frame(s) and more data bit for the last one. */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		unsigned long tids;

		if (ignored_acs & ieee80211_ac_to_qos_mask[ac])
			continue;

		tids = ieee80211_tids_for_ac(ac);

		/* if we already have frames from software, then we can't also
		 * release from hardware queues
		 */
		if (skb_queue_empty(frames)) {
			*driver_release_tids |=
				sta->driver_buffered_tids & tids;
			*driver_release_tids |= sta->txq_buffered_tids & tids;
		}

		if (!*driver_release_tids) {
			struct sk_buff *skb;

			while (n_frames > 0) {
				skb = skb_dequeue(&sta->tx_filtered[ac]);
				if (!skb) {
					skb = skb_dequeue(
						&sta->ps_tx_buf[ac]);
					if (skb)
						local->total_ps_buffered--;
				}
				if (!skb)
					break;
				n_frames--;
				__skb_queue_tail(frames, skb);
			}
		}

		/* If we have more frames buffered on this AC, then abort the
		 * loop since we can't send more data from other ACs before
		 * the buffered frames from this.
		 */
		if (!skb_queue_empty(&sta->tx_filtered[ac]) ||
		    !skb_queue_empty(&sta->ps_tx_buf[ac]))
			break;
	}
}

static void
ieee80211_sta_ps_deliver_response(struct sta_info *sta,
				  int n_frames, u8 ignored_acs,
				  enum ieee80211_frame_release_type reason)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	unsigned long driver_release_tids = 0;
	struct sk_buff_head frames;
	bool more_data;

	/* Service or PS-Poll period starts */
	set_sta_flag(sta, WLAN_STA_SP);

	__skb_queue_head_init(&frames);

	ieee80211_sta_ps_get_frames(sta, n_frames, ignored_acs, reason,
				    &frames, &driver_release_tids);

	more_data = ieee80211_sta_ps_more_data(sta, ignored_acs, reason, driver_release_tids);

	if (driver_release_tids && reason == IEEE80211_FRAME_RELEASE_PSPOLL)
		driver_release_tids =
			BIT(find_highest_prio_tid(driver_release_tids));

	if (skb_queue_empty(&frames) && !driver_release_tids) {
		int tid, ac;

		/*
		 * For PS-Poll, this can only happen due to a race condition
		 * when we set the TIM bit and the station notices it, but
		 * before it can poll for the frame we expire it.
		 *
		 * For uAPSD, this is said in the standard (11.2.1.5 h):
		 *	At each unscheduled SP for a non-AP STA, the AP shall
		 *	attempt to transmit at least one MSDU or MMPDU, but no
		 *	more than the value specified in the Max SP Length field
		 *	in the QoS Capability element from delivery-enabled ACs,
		 *	that are destined for the non-AP STA.
		 *
		 * Since we have no other MSDU/MMPDU, transmit a QoS null frame.
		 */

		/* This will evaluate to 1, 3, 5 or 7. */
		for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ac++)
			if (!(ignored_acs & ieee80211_ac_to_qos_mask[ac]))
				break;
		tid = 7 - 2 * ac;

		ieee80211_send_null_response(sta, tid, reason, true, false);
	} else if (!driver_release_tids) {
		struct sk_buff_head pending;
		struct sk_buff *skb;
		int num = 0;
		u16 tids = 0;
		bool need_null = false;

		skb_queue_head_init(&pending);

		while ((skb = __skb_dequeue(&frames))) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			struct ieee80211_hdr *hdr = (void *) skb->data;
			u8 *qoshdr = NULL;

			num++;

			/*
			 * Tell TX path to send this frame even though the
			 * STA may still remain is PS mode after this frame
			 * exchange.
			 */
			info->flags |= IEEE80211_TX_CTL_NO_PS_BUFFER;
			info->control.flags |= IEEE80211_TX_CTRL_PS_RESPONSE;

			/*
			 * Use MoreData flag to indicate whether there are
			 * more buffered frames for this STA
			 */
			if (more_data || !skb_queue_empty(&frames))
				hdr->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_MOREDATA);
			else
				hdr->frame_control &=
					cpu_to_le16(~IEEE80211_FCTL_MOREDATA);

			if (ieee80211_is_data_qos(hdr->frame_control) ||
			    ieee80211_is_qos_nullfunc(hdr->frame_control))
				qoshdr = ieee80211_get_qos_ctl(hdr);

			tids |= BIT(skb->priority);

			__skb_queue_tail(&pending, skb);

			/* end service period after last frame or add one */
			if (!skb_queue_empty(&frames))
				continue;

			if (reason != IEEE80211_FRAME_RELEASE_UAPSD) {
				/* for PS-Poll, there's only one frame */
				info->flags |= IEEE80211_TX_STATUS_EOSP |
					       IEEE80211_TX_CTL_REQ_TX_STATUS;
				break;
			}

			/* For uAPSD, things are a bit more complicated. If the
			 * last frame has a QoS header (i.e. is a QoS-data or
			 * QoS-nulldata frame) then just set the EOSP bit there
			 * and be done.
			 * If the frame doesn't have a QoS header (which means
			 * it should be a bufferable MMPDU) then we can't set
			 * the EOSP bit in the QoS header; add a QoS-nulldata
			 * frame to the list to send it after the MMPDU.
			 *
			 * Note that this code is only in the mac80211-release
			 * code path, we assume that the driver will not buffer
			 * anything but QoS-data frames, or if it does, will
			 * create the QoS-nulldata frame by itself if needed.
			 *
			 * Cf. 802.11-2012 10.2.1.10 (c).
			 */
			if (qoshdr) {
				*qoshdr |= IEEE80211_QOS_CTL_EOSP;

				info->flags |= IEEE80211_TX_STATUS_EOSP |
					       IEEE80211_TX_CTL_REQ_TX_STATUS;
			} else {
				/* The standard isn't completely clear on this
				 * as it says the more-data bit should be set
				 * if there are more BUs. The QoS-Null frame
				 * we're about to send isn't buffered yet, we
				 * only create it below, but let's pretend it
				 * was buffered just in case some clients only
				 * expect more-data=0 when eosp=1.
				 */
				hdr->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_MOREDATA);
				need_null = true;
				num++;
			}
			break;
		}

		drv_allow_buffered_frames(local, sta, tids, num,
					  reason, more_data);

		ieee80211_add_pending_skbs(local, &pending);

		if (need_null)
			ieee80211_send_null_response(
				sta, find_highest_prio_tid(tids),
				reason, false, false);

		sta_info_recalc_tim(sta);
	} else {
		int tid;

		/*
		 * We need to release a frame that is buffered somewhere in the
		 * driver ... it'll have to handle that.
		 * Note that the driver also has to check the number of frames
		 * on the TIDs we're releasing from - if there are more than
		 * n_frames it has to set the more-data bit (if we didn't ask
		 * it to set it anyway due to other buffered frames); if there
		 * are fewer than n_frames it has to make sure to adjust that
		 * to allow the service period to end properly.
		 */
		drv_release_buffered_frames(local, sta, driver_release_tids,
					    n_frames, reason, more_data);

		/*
		 * Note that we don't recalculate the TIM bit here as it would
		 * most likely have no effect at all unless the driver told us
		 * that the TID(s) became empty before returning here from the
		 * release function.
		 * Either way, however, when the driver tells us that the TID(s)
		 * became empty or we find that a txq became empty, we'll do the
		 * TIM recalculation.
		 */

		for (tid = 0; tid < ARRAY_SIZE(sta->sta.txq); tid++) {
			if (!sta->sta.txq[tid] ||
			    !(driver_release_tids & BIT(tid)) ||
			    txq_has_queue(sta->sta.txq[tid]))
				continue;

			sta_info_recalc_tim(sta);
			break;
		}
	}
}

void ieee80211_sta_ps_deliver_poll_response(struct sta_info *sta)
{
	u8 ignore_for_response = sta->sta.uapsd_queues;

	/*
	 * If all ACs are delivery-enabled then we should reply
	 * from any of them, if only some are enabled we reply
	 * only from the non-enabled ones.
	 */
	if (ignore_for_response == BIT(IEEE80211_NUM_ACS) - 1)
		ignore_for_response = 0;

	ieee80211_sta_ps_deliver_response(sta, 1, ignore_for_response,
					  IEEE80211_FRAME_RELEASE_PSPOLL);
}

void ieee80211_sta_ps_deliver_uapsd(struct sta_info *sta)
{
	int n_frames = sta->sta.max_sp;
	u8 delivery_enabled = sta->sta.uapsd_queues;

	/*
	 * If we ever grow support for TSPEC this might happen if
	 * the TSPEC update from hostapd comes in between a trigger
	 * frame setting WLAN_STA_UAPSD in the RX path and this
	 * actually getting called.
	 */
	if (!delivery_enabled)
		return;

	switch (sta->sta.max_sp) {
	case 1:
		n_frames = 2;
		break;
	case 2:
		n_frames = 4;
		break;
	case 3:
		n_frames = 6;
		break;
	case 0:
		/* XXX: what is a good value? */
		n_frames = 128;
		break;
	}

	ieee80211_sta_ps_deliver_response(sta, n_frames, ~delivery_enabled,
					  IEEE80211_FRAME_RELEASE_UAPSD);
}

void ieee80211_sta_block_awake(struct ieee80211_hw *hw,
			       struct ieee80211_sta *pubsta, bool block)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	trace_api_sta_block_awake(sta->local, pubsta, block);

	if (block) {
		set_sta_flag(sta, WLAN_STA_PS_DRIVER);
		ieee80211_clear_fast_xmit(sta);
		return;
	}

	if (!test_sta_flag(sta, WLAN_STA_PS_DRIVER))
		return;

	if (!test_sta_flag(sta, WLAN_STA_PS_STA)) {
		set_sta_flag(sta, WLAN_STA_PS_DELIVER);
		clear_sta_flag(sta, WLAN_STA_PS_DRIVER);
		ieee80211_queue_work(hw, &sta->drv_deliver_wk);
	} else if (test_sta_flag(sta, WLAN_STA_PSPOLL) ||
		   test_sta_flag(sta, WLAN_STA_UAPSD)) {
		/* must be asleep in this case */
		clear_sta_flag(sta, WLAN_STA_PS_DRIVER);
		ieee80211_queue_work(hw, &sta->drv_deliver_wk);
	} else {
		clear_sta_flag(sta, WLAN_STA_PS_DRIVER);
		ieee80211_check_fast_xmit(sta);
	}
}
EXPORT_SYMBOL(ieee80211_sta_block_awake);

void ieee80211_sta_eosp(struct ieee80211_sta *pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_local *local = sta->local;

	trace_api_eosp(local, pubsta);

	clear_sta_flag(sta, WLAN_STA_SP);
}
EXPORT_SYMBOL(ieee80211_sta_eosp);

void ieee80211_send_eosp_nullfunc(struct ieee80211_sta *pubsta, int tid)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	enum ieee80211_frame_release_type reason;
	bool more_data;

	trace_api_send_eosp_nullfunc(sta->local, pubsta, tid);

	reason = IEEE80211_FRAME_RELEASE_UAPSD;
	more_data = ieee80211_sta_ps_more_data(sta, ~sta->sta.uapsd_queues,
					       reason, 0);

	ieee80211_send_null_response(sta, tid, reason, false, more_data);
}
EXPORT_SYMBOL(ieee80211_send_eosp_nullfunc);

void ieee80211_sta_set_buffered(struct ieee80211_sta *pubsta,
				u8 tid, bool buffered)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	if (WARN_ON(tid >= IEEE80211_NUM_TIDS))
		return;

	trace_api_sta_set_buffered(sta->local, pubsta, tid, buffered);

	if (buffered)
		set_bit(tid, &sta->driver_buffered_tids);
	else
		clear_bit(tid, &sta->driver_buffered_tids);

	sta_info_recalc_tim(sta);
}
EXPORT_SYMBOL(ieee80211_sta_set_buffered);

void ieee80211_sta_register_airtime(struct ieee80211_sta *pubsta, u8 tid,
				    u32 tx_airtime, u32 rx_airtime)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_local *local = sta->sdata->local;
	u8 ac = ieee80211_ac_from_tid(tid);
	u32 airtime = 0;

	if (sta->local->airtime_flags & AIRTIME_USE_TX)
		airtime += tx_airtime;
	if (sta->local->airtime_flags & AIRTIME_USE_RX)
		airtime += rx_airtime;

	spin_lock_bh(&local->active_txq_lock[ac]);
	sta->airtime[ac].tx_airtime += tx_airtime;
	sta->airtime[ac].rx_airtime += rx_airtime;

	if (ieee80211_sta_keep_active(sta, ac))
		sta->airtime[ac].deficit -= airtime;

	spin_unlock_bh(&local->active_txq_lock[ac]);
}
EXPORT_SYMBOL(ieee80211_sta_register_airtime);

void __ieee80211_sta_recalc_aggregates(struct sta_info *sta, u16 active_links)
{
	bool first = true;
	int link_id;

	if (!sta->sta.valid_links || !sta->sta.mlo) {
		sta->sta.cur = &sta->sta.deflink.agg;
		return;
	}

	rcu_read_lock();
	for (link_id = 0; link_id < ARRAY_SIZE((sta)->link); link_id++) {
		struct ieee80211_link_sta *link_sta;
		int i;

		if (!(active_links & BIT(link_id)))
			continue;

		link_sta = rcu_dereference(sta->sta.link[link_id]);
		if (!link_sta)
			continue;

		if (first) {
			sta->cur = sta->sta.deflink.agg;
			first = false;
			continue;
		}

		sta->cur.max_amsdu_len =
			min(sta->cur.max_amsdu_len,
			    link_sta->agg.max_amsdu_len);
		sta->cur.max_rc_amsdu_len =
			min(sta->cur.max_rc_amsdu_len,
			    link_sta->agg.max_rc_amsdu_len);

		for (i = 0; i < ARRAY_SIZE(sta->cur.max_tid_amsdu_len); i++)
			sta->cur.max_tid_amsdu_len[i] =
				min(sta->cur.max_tid_amsdu_len[i],
				    link_sta->agg.max_tid_amsdu_len[i]);
	}
	rcu_read_unlock();

	sta->sta.cur = &sta->cur;
}

void ieee80211_sta_recalc_aggregates(struct ieee80211_sta *pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	__ieee80211_sta_recalc_aggregates(sta, sta->sdata->vif.active_links);
}
EXPORT_SYMBOL(ieee80211_sta_recalc_aggregates);

void ieee80211_sta_update_pending_airtime(struct ieee80211_local *local,
					  struct sta_info *sta, u8 ac,
					  u16 tx_airtime, bool tx_completed)
{
	int tx_pending;

	if (!wiphy_ext_feature_isset(local->hw.wiphy, NL80211_EXT_FEATURE_AQL))
		return;

	if (!tx_completed) {
		if (sta)
			atomic_add(tx_airtime,
				   &sta->airtime[ac].aql_tx_pending);

		atomic_add(tx_airtime, &local->aql_total_pending_airtime);
		atomic_add(tx_airtime, &local->aql_ac_pending_airtime[ac]);
		return;
	}

	if (sta) {
		tx_pending = atomic_sub_return(tx_airtime,
					       &sta->airtime[ac].aql_tx_pending);
		if (tx_pending < 0)
			atomic_cmpxchg(&sta->airtime[ac].aql_tx_pending,
				       tx_pending, 0);
	}

	atomic_sub(tx_airtime, &local->aql_total_pending_airtime);
	tx_pending = atomic_sub_return(tx_airtime,
				       &local->aql_ac_pending_airtime[ac]);
	if (WARN_ONCE(tx_pending < 0,
		      "Device %s AC %d pending airtime underflow: %u, %u",
		      wiphy_name(local->hw.wiphy), ac, tx_pending,
		      tx_airtime)) {
		atomic_cmpxchg(&local->aql_ac_pending_airtime[ac],
			       tx_pending, 0);
		atomic_sub(tx_pending, &local->aql_total_pending_airtime);
	}
}

static struct ieee80211_sta_rx_stats *
sta_get_last_rx_stats(struct sta_info *sta)
{
	struct ieee80211_sta_rx_stats *stats = &sta->deflink.rx_stats;
	int cpu;

	if (!sta->deflink.pcpu_rx_stats)
		return stats;

	for_each_possible_cpu(cpu) {
		struct ieee80211_sta_rx_stats *cpustats;

		cpustats = per_cpu_ptr(sta->deflink.pcpu_rx_stats, cpu);

		if (time_after(cpustats->last_rx, stats->last_rx))
			stats = cpustats;
	}

	return stats;
}

static void sta_stats_decode_rate(struct ieee80211_local *local, u32 rate,
				  struct rate_info *rinfo)
{
	rinfo->bw = STA_STATS_GET(BW, rate);

	switch (STA_STATS_GET(TYPE, rate)) {
	case STA_STATS_RATE_TYPE_VHT:
		rinfo->flags = RATE_INFO_FLAGS_VHT_MCS;
		rinfo->mcs = STA_STATS_GET(VHT_MCS, rate);
		rinfo->nss = STA_STATS_GET(VHT_NSS, rate);
		if (STA_STATS_GET(SGI, rate))
			rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case STA_STATS_RATE_TYPE_HT:
		rinfo->flags = RATE_INFO_FLAGS_MCS;
		rinfo->mcs = STA_STATS_GET(HT_MCS, rate);
		if (STA_STATS_GET(SGI, rate))
			rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case STA_STATS_RATE_TYPE_LEGACY: {
		struct ieee80211_supported_band *sband;
		u16 brate;
		unsigned int shift;
		int band = STA_STATS_GET(LEGACY_BAND, rate);
		int rate_idx = STA_STATS_GET(LEGACY_IDX, rate);

		sband = local->hw.wiphy->bands[band];

		if (WARN_ON_ONCE(!sband->bitrates))
			break;

		brate = sband->bitrates[rate_idx].bitrate;
		if (rinfo->bw == RATE_INFO_BW_5)
			shift = 2;
		else if (rinfo->bw == RATE_INFO_BW_10)
			shift = 1;
		else
			shift = 0;
		rinfo->legacy = DIV_ROUND_UP(brate, 1 << shift);
		break;
		}
	case STA_STATS_RATE_TYPE_HE:
		rinfo->flags = RATE_INFO_FLAGS_HE_MCS;
		rinfo->mcs = STA_STATS_GET(HE_MCS, rate);
		rinfo->nss = STA_STATS_GET(HE_NSS, rate);
		rinfo->he_gi = STA_STATS_GET(HE_GI, rate);
		rinfo->he_ru_alloc = STA_STATS_GET(HE_RU, rate);
		rinfo->he_dcm = STA_STATS_GET(HE_DCM, rate);
		break;
	case STA_STATS_RATE_TYPE_EHT:
		rinfo->flags = RATE_INFO_FLAGS_EHT_MCS;
		rinfo->mcs = STA_STATS_GET(EHT_MCS, rate);
		rinfo->nss = STA_STATS_GET(EHT_NSS, rate);
		rinfo->eht_gi = STA_STATS_GET(EHT_GI, rate);
		rinfo->eht_ru_alloc = STA_STATS_GET(EHT_RU, rate);
		break;
	}
}

static int sta_set_rate_info_rx(struct sta_info *sta, struct rate_info *rinfo)
{
	u32 rate = READ_ONCE(sta_get_last_rx_stats(sta)->last_rate);

	if (rate == STA_STATS_RATE_INVALID)
		return -EINVAL;

	sta_stats_decode_rate(sta->local, rate, rinfo);
	return 0;
}

static inline u64 sta_get_tidstats_msdu(struct ieee80211_sta_rx_stats *rxstats,
					int tid)
{
	unsigned int start;
	u64 value;

	do {
		start = u64_stats_fetch_begin(&rxstats->syncp);
		value = rxstats->msdu[tid];
	} while (u64_stats_fetch_retry(&rxstats->syncp, start));

	return value;
}

static void sta_set_tidstats(struct sta_info *sta,
			     struct cfg80211_tid_stats *tidstats,
			     int tid)
{
	struct ieee80211_local *local = sta->local;
	int cpu;

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_RX_MSDU))) {
		tidstats->rx_msdu += sta_get_tidstats_msdu(&sta->deflink.rx_stats,
							   tid);

		if (sta->deflink.pcpu_rx_stats) {
			for_each_possible_cpu(cpu) {
				struct ieee80211_sta_rx_stats *cpurxs;

				cpurxs = per_cpu_ptr(sta->deflink.pcpu_rx_stats,
						     cpu);
				tidstats->rx_msdu +=
					sta_get_tidstats_msdu(cpurxs, tid);
			}
		}

		tidstats->filled |= BIT(NL80211_TID_STATS_RX_MSDU);
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU))) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU);
		tidstats->tx_msdu = sta->deflink.tx_stats.msdu[tid];
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU_RETRIES)) &&
	    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU_RETRIES);
		tidstats->tx_msdu_retries = sta->deflink.status_stats.msdu_retries[tid];
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU_FAILED)) &&
	    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU_FAILED);
		tidstats->tx_msdu_failed = sta->deflink.status_stats.msdu_failed[tid];
	}

	if (tid < IEEE80211_NUM_TIDS) {
		spin_lock_bh(&local->fq.lock);
		rcu_read_lock();

		tidstats->filled |= BIT(NL80211_TID_STATS_TXQ_STATS);
		ieee80211_fill_txq_stats(&tidstats->txq_stats,
					 to_txq_info(sta->sta.txq[tid]));

		rcu_read_unlock();
		spin_unlock_bh(&local->fq.lock);
	}
}

static inline u64 sta_get_stats_bytes(struct ieee80211_sta_rx_stats *rxstats)
{
	unsigned int start;
	u64 value;

	do {
		start = u64_stats_fetch_begin(&rxstats->syncp);
		value = rxstats->bytes;
	} while (u64_stats_fetch_retry(&rxstats->syncp, start));

	return value;
}

void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo,
		   bool tidstats)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	u32 thr = 0;
	int i, ac, cpu;
	struct ieee80211_sta_rx_stats *last_rxstats;

	last_rxstats = sta_get_last_rx_stats(sta);

	sinfo->generation = sdata->local->sta_generation;

	/* do before driver, so beacon filtering drivers have a
	 * chance to e.g. just add the number of filtered beacons
	 * (or just modify the value entirely, of course)
	 */
	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		sinfo->rx_beacon = sdata->deflink.u.mgd.count_beacon_signal;

	drv_sta_statistics(local, sdata, &sta->sta, sinfo);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME) |
			 BIT_ULL(NL80211_STA_INFO_STA_FLAGS) |
			 BIT_ULL(NL80211_STA_INFO_BSS_PARAM) |
			 BIT_ULL(NL80211_STA_INFO_CONNECTED_TIME) |
			 BIT_ULL(NL80211_STA_INFO_ASSOC_AT_BOOTTIME) |
			 BIT_ULL(NL80211_STA_INFO_RX_DROP_MISC);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		sinfo->beacon_loss_count =
			sdata->deflink.u.mgd.beacon_loss_count;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BEACON_LOSS);
	}

	sinfo->connected_time = ktime_get_seconds() - sta->last_connected;
	sinfo->assoc_at = sta->assoc_at;
	sinfo->inactive_time =
		jiffies_to_msecs(jiffies - ieee80211_sta_last_active(sta));

	if (!(sinfo->filled & (BIT_ULL(NL80211_STA_INFO_TX_BYTES64) |
			       BIT_ULL(NL80211_STA_INFO_TX_BYTES)))) {
		sinfo->tx_bytes = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->tx_bytes += sta->deflink.tx_stats.bytes[ac];
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_PACKETS))) {
		sinfo->tx_packets = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->tx_packets += sta->deflink.tx_stats.packets[ac];
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
	}

	if (!(sinfo->filled & (BIT_ULL(NL80211_STA_INFO_RX_BYTES64) |
			       BIT_ULL(NL80211_STA_INFO_RX_BYTES)))) {
		sinfo->rx_bytes += sta_get_stats_bytes(&sta->deflink.rx_stats);

		if (sta->deflink.pcpu_rx_stats) {
			for_each_possible_cpu(cpu) {
				struct ieee80211_sta_rx_stats *cpurxs;

				cpurxs = per_cpu_ptr(sta->deflink.pcpu_rx_stats,
						     cpu);
				sinfo->rx_bytes += sta_get_stats_bytes(cpurxs);
			}
		}

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_RX_PACKETS))) {
		sinfo->rx_packets = sta->deflink.rx_stats.packets;
		if (sta->deflink.pcpu_rx_stats) {
			for_each_possible_cpu(cpu) {
				struct ieee80211_sta_rx_stats *cpurxs;

				cpurxs = per_cpu_ptr(sta->deflink.pcpu_rx_stats,
						     cpu);
				sinfo->rx_packets += cpurxs->packets;
			}
		}
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_RETRIES))) {
		sinfo->tx_retries = sta->deflink.status_stats.retry_count;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_FAILED))) {
		sinfo->tx_failed = sta->deflink.status_stats.retry_failed;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_RX_DURATION))) {
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->rx_duration += sta->airtime[ac].rx_airtime;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_DURATION);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_DURATION))) {
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->tx_duration += sta->airtime[ac].tx_airtime;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_DURATION);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_AIRTIME_WEIGHT))) {
		sinfo->airtime_weight = sta->airtime_weight;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_AIRTIME_WEIGHT);
	}

	sinfo->rx_dropped_misc = sta->deflink.rx_stats.dropped;
	if (sta->deflink.pcpu_rx_stats) {
		for_each_possible_cpu(cpu) {
			struct ieee80211_sta_rx_stats *cpurxs;

			cpurxs = per_cpu_ptr(sta->deflink.pcpu_rx_stats, cpu);
			sinfo->rx_dropped_misc += cpurxs->dropped;
		}
	}

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BEACON_RX) |
				 BIT_ULL(NL80211_STA_INFO_BEACON_SIGNAL_AVG);
		sinfo->rx_beacon_signal_avg = ieee80211_ave_rssi(&sdata->vif);
	}

	if (ieee80211_hw_check(&sta->local->hw, SIGNAL_DBM) ||
	    ieee80211_hw_check(&sta->local->hw, SIGNAL_UNSPEC)) {
		if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_SIGNAL))) {
			sinfo->signal = (s8)last_rxstats->last_signal;
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
		}

		if (!sta->deflink.pcpu_rx_stats &&
		    !(sinfo->filled & BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG))) {
			sinfo->signal_avg =
				-ewma_signal_read(&sta->deflink.rx_stats_avg.signal);
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);
		}
	}

	/* for the average - if pcpu_rx_stats isn't set - rxstats must point to
	 * the sta->rx_stats struct, so the check here is fine with and without
	 * pcpu statistics
	 */
	if (last_rxstats->chains &&
	    !(sinfo->filled & (BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL) |
			       BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL_AVG)))) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL);
		if (!sta->deflink.pcpu_rx_stats)
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL_AVG);

		sinfo->chains = last_rxstats->chains;

		for (i = 0; i < ARRAY_SIZE(sinfo->chain_signal); i++) {
			sinfo->chain_signal[i] =
				last_rxstats->chain_signal_last[i];
			sinfo->chain_signal_avg[i] =
				-ewma_signal_read(&sta->deflink.rx_stats_avg.chain_signal[i]);
		}
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_BITRATE)) &&
	    !sta->sta.valid_links &&
	    ieee80211_rate_valid(&sta->deflink.tx_stats.last_rate)) {
		sta_set_rate_info_tx(sta, &sta->deflink.tx_stats.last_rate,
				     &sinfo->txrate);
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_RX_BITRATE)) &&
	    !sta->sta.valid_links) {
		if (sta_set_rate_info_rx(sta, &sinfo->rxrate) == 0)
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);
	}

	if (tidstats && !cfg80211_sinfo_alloc_tid_stats(sinfo, GFP_KERNEL)) {
		for (i = 0; i < IEEE80211_NUM_TIDS + 1; i++)
			sta_set_tidstats(sta, &sinfo->pertid[i], i);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
#ifdef CONFIG_MAC80211_MESH
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_LLID) |
				 BIT_ULL(NL80211_STA_INFO_PLID) |
				 BIT_ULL(NL80211_STA_INFO_PLINK_STATE) |
				 BIT_ULL(NL80211_STA_INFO_LOCAL_PM) |
				 BIT_ULL(NL80211_STA_INFO_PEER_PM) |
				 BIT_ULL(NL80211_STA_INFO_NONPEER_PM) |
				 BIT_ULL(NL80211_STA_INFO_CONNECTED_TO_GATE) |
				 BIT_ULL(NL80211_STA_INFO_CONNECTED_TO_AS);

		sinfo->llid = sta->mesh->llid;
		sinfo->plid = sta->mesh->plid;
		sinfo->plink_state = sta->mesh->plink_state;
		if (test_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN)) {
			sinfo->filled |= BIT_ULL(NL80211_STA_INFO_T_OFFSET);
			sinfo->t_offset = sta->mesh->t_offset;
		}
		sinfo->local_pm = sta->mesh->local_pm;
		sinfo->peer_pm = sta->mesh->peer_pm;
		sinfo->nonpeer_pm = sta->mesh->nonpeer_pm;
		sinfo->connected_to_gate = sta->mesh->connected_to_gate;
		sinfo->connected_to_as = sta->mesh->connected_to_as;
#endif
	}

	sinfo->bss_param.flags = 0;
	if (sdata->vif.bss_conf.use_cts_prot)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_CTS_PROT;
	if (sdata->vif.bss_conf.use_short_preamble)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_PREAMBLE;
	if (sdata->vif.bss_conf.use_short_slot)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_SLOT_TIME;
	sinfo->bss_param.dtim_period = sdata->vif.bss_conf.dtim_period;
	sinfo->bss_param.beacon_interval = sdata->vif.bss_conf.beacon_int;

	sinfo->sta_flags.set = 0;
	sinfo->sta_flags.mask = BIT(NL80211_STA_FLAG_AUTHORIZED) |
				BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) |
				BIT(NL80211_STA_FLAG_WME) |
				BIT(NL80211_STA_FLAG_MFP) |
				BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				BIT(NL80211_STA_FLAG_ASSOCIATED) |
				BIT(NL80211_STA_FLAG_TDLS_PEER);
	if (test_sta_flag(sta, WLAN_STA_AUTHORIZED))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_AUTHORIZED);
	if (test_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_SHORT_PREAMBLE);
	if (sta->sta.wme)
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_WME);
	if (test_sta_flag(sta, WLAN_STA_MFP))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_MFP);
	if (test_sta_flag(sta, WLAN_STA_AUTH))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_AUTHENTICATED);
	if (test_sta_flag(sta, WLAN_STA_ASSOC))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_ASSOCIATED);
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_TDLS_PEER);

	thr = sta_get_expected_throughput(sta);

	if (thr != 0) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_EXPECTED_THROUGHPUT);
		sinfo->expected_throughput = thr;
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL)) &&
	    sta->deflink.status_stats.ack_signal_filled) {
		sinfo->ack_signal = sta->deflink.status_stats.last_ack_signal;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);
	}

	if (!(sinfo->filled & BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG)) &&
	    sta->deflink.status_stats.ack_signal_filled) {
		sinfo->avg_ack_signal =
			-(s8)ewma_avg_signal_read(
				&sta->deflink.status_stats.avg_ack_signal);
		sinfo->filled |=
			BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_AIRTIME_LINK_METRIC);
		sinfo->airtime_link_metric =
			airtime_link_metric_get(local, sta);
	}
}

u32 sta_get_expected_throughput(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct rate_control_ref *ref = NULL;
	u32 thr = 0;

	if (test_sta_flag(sta, WLAN_STA_RATE_CONTROL))
		ref = local->rate_ctrl;

	/* check if the driver has a SW RC implementation */
	if (ref && ref->ops->get_expected_throughput)
		thr = ref->ops->get_expected_throughput(sta->rate_ctrl_priv);
	else
		thr = drv_get_expected_throughput(local, sta);

	return thr;
}

unsigned long ieee80211_sta_last_active(struct sta_info *sta)
{
	struct ieee80211_sta_rx_stats *stats = sta_get_last_rx_stats(sta);

	if (!sta->deflink.status_stats.last_ack ||
	    time_after(stats->last_rx, sta->deflink.status_stats.last_ack))
		return stats->last_rx;
	return sta->deflink.status_stats.last_ack;
}

static void sta_update_codel_params(struct sta_info *sta, u32 thr)
{
	if (thr && thr < STA_SLOW_THRESHOLD * sta->local->num_sta) {
		sta->cparams.target = MS2TIME(50);
		sta->cparams.interval = MS2TIME(300);
		sta->cparams.ecn = false;
	} else {
		sta->cparams.target = MS2TIME(20);
		sta->cparams.interval = MS2TIME(100);
		sta->cparams.ecn = true;
	}
}

void ieee80211_sta_set_expected_throughput(struct ieee80211_sta *pubsta,
					   u32 thr)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	sta_update_codel_params(sta, thr);
}

int ieee80211_sta_allocate_link(struct sta_info *sta, unsigned int link_id)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct sta_link_alloc *alloc;
	int ret;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	WARN_ON(!test_sta_flag(sta, WLAN_STA_INSERTED));

	/* must represent an MLD from the start */
	if (WARN_ON(!sta->sta.valid_links))
		return -EINVAL;

	if (WARN_ON(sta->sta.valid_links & BIT(link_id) ||
		    sta->link[link_id]))
		return -EBUSY;

	alloc = kzalloc(sizeof(*alloc), GFP_KERNEL);
	if (!alloc)
		return -ENOMEM;

	ret = sta_info_alloc_link(sdata->local, &alloc->info, GFP_KERNEL);
	if (ret) {
		kfree(alloc);
		return ret;
	}

	sta_info_add_link(sta, link_id, &alloc->info, &alloc->sta);

	ieee80211_link_sta_debugfs_add(&alloc->info);

	return 0;
}

void ieee80211_sta_free_link(struct sta_info *sta, unsigned int link_id)
{
	lockdep_assert_wiphy(sta->sdata->local->hw.wiphy);

	WARN_ON(!test_sta_flag(sta, WLAN_STA_INSERTED));

	sta_remove_link(sta, link_id, false);
}

int ieee80211_sta_activate_link(struct sta_info *sta, unsigned int link_id)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct link_sta_info *link_sta;
	u16 old_links = sta->sta.valid_links;
	u16 new_links = old_links | BIT(link_id);
	int ret;

	link_sta = rcu_dereference_protected(sta->link[link_id],
					     lockdep_is_held(&sdata->local->hw.wiphy->mtx));

	if (WARN_ON(old_links == new_links || !link_sta))
		return -EINVAL;

	rcu_read_lock();
	if (link_sta_info_hash_lookup(sdata->local, link_sta->addr)) {
		rcu_read_unlock();
		return -EALREADY;
	}
	/* we only modify under the mutex so this is fine */
	rcu_read_unlock();

	sta->sta.valid_links = new_links;

	if (WARN_ON(!test_sta_flag(sta, WLAN_STA_INSERTED)))
		goto hash;

	ieee80211_recalc_min_chandef(sdata, link_id);

	/* Ensure the values are updated for the driver,
	 * redone by sta_remove_link on failure.
	 */
	ieee80211_sta_recalc_aggregates(&sta->sta);

	ret = drv_change_sta_links(sdata->local, sdata, &sta->sta,
				   old_links, new_links);
	if (ret) {
		sta->sta.valid_links = old_links;
		sta_remove_link(sta, link_id, false);
		return ret;
	}

hash:
	ret = link_sta_info_hash_add(sdata->local, link_sta);
	WARN_ON(ret);
	return 0;
}

void ieee80211_sta_remove_link(struct sta_info *sta, unsigned int link_id)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u16 old_links = sta->sta.valid_links;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	sta->sta.valid_links &= ~BIT(link_id);

	if (!WARN_ON(!test_sta_flag(sta, WLAN_STA_INSERTED)))
		drv_change_sta_links(sdata->local, sdata, &sta->sta,
				     old_links, sta->sta.valid_links);

	sta_remove_link(sta, link_id, true);
}

void ieee80211_sta_set_max_amsdu_subframes(struct sta_info *sta,
					   const u8 *ext_capab,
					   unsigned int ext_capab_len)
{
	u8 val;

	sta->sta.max_amsdu_subframes = 0;

	if (ext_capab_len < 8)
		return;

	/* The sender might not have sent the last bit, consider it to be 0 */
	val = u8_get_bits(ext_capab[7], WLAN_EXT_CAPA8_MAX_MSDU_IN_AMSDU_LSB);

	/* we did get all the bits, take the MSB as well */
	if (ext_capab_len >= 9)
		val |= u8_get_bits(ext_capab[8],
				   WLAN_EXT_CAPA9_MAX_MSDU_IN_AMSDU_MSB) << 1;

	if (val)
		sta->sta.max_amsdu_subframes = 4 << (4 - val);
}

#ifdef CONFIG_LOCKDEP
bool lockdep_sta_mutex_held(struct ieee80211_sta *pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	return lockdep_is_held(&sta->local->hw.wiphy->mtx);
}
EXPORT_SYMBOL(lockdep_sta_mutex_held);
#endif
