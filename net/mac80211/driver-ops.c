// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015 Intel Deutschland GmbH
 * Copyright (C) 2022-2023 Intel Corporation
 */
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "trace.h"
#include "driver-ops.h"
#include "debugfs_sta.h"
#include "debugfs_netdev.h"

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
		if (ret == 0) {
			sta->uploaded = true;
			if (rcu_access_pointer(sta->sta.rates))
				drv_sta_rate_tbl_update(local, sdata, &sta->sta);
		}
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		drv_sta_remove(local, sdata, &sta->sta);
	}
	trace_drv_return_int(local, ret);
	return ret;
}

__must_check
int drv_sta_set_txpwr(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata,
		      struct sta_info *sta)
{
	int ret = -EOPNOTSUPP;

	might_sleep();

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_sta_set_txpwr(local, sdata, &sta->sta);
	if (local->ops->sta_set_txpwr)
		ret = local->ops->sta_set_txpwr(&local->hw, &sdata->vif,
						&sta->sta);
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
		struct ieee80211_link_data *link, u16 ac,
		const struct ieee80211_tx_queue_params *params)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	int ret = -EOPNOTSUPP;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(link->link_id)))
		return 0;

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

	trace_drv_conf_tx(local, sdata, link->link_id, ac, params);
	if (local->ops->conf_tx)
		ret = local->ops->conf_tx(&local->hw, &sdata->vif,
					  link->link_id, ac, params);
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

int drv_assign_vif_chanctx(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_bss_conf *link_conf,
			   struct ieee80211_chanctx *ctx)
{
	int ret = 0;

	drv_verify_link_exists(sdata, link_conf);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(link_conf->link_id)))
		return 0;

	trace_drv_assign_vif_chanctx(local, sdata, link_conf, ctx);
	if (local->ops->assign_vif_chanctx) {
		WARN_ON_ONCE(!ctx->driver_present);
		ret = local->ops->assign_vif_chanctx(&local->hw,
						     &sdata->vif,
						     link_conf,
						     &ctx->conf);
	}
	trace_drv_return_int(local, ret);

	return ret;
}

void drv_unassign_vif_chanctx(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_bss_conf *link_conf,
			      struct ieee80211_chanctx *ctx)
{
	might_sleep();

	drv_verify_link_exists(sdata, link_conf);
	if (!check_sdata_in_driver(sdata))
		return;

	if (sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(link_conf->link_id)))
		return;

	trace_drv_unassign_vif_chanctx(local, sdata, link_conf, ctx);
	if (local->ops->unassign_vif_chanctx) {
		WARN_ON_ONCE(!ctx->driver_present);
		local->ops->unassign_vif_chanctx(&local->hw,
						 &sdata->vif,
						 link_conf,
						 &ctx->conf);
	}
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

	if (!sdata)
		return -EIO;

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	trace_drv_ampdu_action(local, sdata, params);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(&local->hw, &sdata->vif, params);

	trace_drv_return_int(local, ret);

	return ret;
}

void drv_link_info_changed(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_bss_conf *info,
			   int link_id, u64 changed)
{
	might_sleep();

	if (WARN_ON_ONCE(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED) &&
			 sdata->vif.type != NL80211_IFTYPE_AP &&
			 sdata->vif.type != NL80211_IFTYPE_ADHOC &&
			 sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
			 sdata->vif.type != NL80211_IFTYPE_OCB))
		return;

	if (WARN_ON_ONCE(sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE ||
			 sdata->vif.type == NL80211_IFTYPE_NAN ||
			 (sdata->vif.type == NL80211_IFTYPE_MONITOR &&
			  !sdata->vif.bss_conf.mu_mimo_owner &&
			  !(changed & BSS_CHANGED_TXPOWER))))
		return;

	if (!check_sdata_in_driver(sdata))
		return;

	if (sdata->vif.active_links &&
	    !(sdata->vif.active_links & BIT(link_id)))
		return;

	trace_drv_link_info_changed(local, sdata, info, changed);
	if (local->ops->link_info_changed)
		local->ops->link_info_changed(&local->hw, &sdata->vif,
					      info, changed);
	else if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(&local->hw, &sdata->vif,
					     info, changed);
	trace_drv_return_void(local);
}

