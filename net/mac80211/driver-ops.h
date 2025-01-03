/* SPDX-License-Identifier: GPL-2.0 */
/*
* Portions of this file
* Copyright(c) 2016 Intel Deutschland GmbH
* Copyright (C) 2018-2019, 2021-2024 Intel Corporation
*/

#ifndef __MAC80211_DRIVER_OPS
#define __MAC80211_DRIVER_OPS

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "trace.h"

#define check_sdata_in_driver(sdata)	({					\
	WARN_ONCE(!sdata->local->reconfig_failure &&				\
		  !(sdata->flags & IEEE80211_SDATA_IN_DRIVER),			\
		  "%s: Failed check-sdata-in-driver check, flags: 0x%x\n",	\
		  sdata->dev ? sdata->dev->name : sdata->name, sdata->flags);	\
	!!(sdata->flags & IEEE80211_SDATA_IN_DRIVER);				\
})

static inline struct ieee80211_sub_if_data *
get_bss_sdata(struct ieee80211_sub_if_data *sdata)
{
	if (sdata && sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss, struct ieee80211_sub_if_data,
				     u.ap);

	return sdata;
}

static inline void drv_tx(struct ieee80211_local *local,
			  struct ieee80211_tx_control *control,
			  struct sk_buff *skb)
{
	local->ops->tx(&local->hw, control, skb);
}

static inline void drv_sync_rx_queues(struct ieee80211_local *local,
				      struct sta_info *sta)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (local->ops->sync_rx_queues) {
		trace_drv_sync_rx_queues(local, sta->sdata, &sta->sta);
		local->ops->sync_rx_queues(&local->hw);
		trace_drv_return_void(local);
	}
}

static inline void drv_get_et_strings(struct ieee80211_sub_if_data *sdata,
				      u32 sset, u8 *data)
{
	struct ieee80211_local *local = sdata->local;
	if (local->ops->get_et_strings) {
		trace_drv_get_et_strings(local, sset);
		local->ops->get_et_strings(&local->hw, &sdata->vif, sset, data);
		trace_drv_return_void(local);
	}
}

static inline void drv_get_et_stats(struct ieee80211_sub_if_data *sdata,
				    struct ethtool_stats *stats,
				    u64 *data)
{
	struct ieee80211_local *local = sdata->local;
	if (local->ops->get_et_stats) {
		trace_drv_get_et_stats(local);
		local->ops->get_et_stats(&local->hw, &sdata->vif, stats, data);
		trace_drv_return_void(local);
	}
}

static inline int drv_get_et_sset_count(struct ieee80211_sub_if_data *sdata,
					int sset)
{
	struct ieee80211_local *local = sdata->local;
	int rv = 0;
	if (local->ops->get_et_sset_count) {
		trace_drv_get_et_sset_count(local, sset);
		rv = local->ops->get_et_sset_count(&local->hw, &sdata->vif,
						   sset);
		trace_drv_return_int(local, rv);
	}
	return rv;
}

int drv_start(struct ieee80211_local *local);
void drv_stop(struct ieee80211_local *local, bool suspend);

#ifdef CONFIG_PM
static inline int drv_suspend(struct ieee80211_local *local,
			      struct cfg80211_wowlan *wowlan)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_suspend(local);
	ret = local->ops->suspend(&local->hw, wowlan);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_resume(struct ieee80211_local *local)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_resume(local);
	ret = local->ops->resume(&local->hw);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_set_wakeup(struct ieee80211_local *local,
				  bool enabled)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!local->ops->set_wakeup)
		return;

	trace_drv_set_wakeup(local, enabled);
	local->ops->set_wakeup(&local->hw, enabled);
	trace_drv_return_void(local);
}
#endif

int drv_add_interface(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata);

int drv_change_interface(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 enum nl80211_iftype type, bool p2p);

void drv_remove_interface(struct ieee80211_local *local,
			  struct ieee80211_sub_if_data *sdata);

static inline int drv_config(struct ieee80211_local *local, u32 changed)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_config(local, changed);
	ret = local->ops->config(&local->hw, changed);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_vif_cfg_changed(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       u64 changed)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_vif_cfg_changed(local, sdata, changed);
	if (local->ops->vif_cfg_changed)
		local->ops->vif_cfg_changed(&local->hw, &sdata->vif, changed);
	else if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, &sdata->vif,
					     &sdata->vif.bss_conf, changed);
	trace_drv_return_void(local);
}

void drv_link_info_changed(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_bss_conf *info,
			   int link_id, u64 changed);

static inline u64 drv_prepare_multicast(struct ieee80211_local *local,
					struct netdev_hw_addr_list *mc_list)
{
	u64 ret = 0;

	trace_drv_prepare_multicast(local, mc_list->count);

