/*
 * Copyright 2015 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "trace.h"
#include "driver-ops.h"

int drv_start(struct ieee80211_local *local)
{
	int ret;

	might_sleep();

	if (WARN_ON(local->started))
		return -EALREADY;

	trace_drv_start(local);
	local->started = true;
	/* allow rx frames */
	smp_mb();
	ret = local->ops->start(&local->hw);
	trace_drv_return_int(local, ret);

	if (ret)
		local->started = false;

	return ret;
}

void drv_stop(struct ieee80211_local *local)
{
	might_sleep();

	if (WARN_ON(!local->started))
		return;

	trace_drv_stop(local);
	local->ops->stop(&local->hw);
	trace_drv_return_void(local);

	/* sync away all work on the tasklet before clearing started */
	tasklet_disable(&local->tasklet);
	tasklet_enable(&local->tasklet);

	barrier();

	local->started = false;
}

int drv_add_interface(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata)
{
	int ret;

	might_sleep();

	if (WARN_ON(sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		    (sdata->vif.type == NL80211_IFTYPE_MONITOR &&
		     !ieee80211_hw_check(&local->hw, WANT_MONITOR_VIF) &&
		     !(sdata->u.mntr.flags & MONITOR_FLAG_ACTIVE))))
		return -EINVAL;

	trace_drv_add_interface(local, sdata);
	ret = local->ops->add_interface(&local->hw, &sdata->vif);
	trace_drv_return_int(local, ret);

	if (ret == 0)
		sdata->flags |= IEEE80211_SDATA_IN_DRIVER;

	return ret;
}

int drv_change_interface(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 enum nl80211_iftype type, bool p2p)
{
	int ret;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_change_interface(local, sdata, type, p2p);
	ret = local->ops->change_interface(&local->hw, &sdata->vif, type, p2p);
	trace_drv_return_int(local, ret);
	return ret;
}

void drv_remove_interface(struct ieee80211_local *local,
			  struct ieee80211_sub_if_data *sdata)
{
	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_remove_interface(local, sdata);
	local->ops->remove_interface(&local->hw, &sdata->vif);
	sdata->flags &= ~IEEE80211_SDATA_IN_DRIVER;
	trace_drv_return_void(local);
}

__must_check
int drv_sta_state(struct ieee80211_local *local,
		  struct ieee80211_sub_if_data *sdata,
		  struct sta_info *sta,
		  enum ieee80211_sta_state old_state,
		  enum ieee80211_sta_state new_state)
{
	int ret = 0;

	might_sleep();

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_sta_state(local, sdata, &sta->sta, old_state, new_state);
	if (local->ops->sta_state) {
		ret = local->ops->sta_state(&local->hw, &sdata->vif, &sta->sta,
					    old_state, new_state);
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC) {
		ret = drv_sta_add(local, sdata, &sta->sta);
		if (ret == 0)
			sta->uploaded = true;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		drv_sta_remove(local, sdata, &sta->sta);
	}
	trace_drv_return_int(local, ret);
	return ret;
}

void drv_sta_rc_update(struct ieee80211_local *local,
		       struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_sta *sta, u32 changed)
{
	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return;

	WARN_ON(changed & IEEE80211_RC_SUPP_RATES_CHANGED &&
		(sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		 sdata->vif.type != NL80211_IFTYPE_MESH_POINT));

	trace_drv_sta_rc_update(local, sdata, sta, changed);
	if (local->ops->sta_rc_update)
		local->ops->sta_rc_update(&local->hw, &sdata->vif,
					  sta, changed);

	trace_drv_return_void(local);
}

