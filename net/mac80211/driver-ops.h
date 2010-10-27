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

	might_sleep();

	trace_drv_start(local);
	local->started = true;
	smp_mb();
	ret = local->ops->start(&local->hw);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_stop(struct ieee80211_local *local)
{
	might_sleep();

	trace_drv_stop(local);
	local->ops->stop(&local->hw);
	trace_drv_return_void(local);

	/* sync away all work on the tasklet before clearing started */
	tasklet_disable(&local->tasklet);
	tasklet_enable(&local->tasklet);

	barrier();

	local->started = false;
}

static inline int drv_add_interface(struct ieee80211_local *local,
				    struct ieee80211_vif *vif)
{
	int ret;

	might_sleep();

	trace_drv_add_interface(local, vif_to_sdata(vif));
	ret = local->ops->add_interface(&local->hw, vif);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_remove_interface(struct ieee80211_local *local,
					struct ieee80211_vif *vif)
{
	might_sleep();

	trace_drv_remove_interface(local, vif_to_sdata(vif));
	local->ops->remove_interface(&local->hw, vif);
	trace_drv_return_void(local);
}

static inline int drv_config(struct ieee80211_local *local, u32 changed)
{
	int ret;

	might_sleep();

	trace_drv_config(local, changed);
	ret = local->ops->config(&local->hw, changed);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_bss_info_changed(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					struct ieee80211_bss_conf *info,
					u32 changed)
{
	might_sleep();

	trace_drv_bss_info_changed(local, sdata, info, changed);
	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, &sdata->vif, info, changed);
	trace_drv_return_void(local);
}

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

	trace_drv_configure_filter(local, changed_flags, total_flags,
				   multicast);
	local->ops->configure_filter(&local->hw, changed_flags, total_flags,
				     multicast);
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

static inline int drv_set_key(struct ieee80211_local *local,
			      enum set_key_cmd cmd,
			      struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key)
{
	int ret;

	might_sleep();

	trace_drv_set_key(local, cmd, sdata, sta, key);
	ret = local->ops->set_key(&local->hw, cmd, &sdata->vif, sta, key);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_update_tkip_key(struct ieee80211_local *local,
				       struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_key_conf *conf,
				       struct sta_info *sta, u32 iv32,
				       u16 *phase1key)
{
	struct ieee80211_sta *ista = NULL;

	if (sta)
		ista = &sta->sta;

	trace_drv_update_tkip_key(local, sdata, conf, ista, iv32);
	if (local->ops->update_tkip_key)
		local->ops->update_tkip_key(&local->hw, &sdata->vif, conf,
					    ista, iv32, phase1key);
	trace_drv_return_void(local);
}

static inline int drv_hw_scan(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_scan_request *req)
{
	int ret;

	might_sleep();

	trace_drv_hw_scan(local, sdata, req);
	ret = local->ops->hw_scan(&local->hw, &sdata->vif, req);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline void drv_sw_scan_start(struct ieee80211_local *local)
{
	might_sleep();

	trace_drv_sw_scan_start(local);
	if (local->ops->sw_scan_start)
		local->ops->sw_scan_start(&local->hw);
	trace_drv_return_void(local);
}

static inline void drv_sw_scan_complete(struct ieee80211_local *local)
{
	might_sleep();

	trace_drv_sw_scan_complete(local);
	if (local->ops->sw_scan_complete)
		local->ops->sw_scan_complete(&local->hw);
	trace_drv_return_void(local);
}

static inline int drv_get_stats(struct ieee80211_local *local,
				struct ieee80211_low_level_stats *stats)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

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

	might_sleep();

	trace_drv_set_rts_threshold(local, value);
	if (local->ops->set_rts_threshold)
		ret = local->ops->set_rts_threshold(&local->hw, value);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_set_coverage_class(struct ieee80211_local *local,
					 u8 value)
{
	int ret = 0;
	might_sleep();

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

	trace_drv_sta_remove(local, sdata, sta);
	if (local->ops->sta_remove)
		local->ops->sta_remove(&local->hw, &sdata->vif, sta);

	trace_drv_return_void(local);
}

static inline int drv_conf_tx(struct ieee80211_local *local, u16 queue,
			      const struct ieee80211_tx_queue_params *params)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	trace_drv_conf_tx(local, queue, params);
	if (local->ops->conf_tx)
		ret = local->ops->conf_tx(&local->hw, queue, params);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline u64 drv_get_tsf(struct ieee80211_local *local)
{
	u64 ret = -1ULL;

	might_sleep();

	trace_drv_get_tsf(local);
	if (local->ops->get_tsf)
		ret = local->ops->get_tsf(&local->hw);
	trace_drv_return_u64(local, ret);
	return ret;
}

static inline void drv_set_tsf(struct ieee80211_local *local, u64 tsf)
{
	might_sleep();

	trace_drv_set_tsf(local, tsf);
	if (local->ops->set_tsf)
		local->ops->set_tsf(&local->hw, tsf);
	trace_drv_return_void(local);
}

static inline void drv_reset_tsf(struct ieee80211_local *local)
{
	might_sleep();

	trace_drv_reset_tsf(local);
	if (local->ops->reset_tsf)
		local->ops->reset_tsf(&local->hw);
	trace_drv_return_void(local);
}

static inline int drv_tx_last_beacon(struct ieee80211_local *local)
{
	int ret = 1;

	might_sleep();

	trace_drv_tx_last_beacon(local);
	if (local->ops->tx_last_beacon)
		ret = local->ops->tx_last_beacon(&local->hw);
	trace_drv_return_int(local, ret);
	return ret;
}

static inline int drv_ampdu_action(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata,
				   enum ieee80211_ampdu_mlme_action action,
				   struct ieee80211_sta *sta, u16 tid,
				   u16 *ssn)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	trace_drv_ampdu_action(local, sdata, action, sta, tid, ssn);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(&local->hw, &sdata->vif, action,
					       sta, tid, ssn);

	trace_drv_return_int(local, ret);

	return ret;
}

static inline int drv_get_survey(struct ieee80211_local *local, int idx,
				struct survey_info *survey)
{
	int ret = -EOPNOTSUPP;

	trace_drv_get_survey(local, idx, survey);

	if (local->ops->get_survey)
		ret = local->ops->get_survey(&local->hw, idx, survey);

	trace_drv_return_int(local, ret);

	return ret;
}

static inline void drv_rfkill_poll(struct ieee80211_local *local)
{
	might_sleep();

	if (local->ops->rfkill_poll)
		local->ops->rfkill_poll(&local->hw);
}

static inline void drv_flush(struct ieee80211_local *local, bool drop)
{
	might_sleep();

	trace_drv_flush(local, drop);
	if (local->ops->flush)
		local->ops->flush(&local->hw, drop);
	trace_drv_return_void(local);
}

static inline void drv_channel_switch(struct ieee80211_local *local,
				     struct ieee80211_channel_switch *ch_switch)
{
	might_sleep();

	trace_drv_channel_switch(local, ch_switch);
	local->ops->channel_switch(&local->hw, ch_switch);
	trace_drv_return_void(local);
}

#endif /* __MAC80211_DRIVER_OPS */