	if (local->ops->prepare_multicast)
		ret = local->ops->prepare_multicast(&local->hw, mc_list);

	trace_drv_return_u64(local, ret);

	return ret;
}

static inline void drv_configure_filter(struct ieee80211_local *local,
					unsigned int changed_flags,
					unsigned int *total_flags,
					u64 multicast)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_configure_filter(local, changed_flags, total_flags,
				   multicast);
	local->ops->configure_filter(&local->hw, changed_flags, total_flags,
				     multicast);
	trace_drv_return_void(local);
}

static inline void drv_config_iface_filter(struct ieee80211_local *local,
					   struct ieee80211_sub_if_data *sdata,
					   unsigned int filter_flags,
					   unsigned int changed_flags)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_config_iface_filter(local, sdata, filter_flags,
				      changed_flags);
	if (local->ops->config_iface_filter)
		local->ops->config_iface_filter(&local->hw, &sdata->vif,
						filter_flags,
						changed_flags);
	trace_drv_return_void(local);
}

static inline int drv_set_tim(struct ieee80211_local *local,
			      struct ieee80211_sta *sta, bool set)
{
	int ret = 0;
	trace_drv_set_tim(local, sta, set);
	if (local->ops->set_tim)
		ret = local->ops->set_tim(&local->hw, sta, set);
	trace_drv_return_int(local, ret);
	return ret;
}

int drv_set_key(struct ieee80211_local *local,
		enum set_key_cmd cmd,
		struct ieee80211_sub_if_data *sdata,
		struct ieee80211_sta *sta,
		struct ieee80211_key_conf *key);

static inline void drv_update_tkip_key(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_key_conf *conf,
				       struct sta_info *sta, u32 iv32,
				       u16 *phase1key)
{
	struct ieee80211_sta *ista = NULL;

	if (sta)
		ista = &sta->sta;

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_update_tkip_key(local, sdata, conf, ista, iv32);
	if (local->ops->update_tkip_key)
		local->ops->update_tkip_key(&local->hw, &sdata->vif, conf,
					    ista, iv32, phase1key);
	trace_drv_return_void(local);
}

