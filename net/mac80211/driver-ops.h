#ifndef __MAC80211_DRIVER_OPS
#define __MAC80211_DRIVER_OPS

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-trace.h"

static inline int drv_tx(struct ieee80211_local *local, struct sk_buff *skb)
{
	return local->ops->tx(&local->hw, skb);
}

static inline int drv_start(struct ieee80211_local *local)
{
	int ret;

	local->started = true;
	smp_mb();
	ret = local->ops->start(&local->hw);
	trace_drv_start(local, ret);
	return ret;
}

static inline void drv_stop(struct ieee80211_local *local)
{
	local->ops->stop(&local->hw);
	trace_drv_stop(local);

	/* sync away all work on the tasklet before clearing started */
	tasklet_disable(&local->tasklet);
	tasklet_enable(&local->tasklet);

	barrier();

	local->started = false;
}

static inline int drv_add_interface(struct ieee80211_local *local,
				    struct ieee80211_if_init_conf *conf)
{
	int ret = local->ops->add_interface(&local->hw, conf);
	trace_drv_add_interface(local, conf->mac_addr, conf->vif, ret);
	return ret;
}

static inline void drv_remove_interface(struct ieee80211_local *local,
					struct ieee80211_if_init_conf *conf)
{
	local->ops->remove_interface(&local->hw, conf);
	trace_drv_remove_interface(local, conf->mac_addr, conf->vif);
}

static inline int drv_config(struct ieee80211_local *local, u32 changed)
{
	int ret = local->ops->config(&local->hw, changed);
	trace_drv_config(local, changed, ret);
	return ret;
}

static inline void drv_bss_info_changed(struct ieee80211_local *local,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *info,
					u32 changed)
{
	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, vif, info, changed);
	trace_drv_bss_info_changed(local, vif, info, changed);
}

static inline u64 drv_prepare_multicast(struct ieee80211_local *local,
					int mc_count,
					struct dev_addr_list *mc_list)
{
	u64 ret = 0;

	if (local->ops->prepare_multicast)
		ret = local->ops->prepare_multicast(&local->hw, mc_count,
						    mc_list);

	trace_drv_prepare_multicast(local, mc_count, ret);

	return ret;
}

static inline void drv_configure_filter(struct ieee80211_local *local,
					unsigned int changed_flags,
					unsigned int *total_flags,
					u64 multicast)
{
	might_sleep();

	local->ops->configure_filter(&local->hw, changed_flags, total_flags,
				     multicast);
	trace_drv_configure_filter(local, changed_flags, total_flags,
				   multicast);
}

static inline int drv_set_tim(struct ieee80211_local *local,
			      struct ieee80211_sta *sta, bool set)
{
	int ret = 0;
	if (local->ops->set_tim)
		ret = local->ops->set_tim(&local->hw, sta, set);
	trace_drv_set_tim(local, sta, set, ret);
	return ret;
}

static inline int drv_set_key(struct ieee80211_local *local,
			      enum set_key_cmd cmd, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key)
{
	int ret = local->ops->set_key(&local->hw, cmd, vif, sta, key);
	trace_drv_set_key(local, cmd, vif, sta, key, ret);
	return ret;
}

static inline void drv_update_tkip_key(struct ieee80211_local *local,
				       struct ieee80211_key_conf *conf,
				       const u8 *address, u32 iv32,
				       u16 *phase1key)
{
	if (local->ops->update_tkip_key)
		local->ops->update_tkip_key(&local->hw, conf, address,
					    iv32, phase1key);
	trace_drv_update_tkip_key(local, conf, address, iv32);
}

static inline int drv_hw_scan(struct ieee80211_local *local,
			      struct cfg80211_scan_request *req)
{
	int ret = local->ops->hw_scan(&local->hw, req);
	trace_drv_hw_scan(local, req, ret);
	return ret;
}

static inline void drv_sw_scan_start(struct ieee80211_local *local)
{
	if (local->ops->sw_scan_start)
		local->ops->sw_scan_start(&local->hw);
	trace_drv_sw_scan_start(local);
}

