/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015 - 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 * the caller may not do much with the STA info before inserting it, in
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
 * There is no concept of ownership on a STA entry, each structure is
 * owned by the global hash table/list until it is removed. All users of
 * the structure need to be RCU protected so that the structure won't be
 * freed before they are done using it.
 */

static const struct rhashtable_params sta_rht_params = {
	.nelem_hint = 3, /* start small */
	.automatic_shrinking = true,
	.head_offset = offsetof(struct sta_info, hash_node),
	.key_offset = offsetof(struct sta_info, addr),
	.key_len = ETH_ALEN,
	.max_size = CONFIG_MAC80211_STA_HASH_MAX_SIZE,
};

/* Caller must hold local->sta_mtx */
static int sta_info_hash_del(struct ieee80211_local *local,
			     struct sta_info *sta)
{
	return rhltable_remove(&local->sta_hash, &sta->hash_node,
			       sta_rht_params);
}

static void __cleanup_single_sta(struct sta_info *sta)
{
	int ac, i;
	struct tid_ampdu_tx *tid_tx;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct fq *fq = &local->fq;
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

	if (sta->sta.txq[0]) {
		for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
			struct txq_info *txqi = to_txq_info(sta->sta.txq[i]);

			spin_lock_bh(&fq->lock);
			ieee80211_txq_purge(local, txqi);
			spin_unlock_bh(&fq->lock);
		}
	}

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

struct sta_info *sta_info_get_by_idx(struct ieee80211_sub_if_data *sdata,
				     int idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int i = 0;

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
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
	if (sta->rate_ctrl)
		rate_control_free_sta(sta);

	sta_dbg(sta->sdata, "Destroyed STA %pM\n", sta->sta.addr);

	if (sta->sta.txq[0])
		kfree(to_txq_info(sta->sta.txq[0]));
	kfree(rcu_dereference_raw(sta->sta.rates));
#ifdef CONFIG_MAC80211_MESH
	kfree(sta->mesh);
#endif
	free_percpu(sta->pcpu_rx_stats);
	kfree(sta);
}

/* Caller must hold local->sta_mtx */
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

struct sta_info *sta_info_alloc(struct ieee80211_sub_if_data *sdata,
				const u8 *addr, gfp_t gfp)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	int i;

	sta = kzalloc(sizeof(*sta) + hw->sta_data_size, gfp);
	if (!sta)
		return NULL;

	if (ieee80211_hw_check(hw, USES_RSS)) {
		sta->pcpu_rx_stats =
			alloc_percpu(struct ieee80211_sta_rx_stats);
		if (!sta->pcpu_rx_stats)
			goto free;
	}

	spin_lock_init(&sta->lock);
	spin_lock_init(&sta->ps_lock);
	INIT_WORK(&sta->drv_deliver_wk, sta_deliver_ps_frames);
	INIT_WORK(&sta->ampdu_mlme.work, ieee80211_ba_session_work);
	mutex_init(&sta->ampdu_mlme.mtx);
#ifdef CONFIG_MAC80211_MESH
	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		sta->mesh = kzalloc(sizeof(*sta->mesh), gfp);
		if (!sta->mesh)
			goto free;
		sta->mesh->plink_sta = sta;
		spin_lock_init(&sta->mesh->plink_lock);
		if (ieee80211_vif_is_mesh(&sdata->vif) &&
		    !sdata->u.mesh.user_mpm)
			timer_setup(&sta->mesh->plink_timer, mesh_plink_timer,
				    0);
		sta->mesh->nonpeer_pm = NL80211_MESH_POWER_ACTIVE;
	}