static inline int drv_hw_scan(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_scan_request *req)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_hw_scan(local, sdata);
	ret = local->ops->hw_scan(&local->hw, &sdata->vif, req);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_cancel_hw_scan(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_cancel_hw_scan(local, sdata);
	local->ops->cancel_hw_scan(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

static inline int
drv_sched_scan_start(struct ieee80211_local *local,
		     struct ieee80211_sub_if_data *sdata,
		     struct cfg80211_sched_scan_request *req,
		     struct ieee80211_scan_ies *ies)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_sched_scan_start(local, sdata);
	ret = local->ops->sched_scan_start(&local->hw, &sdata->vif,
					      req, ies);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_sched_scan_stop(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_sched_scan_stop(local, sdata);
	ret = local->ops->sched_scan_stop(&local->hw, &sdata->vif);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_sw_scan_start(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     const u8 *mac_addr)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_sw_scan_start(local, sdata, mac_addr);
	if (local->ops->sw_scan_start)
		local->ops->sw_scan_start(&local->hw, &sdata->vif, mac_addr);
	trace_drv_return_void(local);
}

static inline void drv_sw_scan_complete(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_sw_scan_complete(local, sdata);
	if (local->ops->sw_scan_complete)
		local->ops->sw_scan_complete(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

static inline int drv_get_stats(struct ieee80211_local *local,
				struct ieee80211_low_level_stats *stats)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (local->ops->get_stats)
		ret = local->ops->get_stats(&local->hw, stats);
	trace_drv_get_stats(local, stats, ret);

	return ret;
}

static inline void drv_get_key_seq(struct ieee80211_local *local,
				   struct ieee80211_key *key,
				   struct ieee80211_key_seq *seq)
{
	if (local->ops->get_key_seq)
		local->ops->get_key_seq(&local->hw, &key->conf, seq);
	trace_drv_get_key_seq(local, &key->conf);
}

static inline int drv_set_frag_threshold(struct ieee80211_local *local,
					u32 value)
{
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_set_frag_threshold(local, value);
	if (local->ops->set_frag_threshold)
		ret = local->ops->set_frag_threshold(&local->hw, value);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_rts_threshold(struct ieee80211_local *local,
					u32 value)
{
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_set_rts_threshold(local, value);
	if (local->ops->set_rts_threshold)
		ret = local->ops->set_rts_threshold(&local->hw, value);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_coverage_class(struct ieee80211_local *local,
					 s16 value)
{
	int ret = 0;
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_set_coverage_class(local, value);
	if (local->ops->set_coverage_class)
		local->ops->set_coverage_class(&local->hw, value);
	else
		ret = -EOPNOTSUPP;

	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_sta_notify(struct ieee80211_local *local,
				  struct ieee80211_sub_if_data *sdata,
				  enum sta_notify_cmd cmd,
				  struct ieee80211_sta *sta)
{
	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_notify(local, sdata, cmd, sta);
	if (local->ops->sta_notify)
		local->ops->sta_notify(&local->hw, &sdata->vif, cmd, sta);
	trace_drv_return_void(local);
}

static inline int drv_sta_add(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_sta *sta)
{
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_sta_add(local, sdata, sta);
	if (local->ops->sta_add)
		ret = local->ops->sta_add(&local->hw, &sdata->vif, sta);

	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_sta_remove(struct ieee80211_local *local,
				  struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_sta *sta)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_remove(local, sdata, sta);
	if (local->ops->sta_remove)
		local->ops->sta_remove(&local->hw, &sdata->vif, sta);

	trace_drv_return_void(local);
}

#ifdef CONFIG_MAC80211_DEBUGFS
static inline void drv_vif_add_debugfs(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata)
{
	might_sleep();

	if (sdata->vif.type == NL80211_IFTYPE_MONITOR ||
	    WARN_ON(!sdata->vif.debugfs_dir))
		return;

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	if (local->ops->vif_add_debugfs)
		local->ops->vif_add_debugfs(&local->hw, &sdata->vif);
}

static inline void drv_link_add_debugfs(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					struct ieee80211_bss_conf *link_conf,
					struct dentry *dir)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	if (local->ops->link_add_debugfs)
		local->ops->link_add_debugfs(&local->hw, &sdata->vif,
					     link_conf, dir);
}

static inline void drv_sta_add_debugfs(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_sta *sta,
				       struct dentry *dir)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	if (local->ops->sta_add_debugfs)
		local->ops->sta_add_debugfs(&local->hw, &sdata->vif,
					    sta, dir);
}

static inline void drv_link_sta_add_debugfs(struct ieee80211_local *local,
					    struct ieee80211_sub_if_data *sdata,
					    struct ieee80211_link_sta *link_sta,
					    struct dentry *dir)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	if (local->ops->link_sta_add_debugfs)
		local->ops->link_sta_add_debugfs(&local->hw, &sdata->vif,
						 link_sta, dir);
}
#else
static inline void drv_vif_add_debugfs(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
}
#endif

static inline void drv_sta_pre_rcu_remove(struct ieee80211_local *local,
					  struct ieee80211_sub_if_data *sdata,
					  struct sta_info *sta)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_pre_rcu_remove(local, sdata, &sta->sta);
	if (local->ops->sta_pre_rcu_remove)
		local->ops->sta_pre_rcu_remove(&local->hw, &sdata->vif,
					       &sta->sta);
	trace_drv_return_void(local);
}

__must_check
int drv_sta_state(struct ieee80211_local *local,
		  struct ieee80211_sub_if_data *sdata,
		  struct sta_info *sta,
		  enum ieee80211_sta_state old_state,
		  enum ieee80211_sta_state new_state);

__must_check
int drv_sta_set_txpwr(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata,
		      struct sta_info *sta);

void drv_link_sta_rc_update(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata,
			    struct ieee80211_link_sta *link_sta, u32 changed);

static inline void drv_sta_rate_tbl_update(struct ieee80211_local *local,
					   struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_sta *sta)
{
	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_rate_tbl_update(local, sdata, sta);
	if (local->ops->sta_rate_tbl_update)
		local->ops->sta_rate_tbl_update(&local->hw, &sdata->vif, sta);

	trace_drv_return_void(local);
}

static inline void drv_sta_statistics(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_sta *sta,
				      struct station_info *sinfo)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_statistics(local, sdata, sta);
	if (local->ops->sta_statistics)
		local->ops->sta_statistics(&local->hw, &sdata->vif, sta, sinfo);
	trace_drv_return_void(local);
}

int drv_conf_tx(struct ieee80211_local *local,
		struct ieee80211_link_data *link, u16 ac,
		const struct ieee80211_tx_queue_params *params);

u64 drv_get_tsf(struct ieee80211_local *local,
		struct ieee80211_sub_if_data *sdata);
void drv_set_tsf(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u64 tsf);
void drv_offset_tsf(struct ieee80211_local *local,
		    struct ieee80211_sub_if_data *sdata,
		    s64 offset);
void drv_reset_tsf(struct ieee80211_local *local,
		   struct ieee80211_sub_if_data *sdata);

static inline int drv_tx_last_beacon(struct ieee80211_local *local)
{
	int ret = 0; /* default unsupported op for less congestion */

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_tx_last_beacon(local);
	if (local->ops->tx_last_beacon)
		ret = local->ops->tx_last_beacon(&local->hw);
	trace_drv_return_int(local, ret);
	return ret;
}

int drv_ampdu_action(struct ieee80211_local *local,
		     struct ieee80211_sub_if_data *sdata,
		     struct ieee80211_ampdu_params *params);

static inline int drv_get_survey(struct ieee80211_local *local, int idx,
				struct survey_info *survey)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_get_survey(local, idx, survey);

	if (local->ops->get_survey)
		ret = local->ops->get_survey(&local->hw, idx, survey);

	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_rfkill_poll(struct ieee80211_local *local)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (local->ops->rfkill_poll)
		local->ops->rfkill_poll(&local->hw);
}