int drv_set_key(struct ieee80211_local *local,
		enum set_key_cmd cmd,
		struct ieee80211_sub_if_data *sdata,
		struct ieee80211_sta *sta,
		struct ieee80211_key_conf *key)
{
	int ret;

	might_sleep();

	sdata = get_bss_sdata(sdata);
	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (WARN_ON(key->link_id >= 0 && sdata->vif.active_links &&
		    !(sdata->vif.active_links & BIT(key->link_id))))
		return -ENOLINK;

	trace_drv_set_key(local, cmd, sdata, sta, key);
	ret = local->ops->set_key(&local->hw, cmd, &sdata->vif, sta, key);
	trace_drv_return_int(local, ret);
	return ret;
}

int drv_change_vif_links(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 u16 old_links, u16 new_links,
			 struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct ieee80211_link_data *link;
	unsigned long links_to_add;
	unsigned long links_to_rem;
	unsigned int link_id;
	int ret = -EOPNOTSUPP;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	if (old_links == new_links)
		return 0;

	links_to_add = ~old_links & new_links;
	links_to_rem = old_links & ~new_links;

	for_each_set_bit(link_id, &links_to_rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		link = rcu_access_pointer(sdata->link[link_id]);

		ieee80211_link_debugfs_drv_remove(link);
	}

	trace_drv_change_vif_links(local, sdata, old_links, new_links);
	if (local->ops->change_vif_links)
		ret = local->ops->change_vif_links(&local->hw, &sdata->vif,
						   old_links, new_links, old);
	trace_drv_return_int(local, ret);

	if (ret)
		return ret;

	if (!local->in_reconfig) {
		for_each_set_bit(link_id, &links_to_add,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			link = rcu_access_pointer(sdata->link[link_id]);

			ieee80211_link_debugfs_drv_add(link);
		}
	}

	return 0;
}

int drv_change_sta_links(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_sta *sta,
			 u16 old_links, u16 new_links)
{
	struct sta_info *info = container_of(sta, struct sta_info, sta);
	struct link_sta_info *link_sta;
	unsigned long links_to_add;
	unsigned long links_to_rem;
	unsigned int link_id;
	int ret = -EOPNOTSUPP;

	might_sleep();

	if (!check_sdata_in_driver(sdata))
		return -EIO;

	old_links &= sdata->vif.active_links;
	new_links &= sdata->vif.active_links;

	if (old_links == new_links)
		return 0;

	links_to_add = ~old_links & new_links;
	links_to_rem = old_links & ~new_links;

	for_each_set_bit(link_id, &links_to_rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		link_sta = rcu_dereference_protected(info->link[link_id],
						     lockdep_is_held(&local->sta_mtx));

		ieee80211_link_sta_debugfs_drv_remove(link_sta);
	}

	trace_drv_change_sta_links(local, sdata, sta, old_links, new_links);
	if (local->ops->change_sta_links)
		ret = local->ops->change_sta_links(&local->hw, &sdata->vif, sta,
						   old_links, new_links);
	trace_drv_return_int(local, ret);

	if (ret)
		return ret;

	/* during reconfig don't add it to debugfs again */
	if (local->in_reconfig)
		return 0;

	for_each_set_bit(link_id, &links_to_add, IEEE80211_MLD_MAX_NUM_LINKS) {
		link_sta = rcu_dereference_protected(info->link[link_id],
						     lockdep_is_held(&local->sta_mtx));
		ieee80211_link_sta_debugfs_drv_add(link_sta);
	}

	return 0;
}