#endif

	memcpy(sta->addr, addr, ETH_ALEN);
	memcpy(sta->sta.addr, addr, ETH_ALEN);
	sta->sta.max_rx_aggregation_subframes =
		local->hw.max_rx_aggregation_subframes;

	sta->local = local;
	sta->sdata = sdata;
	sta->rx_stats.last_rx = jiffies;

	u64_stats_init(&sta->rx_stats.syncp);

	sta->sta_state = IEEE80211_STA_NONE;

	/* Mark TID as unreserved */
	sta->reserved_tid = IEEE80211_TID_UNRESERVED;

	sta->last_connected = ktime_get_seconds();
	ewma_signal_init(&sta->rx_stats_avg.signal);
	for (i = 0; i < ARRAY_SIZE(sta->rx_stats_avg.chain_signal); i++)
		ewma_signal_init(&sta->rx_stats_avg.chain_signal[i]);

	if (local->ops->wake_tx_queue) {
		void *txq_data;
		int size = sizeof(struct txq_info) +
			   ALIGN(hw->txq_data_size, sizeof(void *));

		txq_data = kcalloc(ARRAY_SIZE(sta->sta.txq), size, gfp);
		if (!txq_data)
			goto free;

		for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
			struct txq_info *txq = txq_data + i * size;

			ieee80211_txq_init(sdata, sta, txq, i);
		}
	}

	if (sta_prepare_rate_control(local, sta, gfp))
		goto free_txq;

	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		/*
		 * timer_to_tid must be initialized with identity mapping
		 * to enable session_timer's data differentiation. See
		 * sta_rx_agg_session_timer_expired for usage.
		 */
		sta->timer_to_tid[i] = i;
	}
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		skb_queue_head_init(&sta->ps_tx_buf[i]);
		skb_queue_head_init(&sta->tx_filtered[i]);
	}

	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		sta->last_seq_ctrl[i] = cpu_to_le16(USHRT_MAX);

	sta->sta.smps_mode = IEEE80211_SMPS_OFF;
	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		struct ieee80211_supported_band *sband;
		u8 smps;

		sband = ieee80211_get_sband(sdata);
		if (!sband)
			goto free_txq;

		smps = (sband->ht_cap.cap & IEEE80211_HT_CAP_SM_PS) >>
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		/*
		 * Assume that hostapd advertises our caps in the beacon and
		 * this is the known_smps_mode for a station that just assciated
		 */
		switch (smps) {
		case WLAN_HT_SMPS_CONTROL_DISABLED:
			sta->known_smps_mode = IEEE80211_SMPS_OFF;
			break;
		case WLAN_HT_SMPS_CONTROL_STATIC:
			sta->known_smps_mode = IEEE80211_SMPS_STATIC;
			break;
		case WLAN_HT_SMPS_CONTROL_DYNAMIC:
			sta->known_smps_mode = IEEE80211_SMPS_DYNAMIC;
			break;
		default:
			WARN_ON(1);
		}
	}

	sta->sta.max_rc_amsdu_len = IEEE80211_MAX_MPDU_LEN_HT_BA;

	sta->cparams.ce_threshold = CODEL_DISABLED_THRESHOLD;
	sta->cparams.target = MS2TIME(20);
	sta->cparams.interval = MS2TIME(100);
	sta->cparams.ecn = true;

	sta_dbg(sdata, "Allocated STA %pM\n", sta->sta.addr);

	return sta;

free_txq:
	if (sta->sta.txq[0])
		kfree(to_txq_info(sta->sta.txq[0]));
free:
#ifdef CONFIG_MAC80211_MESH
	kfree(sta->mesh);
#endif
	kfree(sta);
	return NULL;
}

static int sta_info_insert_check(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	/*
	 * Can't be a WARN_ON because it can be triggered through a race:
	 * something inserts a STA (on one CPU) without holding the RTNL
	 * and another CPU turns off the net device.
	 */
	if (unlikely(!ieee80211_sdata_running(sdata)))
		return -ENETDOWN;

	if (WARN_ON(ether_addr_equal(sta->sta.addr, sdata->vif.addr) ||
		    is_multicast_ether_addr(sta->sta.addr)))
		return -EINVAL;

	/* The RCU read lock is required by rhashtable due to
	 * asynchronous resize/rehash.  We also require the mutex
	 * for correctness.
	 */
	rcu_read_lock();
	lockdep_assert_held(&sdata->local->sta_mtx);
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
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_P2P_PS);
	}
}