static inline void drv_flush(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     u32 queues, bool drop)
{
	struct ieee80211_vif *vif;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);
	vif = sdata ? &sdata->vif : NULL;

	if (sdata && !check_sdata_in_driver(sdata))
		return;

	trace_drv_flush(local, queues, drop);
	if (local->ops->flush)
		local->ops->flush(&local->hw, vif, queues, drop);
	trace_drv_return_void(local);
}

static inline void drv_flush_sta(struct ieee80211_local *local,
				 struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	sdata = get_bss_sdata(sdata);

	if (sdata && !check_sdata_in_driver(sdata))
		return;

	trace_drv_flush_sta(local, sdata, &sta->sta);
	if (local->ops->flush_sta)
		local->ops->flush_sta(&local->hw, &sdata->vif, &sta->sta);
	trace_drv_return_void(local);
}

static inline void drv_channel_switch(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel_switch *ch_switch)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_channel_switch(local, sdata, ch_switch);
	local->ops->channel_switch(&local->hw, &sdata->vif, ch_switch);
	trace_drv_return_void(local);
}


static inline int drv_set_antenna(struct ieee80211_local *local,
				  u32 tx_ant, u32 rx_ant)
{
	int ret = -EOPNOTSUPP;
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (local->ops->set_antenna)
		ret = local->ops->set_antenna(&local->hw, tx_ant, rx_ant);
	trace_drv_set_antenna(local, tx_ant, rx_ant, ret);
	return ret;
}

static inline int drv_get_antenna(struct ieee80211_local *local,
				  u32 *tx_ant, u32 *rx_ant)
{
	int ret = -EOPNOTSUPP;
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (local->ops->get_antenna)
		ret = local->ops->get_antenna(&local->hw, tx_ant, rx_ant);
	trace_drv_get_antenna(local, *tx_ant, *rx_ant, ret);
	return ret;
}

static inline int drv_remain_on_channel(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					struct ieee80211_channel *chan,
					unsigned int duration,
					enum ieee80211_roc_type type)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_remain_on_channel(local, sdata, chan, duration, type);
	ret = local->ops->remain_on_channel(&local->hw, &sdata->vif,
					    chan, duration, type);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline int
drv_cancel_remain_on_channel(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_cancel_remain_on_channel(local, sdata);
	ret = local->ops->cancel_remain_on_channel(&local->hw, &sdata->vif);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline int drv_set_ringparam(struct ieee80211_local *local,
				    u32 tx, u32 rx)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_set_ringparam(local, tx, rx);
	if (local->ops->set_ringparam)
		ret = local->ops->set_ringparam(&local->hw, tx, rx);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_get_ringparam(struct ieee80211_local *local,
				     u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_get_ringparam(local, tx, tx_max, rx, rx_max);
	if (local->ops->get_ringparam)
		local->ops->get_ringparam(&local->hw, tx, tx_max, rx, rx_max);
	trace_drv_return_void(local);
}

static inline bool drv_tx_frames_pending(struct ieee80211_local *local)
{
	bool ret = false;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_tx_frames_pending(local);
	if (local->ops->tx_frames_pending)
		ret = local->ops->tx_frames_pending(&local->hw);
	trace_drv_return_bool(local, ret);

	return ret;
}

static inline int drv_set_bitrate_mask(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       const struct cfg80211_bitrate_mask *mask)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_set_bitrate_mask(local, sdata, mask);
	if (local->ops->set_bitrate_mask)
		ret = local->ops->set_bitrate_mask(&local->hw,
						   &sdata->vif, mask);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_set_rekey_data(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata,
				      struct cfg80211_gtk_rekey_data *data)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_set_rekey_data(local, sdata, data);
	if (local->ops->set_rekey_data)
		local->ops->set_rekey_data(&local->hw, &sdata->vif, data);
	trace_drv_return_void(local);
}

static inline void drv_event_callback(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata,
				      const struct ieee80211_event *event)
{
	trace_drv_event_callback(local, sdata, event);
	if (local->ops->event_callback)
		local->ops->event_callback(&local->hw, &sdata->vif, event);
	trace_drv_return_void(local);
}