int drv_conf_tx(struct ieee80211_local *local,
		struct ieee80211_sub_if_data *sdata, u16 ac,
		const struct ieee80211_tx_queue_params *params)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (params->cw_min == 0 || params->cw_min > params->cw_max) {
		/*
		 * If we can't configure hardware anyway, don't warn. We may
		 * never have initialized the CW parameters.
		 */
		WARN_ONCE(local->ops->conf_tx,
			  "%s: invalid CW_min/CW_max: %d/%d\n",
			  sdata->name, params->cw_min, params->cw_max);
		return -EINVAL;
	}

	trace_drv_conf_tx(local, sdata, ac, params);
	if (local->ops->conf_tx)
		ret = local->ops->conf_tx(&local->hw, &sdata->vif,
					  ac, params);
	trace_drv_return_int(local, ret);
	return ret;
}

u64 drv_get_tsf(struct ieee80211_local *local,
		struct ieee80211_sub_if_data *sdata)
{
	u64 ret = -1ULL;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return ret;

	trace_drv_get_tsf(local, sdata);
	if (local->ops->get_tsf)
		ret = local->ops->get_tsf(&local->hw, &sdata->vif);
	trace_drv_return_u64(local, ret);
	return ret;
}

void drv_set_tsf(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u64 tsf)
{
	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_set_tsf(local, sdata, tsf);
	if (local->ops->set_tsf)
		local->ops->set_tsf(&local->hw, &sdata->vif, tsf);
	trace_drv_return_void(local);
}

void drv_offset_tsf(struct ieee80211_local *local,
		    struct ieee80211_sub_if_data *sdata,
		    s64 offset)
{
	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_offset_tsf(local, sdata, offset);
	if (local->ops->offset_tsf)
		local->ops->offset_tsf(&local->hw, &sdata->vif, offset);
	trace_drv_return_void(local);
}

void drv_reset_tsf(struct ieee80211_local *local,
		   struct ieee80211_sub_if_data *sdata)
{
	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return;

	trace_drv_reset_tsf(local, sdata);
	if (local->ops->reset_tsf)
		local->ops->reset_tsf(&local->hw, &sdata->vif);
	trace_drv_return_void(local);
}

int drv_switch_vif_chanctx(struct ieee80211_local *local,
			   struct ieee80211_vif_chanctx_switch *vifs,
			   int n_vifs, enum ieee80211_chanctx_switch_mode mode)
{
	int ret = 0;
	int i;

	might_sleep();

	if (!local->ops->switch_vif_chanctx)
		return -EOPNOTSUPP;

	for (i = 0; i < n_vifs; i++) {
		struct ieee80211_chanctx *new_ctx =
			container_of(vifs[i].new_ctx,
				     struct ieee80211_chanctx,
				     conf);
		struct ieee80211_chanctx *old_ctx =
			container_of(vifs[i].old_ctx,
				     struct ieee80211_chanctx,
				     conf);

		WARN_ON_ONCE(!old_ctx->driver_present);
		WARN_ON_ONCE((mode == CHANCTX_SWMODE_SWAP_CONTEXTS &&
			      new_ctx->driver_present) ||
			     (mode == CHANCTX_SWMODE_REASSIGN_VIF &&
			      !new_ctx->driver_present));
	}

	trace_drv_switch_vif_chanctx(local, vifs, n_vifs, mode);
	ret = local->ops->switch_vif_chanctx(&local->hw,
					     vifs, n_vifs, mode);
	trace_drv_return_int(local, ret);

	if (!ret && mode == CHANCTX_SWMODE_SWAP_CONTEXTS) {
		for (i = 0; i < n_vifs; i++) {
			struct ieee80211_chanctx *new_ctx =
				container_of(vifs[i].new_ctx,
					     struct ieee80211_chanctx,
					     conf);
			struct ieee80211_chanctx *old_ctx =
				container_of(vifs[i].old_ctx,
					     struct ieee80211_chanctx,
					     conf);

			new_ctx->driver_present = true;
			old_ctx->driver_present = false;
		}
	}

	return ret;
}

int drv_ampdu_action(struct ieee80211_local *local,
		     struct ieee80211_sub_if_data *sdata,
		     struct ieee80211_ampdu_params *params)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_ampdu_action(local, sdata, params);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(&local->hw, &sdata->vif, params);

	trace_drv_return_int(local, ret);

	return ret;
}