/*
 * should be called with sta_mtx locked
 * this function replaces the mutex lock
 * with a RCU lock
 */
static int sta_info_insert_finish(struct sta_info *sta) __acquires(RCU)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct station_info *sinfo = NULL;
	int err = 0;

	lockdep_assert_held(&local->sta_mtx);

	/* check if STA exists already */
	if (sta_info_get_bss(sdata, sta->sta.addr)) {
		err = -EEXIST;
		goto out_err;
	}

	sinfo = kzalloc(sizeof(struct station_info), GFP_KERNEL);
	if (!sinfo) {
		err = -ENOMEM;
		goto out_err;
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

	list_add_tail_rcu(&sta->list, &local->sta_list);

	/* notify driver */
	err = sta_info_insert_drv_state(local, sdata, sta);
	if (err)
		goto out_remove;

	set_sta_flag(sta, WLAN_STA_INSERTED);

	if (sta->sta_state >= IEEE80211_STA_ASSOC) {
		ieee80211_recalc_min_chandef(sta->sdata);
		if (!sta->sta.support_p2p_ps)
			ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
	}

	/* accept BA sessions now */
	clear_sta_flag(sta, WLAN_STA_BLOCK_BA);

	ieee80211_sta_debugfs_add(sta);
	rate_control_add_sta_debugfs(sta);

	sinfo->generation = local->sta_generation;
	cfg80211_new_sta(sdata->dev, sta->sta.addr, sinfo, GFP_KERNEL);
	kfree(sinfo);

	sta_dbg(sdata, "Inserted STA %pM\n", sta->sta.addr);

	/* move reference to rcu-protected */
	rcu_read_lock();
	mutex_unlock(&local->sta_mtx);

	if (ieee80211_vif_is_mesh(&sdata->vif))
		mesh_accept_plinks_update(sdata);

	return 0;
 out_remove:
	sta_info_hash_del(local, sta);
	list_del_rcu(&sta->list);
 out_drop_sta:
	local->num_sta--;
	synchronize_net();
	__cleanup_single_sta(sta);
 out_err:
	mutex_unlock(&local->sta_mtx);
	kfree(sinfo);
	rcu_read_lock();
	return err;
}

int sta_info_insert_rcu(struct sta_info *sta) __acquires(RCU)
{
	struct ieee80211_local *local = sta->local;
	int err;

	might_sleep();

	mutex_lock(&local->sta_mtx);

	err = sta_info_insert_check(sta);
	if (err) {
		mutex_unlock(&local->sta_mtx);
		rcu_read_lock();
		goto out_free;
	}

	err = sta_info_insert_finish(sta);
	if (err)
		goto out_free;

	return 0;
 out_free:
	sta_info_free(local, sta);
	return err;
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
	int ret;

	might_sleep();

	if (!sta)
		return -ENOENT;

	local = sta->local;
	sdata = sta->sdata;

	lockdep_assert_held(&local->sta_mtx);

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

	drv_sta_pre_rcu_remove(local, sta->sdata, sta);

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
	    rcu_access_pointer(sdata->u.vlan.sta) == sta)
		RCU_INIT_POINTER(sdata->u.vlan.sta, NULL);

	return 0;
}

