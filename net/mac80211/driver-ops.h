#ifndef __MAC80211_DRIVER_OPS
#define __MAC80211_DRIVER_OPS

#include <net/mac80211.h>
#include "ieee80211_i.h"

static inline int drv_tx(struct ieee80211_local *local, struct sk_buff *skb)
{
	return local->ops->tx(&local->hw, skb);
}

static inline int drv_start(struct ieee80211_local *local)
{
	return local->ops->start(&local->hw);
}

static inline void drv_stop(struct ieee80211_local *local)
{
	local->ops->stop(&local->hw);
}

static inline int drv_add_interface(struct ieee80211_local *local,
				    struct ieee80211_if_init_conf *conf)
{
	return local->ops->add_interface(&local->hw, conf);
}

static inline void drv_remove_interface(struct ieee80211_local *local,
					struct ieee80211_if_init_conf *conf)
{
	local->ops->remove_interface(&local->hw, conf);
}

static inline int drv_config(struct ieee80211_local *local, u32 changed)
{
	return local->ops->config(&local->hw, changed);
}

static inline void drv_bss_info_changed(struct ieee80211_local *local,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *info,
					u32 changed)
{
	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, vif, info, changed);
}

static inline void drv_configure_filter(struct ieee80211_local *local,
					unsigned int changed_flags,
					unsigned int *total_flags,
					int mc_count,
					struct dev_addr_list *mc_list)
{
	local->ops->configure_filter(&local->hw, changed_flags, total_flags,
				     mc_count, mc_list);
}

static inline int drv_set_tim(struct ieee80211_local *local,
			      struct ieee80211_sta *sta, bool set)
{
	if (local->ops->set_tim)
		return local->ops->set_tim(&local->hw, sta, set);
	return 0;
}

static inline int drv_set_key(struct ieee80211_local *local,
			      enum set_key_cmd cmd, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key)
{
	return local->ops->set_key(&local->hw, cmd, vif, sta, key);
}

static inline void drv_update_tkip_key(struct ieee80211_local *local,
				       struct ieee80211_key_conf *conf,
				       const u8 *address, u32 iv32,
				       u16 *phase1key)
{
	if (local->ops->update_tkip_key)
		local->ops->update_tkip_key(&local->hw, conf, address,
					    iv32, phase1key);
}

static inline int drv_hw_scan(struct ieee80211_local *local,
			      struct cfg80211_scan_request *req)
{
	return local->ops->hw_scan(&local->hw, req);
}

static inline void drv_sw_scan_start(struct ieee80211_local *local)
{
	if (local->ops->sw_scan_start)
		local->ops->sw_scan_start(&local->hw);
}

static inline void drv_sw_scan_complete(struct ieee80211_local *local)
{
	if (local->ops->sw_scan_complete)
		local->ops->sw_scan_complete(&local->hw);
}

static inline int drv_get_stats(struct ieee80211_local *local,
				struct ieee80211_low_level_stats *stats)
{
	if (!local->ops->get_stats)
		return -EOPNOTSUPP;
	return local->ops->get_stats(&local->hw, stats);
}

static inline void drv_get_tkip_seq(struct ieee80211_local *local,
				    u8 hw_key_idx, u32 *iv32, u16 *iv16)
{
	if (local->ops->get_tkip_seq)
		local->ops->get_tkip_seq(&local->hw, hw_key_idx, iv32, iv16);
}

static inline int drv_set_rts_threshold(struct ieee80211_local *local,
					u32 value)
{
	if (local->ops->set_rts_threshold)
		return local->ops->set_rts_threshold(&local->hw, value);
	return 0;
}

static inline void drv_sta_notify(struct ieee80211_local *local,
				  struct ieee80211_vif *vif,
				  enum sta_notify_cmd cmd,
				  struct ieee80211_sta *sta)
{
	if (local->ops->sta_notify)
		local->ops->sta_notify(&local->hw, vif, cmd, sta);
}

static inline int drv_conf_tx(struct ieee80211_local *local, u16 queue,
			      const struct ieee80211_tx_queue_params *params)
{
	if (local->ops->conf_tx)
		return local->ops->conf_tx(&local->hw, queue, params);
	return -EOPNOTSUPP;
}

static inline int drv_get_tx_stats(struct ieee80211_local *local,
				   struct ieee80211_tx_queue_stats *stats)
{
	return local->ops->get_tx_stats(&local->hw, stats);
}

static inline u64 drv_get_tsf(struct ieee80211_local *local)
{
	if (local->ops->get_tsf)
		return local->ops->get_tsf(&local->hw);
	return -1ULL;
}

static inline void drv_set_tsf(struct ieee80211_local *local, u64 tsf)
{
	if (local->ops->set_tsf)
		local->ops->set_tsf(&local->hw, tsf);
}

static inline void drv_reset_tsf(struct ieee80211_local *local)
{
	if (local->ops->reset_tsf)
		local->ops->reset_tsf(&local->hw);
}

static inline int drv_tx_last_beacon(struct ieee80211_local *local)
{
	if (local->ops->tx_last_beacon)
		return local->ops->tx_last_beacon(&local->hw);
	return 1;
}

static inline int drv_ampdu_action(struct ieee80211_local *local,
				   enum ieee80211_ampdu_mlme_action action,
				   struct ieee80211_sta *sta, u16 tid,
				   u16 *ssn)
{
	if (local->ops->ampdu_action)
		return local->ops->ampdu_action(&local->hw, action,
						sta, tid, ssn);
	return -EOPNOTSUPP;
}


static inline void drv_rfkill_poll(struct ieee80211_local *local)
{
	if (local->ops->rfkill_poll)
		local->ops->rfkill_poll(&local->hw);
}
#endif /* __MAC80211_DRIVER_OPS */