static inline void
drv_release_buffered_frames(struct ieee80211_local *local,
			    struct sta_info *sta, u16 tids, int num_frames,
			    enum ieee80211_frame_release_type reason,
			    bool more_data)
{
	trace_drv_release_buffered_frames(local, &sta->sta, tids, num_frames,
					  reason, more_data);
	if (local->ops->release_buffered_frames)
		local->ops->release_buffered_frames(&local->hw, &sta->sta, tids,
						    num_frames, reason,
						    more_data);
	trace_drv_return_void(local);
}

static inline void
drv_allow_buffered_frames(struct ieee80211_local *local,
			  struct sta_info *sta, u16 tids, int num_frames,
			  enum ieee80211_frame_release_type reason,
			  bool more_data)
{
	trace_drv_allow_buffered_frames(local, &sta->sta, tids, num_frames,
					reason, more_data);
	if (local->ops->allow_buffered_frames)
		local->ops->allow_buffered_frames(&local->hw, &sta->sta,
						  tids, num_frames, reason,
						  more_data);
	trace_drv_return_void(local);
}

static inline void drv_mgd_prepare_tx(struct ieee80211_local *local,
				      struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_prep_tx_info *info)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;
	WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION);

	info->link_id = info->link_id < 0 ? 0 : info->link_id;
	trace_drv_mgd_prepare_tx(local, sdata, info->duration,
				 info->subtype, info->success);
	if (local->ops->mgd_prepare_tx)
		local->ops->mgd_prepare_tx(&local->hw, &sdata->vif, info);
	trace_drv_return_void(local);
}

static inline void drv_mgd_complete_tx(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_prep_tx_info *info)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;
	WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION);

	trace_drv_mgd_complete_tx(local, sdata, info->duration,
				  info->subtype, info->success);
	if (local->ops->mgd_complete_tx)
		local->ops->mgd_complete_tx(&local->hw, &sdata->vif, info);
	trace_drv_return_void(local);
}

static inline void
drv_mgd_protect_tdls_discover(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      int link_id)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;
	WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION);

	link_id = link_id > 0 ? link_id : 0;

	trace_drv_mgd_protect_tdls_discover(local, sdata);
	if (local->ops->mgd_protect_tdls_discover)
		local->ops->mgd_protect_tdls_discover(&local->hw, &sdata->vif,
						      link_id);
	trace_drv_return_void(local);
}

static inline int drv_add_chanctx(struct ieee80211_local *local,
				  struct ieee80211_chanctx *ctx)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_add_chanctx(local, ctx);
	if (local->ops->add_chanctx)
		ret = local->ops->add_chanctx(&local->hw, &ctx->conf);
	trace_drv_return_int(local, ret);
	if (!ret)
		ctx->driver_present = true;

	return ret;
}

static inline void drv_remove_chanctx(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(!ctx->driver_present))
		return;

	trace_drv_remove_chanctx(local, ctx);
	if (local->ops->remove_chanctx)
		local->ops->remove_chanctx(&local->hw, &ctx->conf);
	trace_drv_return_void(local);
	ctx->driver_present = false;
}

static inline void drv_change_chanctx(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      u32 changed)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_change_chanctx(local, ctx, changed);
	if (local->ops->change_chanctx) {
		WARN_ON_ONCE(!ctx->driver_present);
		local->ops->change_chanctx(&local->hw, &ctx->conf, changed);
	}
	trace_drv_return_void(local);
}

int drv_assign_vif_chanctx(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_bss_conf *link_conf,
			   struct ieee80211_chanctx *ctx);
void drv_unassign_vif_chanctx(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_bss_conf *link_conf,
			      struct ieee80211_chanctx *ctx);
int drv_switch_vif_chanctx(struct ieee80211_local *local,
			   struct ieee80211_vif_chanctx_switch *vifs,
			   int n_vifs, enum ieee80211_chanctx_switch_mode mode);

static inline int drv_start_ap(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_bss_conf *link_conf)
{
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_start_ap(local, sdata, link_conf);
	if (local->ops->start_ap)
		ret = local->ops->start_ap(&local->hw, &sdata->vif, link_conf);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_stop_ap(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_bss_conf *link_conf)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_stop_ap(local, sdata, link_conf);
	if (local->ops->stop_ap)
		local->ops->stop_ap(&local->hw, &sdata->vif, link_conf);
	trace_drv_return_void(local);
}

static inline void
drv_reconfig_complete(struct ieee80211_local *local,
		      enum ieee80211_reconfig_type reconfig_type)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	trace_drv_reconfig_complete(local, reconfig_type);
	if (local->ops->reconfig_complete)
		local->ops->reconfig_complete(&local->hw, reconfig_type);
	trace_drv_return_void(local);
}