static void __sta_info_destroy_part2(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct station_info *sinfo;
	int ret;

	/*
	 * NOTE: This assumes at least synchronize_net() was done
	 *	 after _part1 and before _part2!
	 */

	might_sleep();
	lockdep_assert_held(&local->sta_mtx);

	/* now keys can no longer be reached */
	ieee80211_free_sta_keys(local, sta);

	/* disable TIM bit - last chance to tell driver */
	__sta_info_recalc_tim(sta, true);

	sta->dead = true;

	local->num_sta--;
	local->sta_generation++;

	while (sta->sta_state > IEEE80211_STA_NONE) {
		ret = sta_info_move_state(sta, sta->sta_state - 1);
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
		sta_set_sinfo(sta, sinfo);
	cfg80211_del_sta_sinfo(sdata->dev, sta->sta.addr, sinfo, GFP_KERNEL);
	kfree(sinfo);

	rate_control_remove_sta_debugfs(sta);
	ieee80211_sta_debugfs_remove(sta);

	cleanup_single_sta(sta);
}

int __must_check __sta_info_destroy(struct sta_info *sta)
{
	int err = __sta_info_destroy_part1(sta);

	if (err)
		return err;

	synchronize_net();

	__sta_info_destroy_part2(sta);

	return 0;
}

int sta_info_destroy_addr(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	struct sta_info *sta;
	int ret;

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get(sdata, addr);
	ret = __sta_info_destroy(sta);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

int sta_info_destroy_addr_bss(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr)
{
	struct sta_info *sta;
	int ret;

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get_bss(sdata, addr);
	ret = __sta_info_destroy(sta);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

static void sta_info_cleanup(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
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

	spin_lock_init(&local->tim_lock);
	mutex_init(&local->sta_mtx);
	INIT_LIST_HEAD(&local->sta_list);

	setup_timer(&local->sta_cleanup, sta_info_cleanup,
		    (unsigned long)local);
	return 0;
}

void sta_info_stop(struct ieee80211_local *local)
{
	del_timer_sync(&local->sta_cleanup);
	rhltable_destroy(&local->sta_hash);
}


int __sta_info_flush(struct ieee80211_sub_if_data *sdata, bool vlans)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;
	LIST_HEAD(free_list);
	int ret = 0;

	might_sleep();

	WARN_ON(vlans && sdata->vif.type != NL80211_IFTYPE_AP);
	WARN_ON(vlans && !sdata->bss);

	mutex_lock(&local->sta_mtx);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		if (sdata == sta->sdata ||
		    (vlans && sdata->bss == sta->sdata->bss)) {
			if (!WARN_ON(__sta_info_destroy_part1(sta)))
				list_add(&sta->free_list, &free_list);
			ret++;
		}
	}

	if (!list_empty(&free_list)) {
		synchronize_net();
		list_for_each_entry_safe(sta, tmp, &free_list, free_list)
			__sta_info_destroy_part2(sta);
	}
	mutex_unlock(&local->sta_mtx);

	return ret;
}

void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;

	mutex_lock(&local->sta_mtx);

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

	mutex_unlock(&local->sta_mtx);
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

	if (sta->sta.txq[0]) {
		for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
			if (!txq_has_queue(sta->sta.txq[i]))
				continue;

			drv_wake_tx_queue(local, to_txq_info(sta->sta.txq[i]));
		}
	}

	skb_queue_head_init(&pending);

	/* sync with ieee80211_tx_h_unicast_ps_buf */
	spin_lock(&sta->ps_lock);
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
	spin_unlock(&sta->ps_lock);

	atomic_dec(&ps->num_sta_ps);

	/* This station just woke up and isn't aware of our SMPS state */
	if (!ieee80211_vif_is_mesh(&sdata->vif) &&
	    !ieee80211_smps_is_restrictive(sta->known_smps_mode,
					   sdata->smps_mode) &&
	    sta->known_smps_mode != sdata->bss->req_smps &&
	    sta_info_tx_streams(sta) != 1) {
		ht_dbg(sdata,
		       "%pM just woke up and MIMO capable - update SMPS\n",
		       sta->sta.addr);
		ieee80211_send_smps_action(sdata, sdata->bss->req_smps,
					   sta->sta.addr,
					   sdata->vif.bss_conf.bssid);
	}

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
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
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

		if (!sta->sta.txq[0])
			return;

		for (tid = 0; tid < ARRAY_SIZE(sta->sta.txq); tid++) {
			if (!(driver_release_tids & BIT(tid)) ||
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

int sta_info_move_state(struct sta_info *sta,
			enum ieee80211_sta_state new_state)
{
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

	/*
	 * notify the driver before the actual changes so it can
	 * fail the transition
	 */
	if (test_sta_flag(sta, WLAN_STA_INSERTED)) {
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
			ieee80211_recalc_min_chandef(sta->sdata);
			if (!sta->sta.support_p2p_ps)
				ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
		}
		break;
	case IEEE80211_STA_ASSOC:
		if (sta->sta_state == IEEE80211_STA_AUTH) {
			set_bit(WLAN_STA_ASSOC, &sta->_flags);
			ieee80211_recalc_min_chandef(sta->sdata);
			if (!sta->sta.support_p2p_ps)
				ieee80211_recalc_p2p_go_ps_allowed(sta->sdata);
		} else if (sta->sta_state == IEEE80211_STA_AUTHORIZED) {
			ieee80211_vif_dec_num_mcast(sta->sdata);
			clear_bit(WLAN_STA_AUTHORIZED, &sta->_flags);
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
		break;
	default:
		break;
	}

	sta->sta_state = new_state;

	return 0;
}

u8 sta_info_tx_streams(struct sta_info *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sta->sta.ht_cap;
	u8 rx_streams;

	if (!sta->sta.ht_cap.ht_supported)
		return 1;

	if (sta->sta.vht_cap.vht_supported) {
		int i;
		u16 tx_mcs_map =
			le16_to_cpu(sta->sta.vht_cap.vht_mcs.tx_mcs_map);

		for (i = 7; i >= 0; i--)
			if ((tx_mcs_map & (0x3 << (i * 2))) !=
			    IEEE80211_VHT_MCS_NOT_SUPPORTED)
				return i + 1;
	}

	if (ht_cap->mcs.rx_mask[3])
		rx_streams = 4;
	else if (ht_cap->mcs.rx_mask[2])
		rx_streams = 3;
	else if (ht_cap->mcs.rx_mask[1])
		rx_streams = 2;
	else
		rx_streams = 1;

	if (!(ht_cap->mcs.tx_params & IEEE80211_HT_MCS_TX_RX_DIFF))
		return rx_streams;

	return ((ht_cap->mcs.tx_params & IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK)
			>> IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT) + 1;
}

static struct ieee80211_sta_rx_stats *
sta_get_last_rx_stats(struct sta_info *sta)
{
	struct ieee80211_sta_rx_stats *stats = &sta->rx_stats;
	struct ieee80211_local *local = sta->local;
	int cpu;

	if (!ieee80211_hw_check(&local->hw, USES_RSS))
		return stats;

	for_each_possible_cpu(cpu) {
		struct ieee80211_sta_rx_stats *cpustats;

		cpustats = per_cpu_ptr(sta->pcpu_rx_stats, cpu);

		if (time_after(cpustats->last_rx, stats->last_rx))
			stats = cpustats;
	}

	return stats;
}

static void sta_stats_decode_rate(struct ieee80211_local *local, u16 rate,
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

		rinfo->flags = 0;
		sband = local->hw.wiphy->bands[band];
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
	}
}

static int sta_set_rate_info_rx(struct sta_info *sta, struct rate_info *rinfo)
{
	u16 rate = READ_ONCE(sta_get_last_rx_stats(sta)->last_rate);

	if (rate == STA_STATS_RATE_INVALID)
		return -EINVAL;

	sta_stats_decode_rate(sta->local, rate, rinfo);
	return 0;
}

static void sta_set_tidstats(struct sta_info *sta,
			     struct cfg80211_tid_stats *tidstats,
			     int tid)
{
	struct ieee80211_local *local = sta->local;

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_RX_MSDU))) {
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&sta->rx_stats.syncp);
			tidstats->rx_msdu = sta->rx_stats.msdu[tid];
		} while (u64_stats_fetch_retry(&sta->rx_stats.syncp, start));

		tidstats->filled |= BIT(NL80211_TID_STATS_RX_MSDU);
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU))) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU);
		tidstats->tx_msdu = sta->tx_stats.msdu[tid];
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU_RETRIES)) &&
	    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU_RETRIES);
		tidstats->tx_msdu_retries = sta->status_stats.msdu_retries[tid];
	}

	if (!(tidstats->filled & BIT(NL80211_TID_STATS_TX_MSDU_FAILED)) &&
	    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		tidstats->filled |= BIT(NL80211_TID_STATS_TX_MSDU_FAILED);
		tidstats->tx_msdu_failed = sta->status_stats.msdu_failed[tid];
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

void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo)
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
		sinfo->rx_beacon = sdata->u.mgd.count_beacon_signal;

	drv_sta_statistics(local, sdata, &sta->sta, sinfo);

	sinfo->filled |= BIT(NL80211_STA_INFO_INACTIVE_TIME) |
			 BIT(NL80211_STA_INFO_STA_FLAGS) |
			 BIT(NL80211_STA_INFO_BSS_PARAM) |
			 BIT(NL80211_STA_INFO_CONNECTED_TIME) |
			 BIT(NL80211_STA_INFO_RX_DROP_MISC);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		sinfo->beacon_loss_count = sdata->u.mgd.beacon_loss_count;
		sinfo->filled |= BIT(NL80211_STA_INFO_BEACON_LOSS);
	}

	sinfo->connected_time = ktime_get_seconds() - sta->last_connected;
	sinfo->inactive_time =
		jiffies_to_msecs(jiffies - ieee80211_sta_last_active(sta));

	if (!(sinfo->filled & (BIT(NL80211_STA_INFO_TX_BYTES64) |
			       BIT(NL80211_STA_INFO_TX_BYTES)))) {
		sinfo->tx_bytes = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->tx_bytes += sta->tx_stats.bytes[ac];
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_BYTES64);
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_TX_PACKETS))) {
		sinfo->tx_packets = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			sinfo->tx_packets += sta->tx_stats.packets[ac];
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_PACKETS);
	}

	if (!(sinfo->filled & (BIT(NL80211_STA_INFO_RX_BYTES64) |
			       BIT(NL80211_STA_INFO_RX_BYTES)))) {
		sinfo->rx_bytes += sta_get_stats_bytes(&sta->rx_stats);

		if (sta->pcpu_rx_stats) {
			for_each_possible_cpu(cpu) {
				struct ieee80211_sta_rx_stats *cpurxs;

				cpurxs = per_cpu_ptr(sta->pcpu_rx_stats, cpu);
				sinfo->rx_bytes += sta_get_stats_bytes(cpurxs);
			}
		}

		sinfo->filled |= BIT(NL80211_STA_INFO_RX_BYTES64);
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_RX_PACKETS))) {
		sinfo->rx_packets = sta->rx_stats.packets;
		if (sta->pcpu_rx_stats) {
			for_each_possible_cpu(cpu) {
				struct ieee80211_sta_rx_stats *cpurxs;

				cpurxs = per_cpu_ptr(sta->pcpu_rx_stats, cpu);
				sinfo->rx_packets += cpurxs->packets;
			}
		}
		sinfo->filled |= BIT(NL80211_STA_INFO_RX_PACKETS);
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_TX_RETRIES))) {
		sinfo->tx_retries = sta->status_stats.retry_count;
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_RETRIES);
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_TX_FAILED))) {
		sinfo->tx_failed = sta->status_stats.retry_failed;
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_FAILED);
	}

	sinfo->rx_dropped_misc = sta->rx_stats.dropped;
	if (sta->pcpu_rx_stats) {
		for_each_possible_cpu(cpu) {
			struct ieee80211_sta_rx_stats *cpurxs;

			cpurxs = per_cpu_ptr(sta->pcpu_rx_stats, cpu);
			sinfo->rx_dropped_misc += cpurxs->dropped;
		}
	}

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)) {
		sinfo->filled |= BIT(NL80211_STA_INFO_BEACON_RX) |
				 BIT(NL80211_STA_INFO_BEACON_SIGNAL_AVG);
		sinfo->rx_beacon_signal_avg = ieee80211_ave_rssi(&sdata->vif);
	}

	if (ieee80211_hw_check(&sta->local->hw, SIGNAL_DBM) ||
	    ieee80211_hw_check(&sta->local->hw, SIGNAL_UNSPEC)) {
		if (!(sinfo->filled & BIT(NL80211_STA_INFO_SIGNAL))) {
			sinfo->signal = (s8)last_rxstats->last_signal;
			sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
		}

		if (!sta->pcpu_rx_stats &&
		    !(sinfo->filled & BIT(NL80211_STA_INFO_SIGNAL_AVG))) {
			sinfo->signal_avg =
				-ewma_signal_read(&sta->rx_stats_avg.signal);
			sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL_AVG);
		}
	}

	/* for the average - if pcpu_rx_stats isn't set - rxstats must point to
	 * the sta->rx_stats struct, so the check here is fine with and without
	 * pcpu statistics
	 */
	if (last_rxstats->chains &&
	    !(sinfo->filled & (BIT(NL80211_STA_INFO_CHAIN_SIGNAL) |
			       BIT(NL80211_STA_INFO_CHAIN_SIGNAL_AVG)))) {
		sinfo->filled |= BIT(NL80211_STA_INFO_CHAIN_SIGNAL);
		if (!sta->pcpu_rx_stats)
			sinfo->filled |= BIT(NL80211_STA_INFO_CHAIN_SIGNAL_AVG);

		sinfo->chains = last_rxstats->chains;

		for (i = 0; i < ARRAY_SIZE(sinfo->chain_signal); i++) {
			sinfo->chain_signal[i] =
				last_rxstats->chain_signal_last[i];
			sinfo->chain_signal_avg[i] =
				-ewma_signal_read(&sta->rx_stats_avg.chain_signal[i]);
		}
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_TX_BITRATE))) {
		sta_set_rate_info_tx(sta, &sta->tx_stats.last_rate,
				     &sinfo->txrate);
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
	}

	if (!(sinfo->filled & BIT(NL80211_STA_INFO_RX_BITRATE))) {
		if (sta_set_rate_info_rx(sta, &sinfo->rxrate) == 0)
			sinfo->filled |= BIT(NL80211_STA_INFO_RX_BITRATE);
	}

	sinfo->filled |= BIT(NL80211_STA_INFO_TID_STATS);
	for (i = 0; i < IEEE80211_NUM_TIDS + 1; i++) {
		struct cfg80211_tid_stats *tidstats = &sinfo->pertid[i];

		sta_set_tidstats(sta, tidstats, i);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
#ifdef CONFIG_MAC80211_MESH
		sinfo->filled |= BIT(NL80211_STA_INFO_LLID) |
				 BIT(NL80211_STA_INFO_PLID) |
				 BIT(NL80211_STA_INFO_PLINK_STATE) |
				 BIT(NL80211_STA_INFO_LOCAL_PM) |
				 BIT(NL80211_STA_INFO_PEER_PM) |
				 BIT(NL80211_STA_INFO_NONPEER_PM);

		sinfo->llid = sta->mesh->llid;
		sinfo->plid = sta->mesh->plid;
		sinfo->plink_state = sta->mesh->plink_state;
		if (test_sta_flag(sta, WLAN_STA_TOFFSET_KNOWN)) {
			sinfo->filled |= BIT(NL80211_STA_INFO_T_OFFSET);
			sinfo->t_offset = sta->mesh->t_offset;
		}
		sinfo->local_pm = sta->mesh->local_pm;
		sinfo->peer_pm = sta->mesh->peer_pm;
		sinfo->nonpeer_pm = sta->mesh->nonpeer_pm;
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
		sinfo->filled |= BIT(NL80211_STA_INFO_EXPECTED_THROUGHPUT);
		sinfo->expected_throughput = thr;
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

	if (time_after(stats->last_rx, sta->status_stats.last_ack))
		return stats->last_rx;
	return sta->status_stats.last_ack;
}

static void sta_update_codel_params(struct sta_info *sta, u32 thr)
{
	if (!sta->sdata->local->ops->wake_tx_queue)
		return;

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