static inline void drv_sw_scan_complete(struct ieee80211_local *local)
{
	if (local->ops->sw_scan_complete)
		local->ops->sw_scan_complete(&local->hw);
	trace_drv_sw_scan_complete(local);
}

static inline int drv_get_stats(struct ieee80211_local *local,
				struct ieee80211_low_level_stats *stats)
{
	int ret = -EOPNOTSUPP;

	if (local->ops->get_stats)
		ret = local->ops->get_stats(&local->hw, stats);
	trace_drv_get_stats(local, stats, ret);

	return ret;
}

static inline void drv_get_tkip_seq(struct ieee80211_local *local,
				    u8 hw_key_idx, u32 *iv32, u16 *iv16)
{
	if (local->ops->get_tkip_seq)
		local->ops->get_tkip_seq(&local->hw, hw_key_idx, iv32, iv16);
	trace_drv_get_tkip_seq(local, hw_key_idx, iv32, iv16);
}

static inline int drv_set_rts_threshold(struct ieee80211_local *local,
					u32 value)
{
	int ret = 0;
	if (local->ops->set_rts_threshold)
		ret = local->ops->set_rts_threshold(&local->hw, value);
	trace_drv_set_rts_threshold(local, value, ret);
	return ret;
}

static inline void drv_sta_notify(struct ieee80211_local *local,
				  struct ieee80211_vif *vif,
				  enum sta_notify_cmd cmd,
				  struct ieee80211_sta *sta)
{
	if (local->ops->sta_notify)
		local->ops->sta_notify(&local->hw, vif, cmd, sta);
	trace_drv_sta_notify(local, vif, cmd, sta);
}

static inline int drv_conf_tx(struct ieee80211_local *local, u16 queue,
			      const struct ieee80211_tx_queue_params *params)
{
	int ret = -EOPNOTSUPP;
	if (local->ops->conf_tx)
		ret = local->ops->conf_tx(&local->hw, queue, params);
	trace_drv_conf_tx(local, queue, params, ret);
	return ret;
}

static inline int drv_get_tx_stats(struct ieee80211_local *local,
				   struct ieee80211_tx_queue_stats *stats)
{
	int ret = local->ops->get_tx_stats(&local->hw, stats);
	trace_drv_get_tx_stats(local, stats, ret);
	return ret;
}

static inline u64 drv_get_tsf(struct ieee80211_local *local)
{
	u64 ret = -1ULL;
	if (local->ops->get_tsf)
		ret = local->ops->get_tsf(&local->hw);
	trace_drv_get_tsf(local, ret);
	return ret;
}

static inline void drv_set_tsf(struct ieee80211_local *local, u64 tsf)
{
	if (local->ops->set_tsf)
		local->ops->set_tsf(&local->hw, tsf);
	trace_drv_set_tsf(local, tsf);
}

static inline void drv_reset_tsf(struct ieee80211_local *local)
{
	if (local->ops->reset_tsf)
		local->ops->reset_tsf(&local->hw);
	trace_drv_reset_tsf(local);
}

static inline int drv_tx_last_beacon(struct ieee80211_local *local)
{
	int ret = 1;
	if (local->ops->tx_last_beacon)
		ret = local->ops->tx_last_beacon(&local->hw);
	trace_drv_tx_last_beacon(local, ret);
	return ret;
}

static inline int drv_ampdu_action(struct ieee80211_local *local,
				   enum ieee80211_ampdu_mlme_action action,
				   struct ieee80211_sta *sta, u16 tid,
				   u16 *ssn)
{
	int ret = -EOPNOTSUPP;
	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(&local->hw, action,
					       sta, tid, ssn);
	trace_drv_ampdu_action(local, action, sta, tid, ssn, ret);
	return ret;
}


static inline void drv_rfkill_poll(struct ieee80211_local *local)
{
	if (local->ops->rfkill_poll)
		local->ops->rfkill_poll(&local->hw);
}
#endif /* __MAC80211_DRIVER_OPS */