static inline void
drv_set_default_unicast_key(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata,
			    int key_idx)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	WARN_ON_ONCE(key_idx < -1 || key_idx > 3);

	trace_drv_set_default_unicast_key(local, sdata, key_idx);
	if (local->ops->set_default_unicast_key)
		local->ops->set_default_unicast_key(&local->hw, &sdata->vif,
						    key_idx);
	trace_drv_return_void(local);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline void drv_ipv6_addr_change(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					struct inet6_dev *idev)
{
	trace_drv_ipv6_addr_change(local, sdata);
	if (local->ops->ipv6_addr_change)
		local->ops->ipv6_addr_change(&local->hw, &sdata->vif, idev);
	trace_drv_return_void(local);
}
#endif

static inline void
drv_channel_switch_beacon(struct ieee80211_sub_if_data *sdata,
			  struct cfg80211_chan_def *chandef)
{
	struct ieee80211_local *local = sdata->local;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (local->ops->channel_switch_beacon) {
		trace_drv_channel_switch_beacon(local, sdata, chandef);
		local->ops->channel_switch_beacon(&local->hw, &sdata->vif,
						  chandef);
	}
}

static inline int
drv_pre_channel_switch(struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_channel_switch *ch_switch)
{
	struct ieee80211_local *local = sdata->local;
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (!ieee80211_vif_link_active(&sdata->vif, ch_switch->link_id))
		return 0;

	trace_drv_pre_channel_switch(local, sdata, ch_switch);
	if (local->ops->pre_channel_switch)
		ret = local->ops->pre_channel_switch(&local->hw, &sdata->vif,
						     ch_switch);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int
drv_post_channel_switch(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (!ieee80211_vif_link_active(&sdata->vif, link->link_id))
		return 0;

	trace_drv_post_channel_switch(local, sdata);
	if (local->ops->post_channel_switch)
		ret = local->ops->post_channel_switch(&local->hw, &sdata->vif,
						      link->conf);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void
drv_abort_channel_switch(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	if (!ieee80211_vif_link_active(&sdata->vif, link->link_id))
		return;

	trace_drv_abort_channel_switch(local, sdata);

	if (local->ops->abort_channel_switch)
		local->ops->abort_channel_switch(&local->hw, &sdata->vif,
						 link->conf);
}

static inline void
drv_channel_switch_rx_beacon(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_channel_switch *ch_switch)
{
	struct ieee80211_local *local = sdata->local;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	if (!ieee80211_vif_link_active(&sdata->vif, ch_switch->link_id))
		return;

	trace_drv_channel_switch_rx_beacon(local, sdata, ch_switch);
	if (local->ops->channel_switch_rx_beacon)
		local->ops->channel_switch_rx_beacon(&local->hw, &sdata->vif,
						     ch_switch);
}

static inline int drv_join_ibss(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata)
{
	int ret = 0;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_join_ibss(local, sdata, &sdata->vif.bss_conf);
	if (local->ops->join_ibss)
		ret = local->ops->join_ibss(&local->hw, &sdata->vif);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_leave_ibss(struct ieee80211_local *local,
				  struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_leave_ibss(local, sdata);
	if (local->ops->leave_ibss)
		local->ops->leave_ibss(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

static inline u32 drv_get_expected_throughput(struct ieee80211_local *local,
					      struct sta_info *sta)
{
	u32 ret = 0;

	trace_drv_get_expected_throughput(&sta->sta);
	if (local->ops->get_expected_throughput && sta->uploaded)
		ret = local->ops->get_expected_throughput(&local->hw, &sta->sta);
	trace_drv_return_u32(local, ret);

	return ret;
}

static inline int drv_get_txpower(struct ieee80211_local *local,
				  struct ieee80211_sub_if_data *sdata, int *dbm)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!local->ops->get_txpower)
		return -EOPNOTSUPP;

	ret = local->ops->get_txpower(&local->hw, &sdata->vif, dbm);
	trace_drv_get_txpower(local, sdata, *dbm, ret);

	return ret;
}

static inline int
drv_tdls_channel_switch(struct ieee80211_local *local,
			struct ieee80211_sub_if_data *sdata,
			struct ieee80211_sta *sta, u8 oper_class,
			struct cfg80211_chan_def *chandef,
			struct sk_buff *tmpl_skb, u32 ch_sw_tm_ie)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (!local->ops->tdls_channel_switch)
		return -EOPNOTSUPP;

	trace_drv_tdls_channel_switch(local, sdata, sta, oper_class, chandef);
	ret = local->ops->tdls_channel_switch(&local->hw, &sdata->vif, sta,
					      oper_class, chandef, tmpl_skb,
					      ch_sw_tm_ie);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void
drv_tdls_cancel_channel_switch(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_sta *sta)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	if (!local->ops->tdls_cancel_channel_switch)
		return;

	trace_drv_tdls_cancel_channel_switch(local, sdata, sta);
	local->ops->tdls_cancel_channel_switch(&local->hw, &sdata->vif, sta);
	trace_drv_return_void(local);
}

static inline void
drv_tdls_recv_channel_switch(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_tdls_ch_sw_params *params)
{
	trace_drv_tdls_recv_channel_switch(local, sdata, params);
	if (local->ops->tdls_recv_channel_switch)
		local->ops->tdls_recv_channel_switch(&local->hw, &sdata->vif,
						     params);
	trace_drv_return_void(local);
}

static inline void drv_wake_tx_queue(struct ieee80211_local *local,
				     struct txq_info *txq)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(txq->txq.vif);

	/* In reconfig don't transmit now, but mark for waking later */
	if (local->in_reconfig) {
		set_bit(IEEE80211_TXQ_DIRTY, &txq->flags);
		return;
	}

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_wake_tx_queue(local, sdata, txq);
	local->ops->wake_tx_queue(&local->hw, &txq->txq);
}

static inline void schedule_and_wake_txq(struct ieee80211_local *local,
					 struct txq_info *txqi)
{
	ieee80211_schedule_txq(&local->hw, &txqi->txq);
	drv_wake_tx_queue(local, txqi);
}

static inline int drv_can_aggregate_in_amsdu(struct ieee80211_local *local,
					     struct sk_buff *head,
					     struct sk_buff *skb)
{
	if (!local->ops->can_aggregate_in_amsdu)
		return true;

	return local->ops->can_aggregate_in_amsdu(&local->hw, head, skb);
}

static inline int
drv_get_ftm_responder_stats(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata,
			    struct cfg80211_ftm_responder_stats *ftm_stats)
{
	u32 ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (local->ops->get_ftm_responder_stats)
		ret = local->ops->get_ftm_responder_stats(&local->hw,
							 &sdata->vif,
							 ftm_stats);
	trace_drv_get_ftm_responder_stats(local, sdata, ftm_stats);

	return ret;
}

static inline int drv_start_pmsr(struct ieee80211_local *local,
				 struct ieee80211_sub_if_data *sdata,
				 struct cfg80211_pmsr_request *request)
{
	int ret = -EOPNOTSUPP;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_start_pmsr(local, sdata);

	if (local->ops->start_pmsr)
		ret = local->ops->start_pmsr(&local->hw, &sdata->vif, request);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_abort_pmsr(struct ieee80211_local *local,
				  struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_pmsr_request *request)
{
	trace_drv_abort_pmsr(local, sdata);

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	if (local->ops->abort_pmsr)
		local->ops->abort_pmsr(&local->hw, &sdata->vif, request);
	trace_drv_return_void(local);
}

static inline int drv_start_nan(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				struct cfg80211_nan_conf *conf)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	trace_drv_start_nan(local, sdata, conf);
	ret = local->ops->start_nan(&local->hw, &sdata->vif, conf);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_stop_nan(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	trace_drv_stop_nan(local, sdata);
	local->ops->stop_nan(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

static inline int drv_nan_change_conf(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_nan_conf *conf,
				       u32 changes)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	if (!local->ops->nan_change_conf)
		return -EOPNOTSUPP;

	trace_drv_nan_change_conf(local, sdata, conf, changes);
	ret = local->ops->nan_change_conf(&local->hw, &sdata->vif, conf,
					  changes);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline int drv_add_nan_func(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata,
				   const struct cfg80211_nan_func *nan_func)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	if (!local->ops->add_nan_func)
		return -EOPNOTSUPP;

	trace_drv_add_nan_func(local, sdata, nan_func);
	ret = local->ops->add_nan_func(&local->hw, &sdata->vif, nan_func);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_del_nan_func(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata,
				   u8 instance_id)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	trace_drv_del_nan_func(local, sdata, instance_id);
	if (local->ops->del_nan_func)
		local->ops->del_nan_func(&local->hw, &sdata->vif, instance_id);
	trace_drv_return_void(local);
}

static inline int drv_set_tid_config(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_sta *sta,
				     struct cfg80211_tid_config *tid_conf)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	ret = local->ops->set_tid_config(&local->hw, &sdata->vif, sta,
					 tid_conf);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline int drv_reset_tid_config(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_sta *sta, u8 tids)
{
	int ret;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	ret = local->ops->reset_tid_config(&local->hw, &sdata->vif, sta, tids);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_update_vif_offload(struct ieee80211_local *local,
					  struct ieee80211_sub_if_data *sdata)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	check_sdata_in_driver(sdata);

	if (!local->ops->update_vif_offload)
		return;

	trace_drv_update_vif_offload(local, sdata);
	local->ops->update_vif_offload(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

static inline void drv_sta_set_4addr(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_sta *sta, bool enabled)
{
	sdata = get_bss_sdata(sdata);

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_set_4addr(local, sdata, sta, enabled);
	if (local->ops->sta_set_4addr)
		local->ops->sta_set_4addr(&local->hw, &sdata->vif, sta, enabled);
	trace_drv_return_void(local);
}

static inline void drv_sta_set_decap_offload(struct ieee80211_local *local,
					     struct ieee80211_sub_if_data *sdata,
					     struct ieee80211_sta *sta,
					     bool enabled)
{
	sdata = get_bss_sdata(sdata);

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_sta_set_decap_offload(local, sdata, sta, enabled);
	if (local->ops->sta_set_decap_offload)
		local->ops->sta_set_decap_offload(&local->hw, &sdata->vif, sta,
						  enabled);
	trace_drv_return_void(local);
}

static inline void drv_add_twt_setup(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_sta *sta,
				     struct ieee80211_twt_setup *twt)
{
	struct ieee80211_twt_params *twt_agrt;

	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return;

	twt_agrt = (void *)twt->params;

	trace_drv_add_twt_setup(local, sta, twt, twt_agrt);
	local->ops->add_twt_setup(&local->hw, sta, twt);
	trace_drv_return_void(local);
}

static inline void drv_twt_teardown_request(struct ieee80211_local *local,
					    struct ieee80211_sub_if_data *sdata,
					    struct ieee80211_sta *sta,
					    u8 flowid)
{
	might_sleep();
	lockdep_assert_wiphy(local->hw.wiphy);
	if (!check_sdata_in_driver(sdata))
		return;

	if (!local->ops->twt_teardown_request)
		return;

	trace_drv_twt_teardown_request(local, sta, flowid);
	local->ops->twt_teardown_request(&local->hw, sta, flowid);
	trace_drv_return_void(local);
}

static inline int drv_net_fill_forward_path(struct ieee80211_local *local,
					    struct ieee80211_sub_if_data *sdata,
					    struct ieee80211_sta *sta,
					    struct net_device_path_ctx *ctx,
					    struct net_device_path *path)
{
	int ret = -EOPNOTSUPP;

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_net_fill_forward_path(local, sdata, sta);
	if (local->ops->net_fill_forward_path)
		ret = local->ops->net_fill_forward_path(&local->hw,
							&sdata->vif, sta,
							ctx, path);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline int drv_net_setup_tc(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata,
				   struct net_device *dev,
				   enum tc_setup_type type, void *type_data)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	sdata = get_bss_sdata(sdata);
	trace_drv_net_setup_tc(local, sdata, type);
	if (local->ops->net_setup_tc)
		ret = local->ops->net_setup_tc(&local->hw, &sdata->vif, dev,
					       type, type_data);
	trace_drv_return_int(local, ret);

	return ret;
}

static inline bool drv_can_activate_links(struct ieee80211_local *local,
					  struct ieee80211_sub_if_data *sdata,
					  u16 active_links)
{
	bool ret = true;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!check_sdata_in_driver(sdata))
		return false;

	trace_drv_can_activate_links(local, sdata, active_links);
	if (local->ops->can_activate_links)
		ret = local->ops->can_activate_links(&local->hw, &sdata->vif,
						     active_links);
	trace_drv_return_bool(local, ret);

	return ret;
}

int drv_change_vif_links(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 u16 old_links, u16 new_links,
			 struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS]);
int drv_change_sta_links(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_sta *sta,
			 u16 old_links, u16 new_links);

static inline enum ieee80211_neg_ttlm_res
drv_can_neg_ttlm(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_neg_ttlm *neg_ttlm)
{
	enum ieee80211_neg_ttlm_res res = NEG_TTLM_RES_REJECT;

	might_sleep();
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_can_neg_ttlm(local, sdata, neg_ttlm);
	if (local->ops->can_neg_ttlm)
		res = local->ops->can_neg_ttlm(&local->hw, &sdata->vif,
					       neg_ttlm);
	trace_drv_neg_ttlm_res(local, sdata, res, neg_ttlm);

	return res;
}

static inline void
drv_prep_add_interface(struct ieee80211_local *local,
		       enum nl80211_iftype type)
{
	trace_drv_prep_add_interface(local, type);
	if (local->ops->prep_add_interface)
		local->ops->prep_add_interface(&local->hw, type);

	trace_drv_return_void(local);
}

#endif /* __MAC80211_DRIVER_OPS */
