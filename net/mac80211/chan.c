// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include <linux/export.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

static int ieee80211_chanctx_num_assigned(struct ieee80211_local *local,
					  struct ieee80211_chanctx *ctx)
{
	struct ieee80211_sub_if_data *sdata;
	int num = 0;

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(sdata, &ctx->assigned_vifs, assigned_chanctx_list)
		num++;

	return num;
}

static int ieee80211_chanctx_num_reserved(struct ieee80211_local *local,
					  struct ieee80211_chanctx *ctx)
{
	struct ieee80211_sub_if_data *sdata;
	int num = 0;

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(sdata, &ctx->reserved_vifs, reserved_chanctx_list)
		num++;

	return num;
}

int ieee80211_chanctx_refcount(struct ieee80211_local *local,
			       struct ieee80211_chanctx *ctx)
{
	return ieee80211_chanctx_num_assigned(local, ctx) +
	       ieee80211_chanctx_num_reserved(local, ctx);
}

static int ieee80211_num_chanctx(struct ieee80211_local *local)
{
	struct ieee80211_chanctx *ctx;
	int num = 0;

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(ctx, &local->chanctx_list, list)
		num++;

	return num;
}

static bool ieee80211_can_create_new_chanctx(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->chanctx_mtx);
	return ieee80211_num_chanctx(local) < ieee80211_max_num_channels(local);
}

static struct ieee80211_chanctx *
ieee80211_vif_get_chanctx(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local __maybe_unused = sdata->local;
	struct ieee80211_chanctx_conf *conf;

	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf)
		return NULL;

	return container_of(conf, struct ieee80211_chanctx, conf);
}

static const struct cfg80211_chan_def *
ieee80211_chanctx_reserved_chandef(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx,
				   const struct cfg80211_chan_def *compat)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(sdata, &ctx->reserved_vifs,
			    reserved_chanctx_list) {
		if (!compat)
			compat = &sdata->reserved_chandef;

		compat = cfg80211_chandef_compatible(&sdata->reserved_chandef,
						     compat);
		if (!compat)
			break;
	}

	return compat;
}

static const struct cfg80211_chan_def *
ieee80211_chanctx_non_reserved_chandef(struct ieee80211_local *local,
				       struct ieee80211_chanctx *ctx,
				       const struct cfg80211_chan_def *compat)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(sdata, &ctx->assigned_vifs,
			    assigned_chanctx_list) {
		if (sdata->reserved_chanctx != NULL)
			continue;

		if (!compat)
			compat = &sdata->vif.bss_conf.chandef;

		compat = cfg80211_chandef_compatible(
				&sdata->vif.bss_conf.chandef, compat);
		if (!compat)
			break;
	}

	return compat;
}

static const struct cfg80211_chan_def *
ieee80211_chanctx_combined_chandef(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx,
				   const struct cfg80211_chan_def *compat)
{
	lockdep_assert_held(&local->chanctx_mtx);

	compat = ieee80211_chanctx_reserved_chandef(local, ctx, compat);
	if (!compat)
		return NULL;

	compat = ieee80211_chanctx_non_reserved_chandef(local, ctx, compat);
	if (!compat)
		return NULL;

	return compat;
}

static bool
ieee80211_chanctx_can_reserve_chandef(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      const struct cfg80211_chan_def *def)
{
	lockdep_assert_held(&local->chanctx_mtx);

	if (ieee80211_chanctx_combined_chandef(local, ctx, def))
		return true;

	if (!list_empty(&ctx->reserved_vifs) &&
	    ieee80211_chanctx_reserved_chandef(local, ctx, def))
		return true;

	return false;
}

static struct ieee80211_chanctx *
ieee80211_find_reservation_chanctx(struct ieee80211_local *local,
				   const struct cfg80211_chan_def *chandef,
				   enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;

		if (!ieee80211_chanctx_can_reserve_chandef(local, ctx,
							   chandef))
			continue;

		return ctx;
	}

	return NULL;
}

enum nl80211_chan_width ieee80211_get_sta_bw(struct ieee80211_sta *sta)
{
	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_20:
		if (sta->ht_cap.ht_supported)
			return NL80211_CHAN_WIDTH_20;
		else
			return NL80211_CHAN_WIDTH_20_NOHT;
	case IEEE80211_STA_RX_BW_40:
		return NL80211_CHAN_WIDTH_40;
	case IEEE80211_STA_RX_BW_80:
		return NL80211_CHAN_WIDTH_80;
	case IEEE80211_STA_RX_BW_160:
		/*
		 * This applied for both 160 and 80+80. since we use
		 * the returned value to consider degradation of
		 * ctx->conf.min_def, we have to make sure to take
		 * the bigger one (NL80211_CHAN_WIDTH_160).
		 * Otherwise we might try degrading even when not
		 * needed, as the max required sta_bw returned (80+80)
		 * might be smaller than the configured bw (160).
		 */
		return NL80211_CHAN_WIDTH_160;
	default:
		WARN_ON(1);
		return NL80211_CHAN_WIDTH_20;
	}
}

static enum nl80211_chan_width
ieee80211_get_max_required_bw(struct ieee80211_sub_if_data *sdata)
{
	enum nl80211_chan_width max_bw = NL80211_CHAN_WIDTH_20_NOHT;
	struct sta_info *sta;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &sdata->local->sta_list, list) {
		if (sdata != sta->sdata &&
		    !(sta->sdata->bss && sta->sdata->bss == sdata->bss))
			continue;

		max_bw = max(max_bw, ieee80211_get_sta_bw(&sta->sta));
	}
	rcu_read_unlock();

	return max_bw;
}

static enum nl80211_chan_width
ieee80211_get_chanctx_max_required_bw(struct ieee80211_local *local,
				      struct ieee80211_chanctx_conf *conf)
{
	struct ieee80211_sub_if_data *sdata;
	enum nl80211_chan_width max_bw = NL80211_CHAN_WIDTH_20_NOHT;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		struct ieee80211_vif *vif = &sdata->vif;
		enum nl80211_chan_width width = NL80211_CHAN_WIDTH_20_NOHT;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (rcu_access_pointer(sdata->vif.chanctx_conf) != conf)
			continue;

		switch (vif->type) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_AP_VLAN:
			width = ieee80211_get_max_required_bw(sdata);
			break;
		case NL80211_IFTYPE_STATION:
			/*
			 * The ap's sta->bandwidth is not set yet at this
			 * point, so take the width from the chandef, but
			 * account also for TDLS peers
			 */
			width = max(vif->bss_conf.chandef.width,
				    ieee80211_get_max_required_bw(sdata));
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
		case NL80211_IFTYPE_NAN:
			continue;
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_OCB:
			width = vif->bss_conf.chandef.width;
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
			WARN_ON_ONCE(1);
		}
		max_bw = max(max_bw, width);
	}

	/* use the configured bandwidth in case of monitor interface */
	sdata = rcu_dereference(local->monitor_sdata);
	if (sdata && rcu_access_pointer(sdata->vif.chanctx_conf) == conf)
		max_bw = max(max_bw, conf->def.width);

	rcu_read_unlock();

	return max_bw;
}

/*
 * recalc the min required chan width of the channel context, which is
 * the max of min required widths of all the interfaces bound to this
 * channel context.
 */
void ieee80211_recalc_chanctx_min_def(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx)
{
	enum nl80211_chan_width max_bw;
	struct cfg80211_chan_def min_def;

	lockdep_assert_held(&local->chanctx_mtx);

	/* don't optimize 5MHz, 10MHz, and radar_enabled confs */
	if (ctx->conf.def.width == NL80211_CHAN_WIDTH_5 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_10 ||
	    ctx->conf.radar_enabled) {
		ctx->conf.min_def = ctx->conf.def;
		return;
	}

	max_bw = ieee80211_get_chanctx_max_required_bw(local, &ctx->conf);

	/* downgrade chandef up to max_bw */
	min_def = ctx->conf.def;
	while (min_def.width > max_bw)
		ieee80211_chandef_downgrade(&min_def);

	if (cfg80211_chandef_identical(&ctx->conf.min_def, &min_def))
		return;

	ctx->conf.min_def = min_def;
	if (!ctx->driver_present)
		return;

	drv_change_chanctx(local, ctx, IEEE80211_CHANCTX_CHANGE_MIN_WIDTH);
}

static void ieee80211_change_chanctx(struct ieee80211_local *local,
				     struct ieee80211_chanctx *ctx,
				     const struct cfg80211_chan_def *chandef)
{
	if (cfg80211_chandef_identical(&ctx->conf.def, chandef)) {
		ieee80211_recalc_chanctx_min_def(local, ctx);
		return;
	}

	WARN_ON(!cfg80211_chandef_compatible(&ctx->conf.def, chandef));

	ctx->conf.def = *chandef;
	drv_change_chanctx(local, ctx, IEEE80211_CHANCTX_CHANGE_WIDTH);
	ieee80211_recalc_chanctx_min_def(local, ctx);

	if (!local->use_chanctx) {
		local->_oper_chandef = *chandef;
		ieee80211_hw_config(local, 0);
	}
}

static struct ieee80211_chanctx *
ieee80211_find_chanctx(struct ieee80211_local *local,
		       const struct cfg80211_chan_def *chandef,
		       enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		const struct cfg80211_chan_def *compat;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACE_NONE)
			continue;

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;

		compat = cfg80211_chandef_compatible(&ctx->conf.def, chandef);
		if (!compat)
			continue;

		compat = ieee80211_chanctx_reserved_chandef(local, ctx,
							    compat);
		if (!compat)
			continue;

		ieee80211_change_chanctx(local, ctx, compat);

		return ctx;
	}

	return NULL;
}

bool ieee80211_is_radar_required(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->mtx);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->radar_required) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

static bool
ieee80211_chanctx_radar_required(struct ieee80211_local *local,
				 struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	struct ieee80211_sub_if_data *sdata;
	bool required = false;

	lockdep_assert_held(&local->chanctx_mtx);
	lockdep_assert_held(&local->mtx);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (rcu_access_pointer(sdata->vif.chanctx_conf) != conf)
			continue;
		if (!sdata->radar_required)
			continue;

		required = true;
		break;
	}
	rcu_read_unlock();

	return required;
}

static struct ieee80211_chanctx *
ieee80211_alloc_chanctx(struct ieee80211_local *local,
			const struct cfg80211_chan_def *chandef,
			enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx = kzalloc(sizeof(*ctx) + local->hw.chanctx_data_size, GFP_KERNEL);
	if (!ctx)
		return NULL;

	INIT_LIST_HEAD(&ctx->assigned_vifs);
	INIT_LIST_HEAD(&ctx->reserved_vifs);
	ctx->conf.def = *chandef;
	ctx->conf.rx_chains_static = 1;
	ctx->conf.rx_chains_dynamic = 1;
	ctx->mode = mode;
	ctx->conf.radar_enabled = false;
	ieee80211_recalc_chanctx_min_def(local, ctx);

	return ctx;
}

static int ieee80211_add_chanctx(struct ieee80211_local *local,
				 struct ieee80211_chanctx *ctx)
{
	u32 changed;
	int err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	if (!local->use_chanctx)
		local->hw.conf.radar_enabled = ctx->conf.radar_enabled;

	/* turn idle off *before* setting channel -- some drivers need that */
	changed = ieee80211_idle_off(local);
	if (changed)
		ieee80211_hw_config(local, changed);

	if (!local->use_chanctx) {
		local->_oper_chandef = ctx->conf.def;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	} else {
		err = drv_add_chanctx(local, ctx);
		if (err) {
			ieee80211_recalc_idle(local);
			return err;
		}
	}

	return 0;
}

static struct ieee80211_chanctx *
ieee80211_new_chanctx(struct ieee80211_local *local,
		      const struct cfg80211_chan_def *chandef,
		      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;
	int err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	ctx = ieee80211_alloc_chanctx(local, chandef, mode);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	err = ieee80211_add_chanctx(local, ctx);
	if (err) {
		kfree(ctx);
		return ERR_PTR(err);
	}

	list_add_rcu(&ctx->list, &local->chanctx_list);
	return ctx;
}

static void ieee80211_del_chanctx(struct ieee80211_local *local,
				  struct ieee80211_chanctx *ctx)
{
	lockdep_assert_held(&local->chanctx_mtx);

	if (!local->use_chanctx) {
		struct cfg80211_chan_def *chandef = &local->_oper_chandef;
		chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef->center_freq1 = chandef->chan->center_freq;
		chandef->center_freq2 = 0;

		/* NOTE: Disabling radar is only valid here for
		 * single channel context. To be sure, check it ...
		 */
		WARN_ON(local->hw.conf.radar_enabled &&
			!list_empty(&local->chanctx_list));

		local->hw.conf.radar_enabled = false;

		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	} else {
		drv_remove_chanctx(local, ctx);
	}

	ieee80211_recalc_idle(local);
}

static void ieee80211_free_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx)
{
	lockdep_assert_held(&local->chanctx_mtx);

	WARN_ON_ONCE(ieee80211_chanctx_refcount(local, ctx) != 0);

	list_del_rcu(&ctx->list);
	ieee80211_del_chanctx(local, ctx);
	kfree_rcu(ctx, rcu_head);
}

void ieee80211_recalc_chanctx_chantype(struct ieee80211_local *local,
				       struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	struct ieee80211_sub_if_data *sdata;
	const struct cfg80211_chan_def *compat = NULL;
	struct sta_info *sta;

	lockdep_assert_held(&local->chanctx_mtx);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {

		if (!ieee80211_sdata_running(sdata))
			continue;
		if (rcu_access_pointer(sdata->vif.chanctx_conf) != conf)
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			continue;

		if (!compat)
			compat = &sdata->vif.bss_conf.chandef;

		compat = cfg80211_chandef_compatible(
				&sdata->vif.bss_conf.chandef, compat);
		if (WARN_ON_ONCE(!compat))
			break;
	}

	/* TDLS peers can sometimes affect the chandef width */
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (!sta->uploaded ||
		    !test_sta_flag(sta, WLAN_STA_TDLS_WIDER_BW) ||
		    !test_sta_flag(sta, WLAN_STA_AUTHORIZED) ||
		    !sta->tdls_chandef.chan)
			continue;

		compat = cfg80211_chandef_compatible(&sta->tdls_chandef,
						     compat);
		if (WARN_ON_ONCE(!compat))
			break;
	}
	rcu_read_unlock();

	if (!compat)
		return;

	ieee80211_change_chanctx(local, ctx, compat);
}

static void ieee80211_recalc_radar_chanctx(struct ieee80211_local *local,
					   struct ieee80211_chanctx *chanctx)
{
	bool radar_enabled;

	lockdep_assert_held(&local->chanctx_mtx);
	/* for ieee80211_is_radar_required */
	lockdep_assert_held(&local->mtx);

	radar_enabled = ieee80211_chanctx_radar_required(local, chanctx);

	if (radar_enabled == chanctx->conf.radar_enabled)
		return;

	chanctx->conf.radar_enabled = radar_enabled;

	if (!local->use_chanctx) {
		local->hw.conf.radar_enabled = chanctx->conf.radar_enabled;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	}

	drv_change_chanctx(local, chanctx, IEEE80211_CHANCTX_CHANGE_RADAR);
}

static int ieee80211_assign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_chanctx *new_ctx)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *curr_ctx = NULL;
	int ret = 0;

	if (WARN_ON(sdata->vif.type == NL80211_IFTYPE_NAN))
		return -ENOTSUPP;

	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));

	if (conf) {
		curr_ctx = container_of(conf, struct ieee80211_chanctx, conf);

		drv_unassign_vif_chanctx(local, sdata, curr_ctx);
		conf = NULL;
		list_del(&sdata->assigned_chanctx_list);
	}

	if (new_ctx) {
		ret = drv_assign_vif_chanctx(local, sdata, new_ctx);
		if (ret)
			goto out;

		conf = &new_ctx->conf;
		list_add(&sdata->assigned_chanctx_list,
			 &new_ctx->assigned_vifs);
	}

out:
	rcu_assign_pointer(sdata->vif.chanctx_conf, conf);

	sdata->vif.bss_conf.idle = !conf;

	if (curr_ctx && ieee80211_chanctx_num_assigned(local, curr_ctx) > 0) {
		ieee80211_recalc_chanctx_chantype(local, curr_ctx);
		ieee80211_recalc_smps_chanctx(local, curr_ctx);
		ieee80211_recalc_radar_chanctx(local, curr_ctx);
		ieee80211_recalc_chanctx_min_def(local, curr_ctx);
	}

	if (new_ctx && ieee80211_chanctx_num_assigned(local, new_ctx) > 0) {
		ieee80211_recalc_txpower(sdata, false);
		ieee80211_recalc_chanctx_min_def(local, new_ctx);
	}

	if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_MONITOR)
		ieee80211_bss_info_change_notify(sdata,
						 BSS_CHANGED_IDLE);

	ieee80211_check_fast_xmit_iface(sdata);

	return ret;
}

void ieee80211_recalc_smps_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *chanctx)
{
	struct ieee80211_sub_if_data *sdata;
	u8 rx_chains_static, rx_chains_dynamic;

	lockdep_assert_held(&local->chanctx_mtx);

	rx_chains_static = 1;
	rx_chains_dynamic = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		u8 needed_static, needed_dynamic;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (rcu_access_pointer(sdata->vif.chanctx_conf) !=
						&chanctx->conf)
			continue;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_P2P_DEVICE:
		case NL80211_IFTYPE_NAN:
			continue;
		case NL80211_IFTYPE_STATION:
			if (!sdata->u.mgd.associated)
				continue;
			break;
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_OCB:
			break;
		default:
			WARN_ON_ONCE(1);
		}

		switch (sdata->smps_mode) {
		default:
			WARN_ONCE(1, "Invalid SMPS mode %d\n",
				  sdata->smps_mode);
			/* fall through */
		case IEEE80211_SMPS_OFF:
			needed_static = sdata->needed_rx_chains;
			needed_dynamic = sdata->needed_rx_chains;
			break;
		case IEEE80211_SMPS_DYNAMIC:
			needed_static = 1;
			needed_dynamic = sdata->needed_rx_chains;
			break;
		case IEEE80211_SMPS_STATIC:
			needed_static = 1;
			needed_dynamic = 1;
			break;
		}

		rx_chains_static = max(rx_chains_static, needed_static);
		rx_chains_dynamic = max(rx_chains_dynamic, needed_dynamic);
	}

	/* Disable SMPS for the monitor interface */
	sdata = rcu_dereference(local->monitor_sdata);
	if (sdata &&
	    rcu_access_pointer(sdata->vif.chanctx_conf) == &chanctx->conf)
		rx_chains_dynamic = rx_chains_static = local->rx_chains;

	rcu_read_unlock();

	if (!local->use_chanctx) {
		if (rx_chains_static > 1)
			local->smps_mode = IEEE80211_SMPS_OFF;
		else if (rx_chains_dynamic > 1)
			local->smps_mode = IEEE80211_SMPS_DYNAMIC;
		else
			local->smps_mode = IEEE80211_SMPS_STATIC;
		ieee80211_hw_config(local, 0);
	}

	if (rx_chains_static == chanctx->conf.rx_chains_static &&
	    rx_chains_dynamic == chanctx->conf.rx_chains_dynamic)
		return;

	chanctx->conf.rx_chains_static = rx_chains_static;
	chanctx->conf.rx_chains_dynamic = rx_chains_dynamic;
	drv_change_chanctx(local, chanctx, IEEE80211_CHANCTX_CHANGE_RX_CHAINS);
}

static void
__ieee80211_vif_copy_chanctx_to_vlans(struct ieee80211_sub_if_data *sdata,
				      bool clear)
{
	struct ieee80211_local *local __maybe_unused = sdata->local;
	struct ieee80211_sub_if_data *vlan;
	struct ieee80211_chanctx_conf *conf;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP))
		return;

	lockdep_assert_held(&local->mtx);

	/* Check that conf exists, even when clearing this function
	 * must be called with the AP's channel context still there
	 * as it would otherwise cause VLANs to have an invalid
	 * channel context pointer for a while, possibly pointing
	 * to a channel context that has already been freed.
	 */
	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	WARN_ON(!conf);

	if (clear)
		conf = NULL;

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
		rcu_assign_pointer(vlan->vif.chanctx_conf, conf);
}

void ieee80211_vif_copy_chanctx_to_vlans(struct ieee80211_sub_if_data *sdata,
					 bool clear)
{
	struct ieee80211_local *local = sdata->local;

	mutex_lock(&local->chanctx_mtx);

	__ieee80211_vif_copy_chanctx_to_vlans(sdata, clear);

	mutex_unlock(&local->chanctx_mtx);
}

int ieee80211_vif_unreserve_chanctx(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_chanctx *ctx = sdata->reserved_chanctx;

	lockdep_assert_held(&sdata->local->chanctx_mtx);

	if (WARN_ON(!ctx))
		return -EINVAL;

	list_del(&sdata->reserved_chanctx_list);
	sdata->reserved_chanctx = NULL;

	if (ieee80211_chanctx_refcount(sdata->local, ctx) == 0) {
		if (ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER) {
			if (WARN_ON(!ctx->replace_ctx))
				return -EINVAL;

			WARN_ON(ctx->replace_ctx->replace_state !=
			        IEEE80211_CHANCTX_WILL_BE_REPLACED);
			WARN_ON(ctx->replace_ctx->replace_ctx != ctx);

			ctx->replace_ctx->replace_ctx = NULL;
			ctx->replace_ctx->replace_state =
					IEEE80211_CHANCTX_REPLACE_NONE;

			list_del_rcu(&ctx->list);
			kfree_rcu(ctx, rcu_head);
		} else {
			ieee80211_free_chanctx(sdata->local, ctx);
		}
	}

	return 0;
}

int ieee80211_vif_reserve_chanctx(struct ieee80211_sub_if_data *sdata,
				  const struct cfg80211_chan_def *chandef,
				  enum ieee80211_chanctx_mode mode,
				  bool radar_required)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *new_ctx, *curr_ctx, *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	curr_ctx = ieee80211_vif_get_chanctx(sdata);
	if (curr_ctx && local->use_chanctx && !local->ops->switch_vif_chanctx)
		return -ENOTSUPP;

	new_ctx = ieee80211_find_reservation_chanctx(local, chandef, mode);
	if (!new_ctx) {
		if (ieee80211_can_create_new_chanctx(local)) {
			new_ctx = ieee80211_new_chanctx(local, chandef, mode);
			if (IS_ERR(new_ctx))
				return PTR_ERR(new_ctx);
		} else {
			if (!curr_ctx ||
			    (curr_ctx->replace_state ==
			     IEEE80211_CHANCTX_WILL_BE_REPLACED) ||
			    !list_empty(&curr_ctx->reserved_vifs)) {
				/*
				 * Another vif already requested this context
				 * for a reservation. Find another one hoping
				 * all vifs assigned to it will also switch
				 * soon enough.
				 *
				 * TODO: This needs a little more work as some
				 * cases (more than 2 chanctx capable devices)
				 * may fail which could otherwise succeed
				 * provided some channel context juggling was
				 * performed.
				 *
				 * Consider ctx1..3, vif1..6, each ctx has 2
				 * vifs. vif1 and vif2 from ctx1 request new
				 * different chandefs starting 2 in-place
				 * reserations with ctx4 and ctx5 replacing
				 * ctx1 and ctx2 respectively. Next vif5 and
				 * vif6 from ctx3 reserve ctx4. If vif3 and
				 * vif4 remain on ctx2 as they are then this
				 * fails unless `replace_ctx` from ctx5 is
				 * replaced with ctx3.
				 */
				list_for_each_entry(ctx, &local->chanctx_list,
						    list) {
					if (ctx->replace_state !=
					    IEEE80211_CHANCTX_REPLACE_NONE)
						continue;

					if (!list_empty(&ctx->reserved_vifs))
						continue;

					curr_ctx = ctx;
					break;
				}
			}

			/*
			 * If that's true then all available contexts already
			 * have reservations and cannot be used.
			 */
			if (!curr_ctx ||
			    (curr_ctx->replace_state ==
			     IEEE80211_CHANCTX_WILL_BE_REPLACED) ||
			    !list_empty(&curr_ctx->reserved_vifs))
				return -EBUSY;

			new_ctx = ieee80211_alloc_chanctx(local, chandef, mode);
			if (!new_ctx)
				return -ENOMEM;

			new_ctx->replace_ctx = curr_ctx;
			new_ctx->replace_state =
					IEEE80211_CHANCTX_REPLACES_OTHER;

			curr_ctx->replace_ctx = new_ctx;
			curr_ctx->replace_state =
					IEEE80211_CHANCTX_WILL_BE_REPLACED;

			list_add_rcu(&new_ctx->list, &local->chanctx_list);
		}
	}

	list_add(&sdata->reserved_chanctx_list, &new_ctx->reserved_vifs);
	sdata->reserved_chanctx = new_ctx;
	sdata->reserved_chandef = *chandef;
	sdata->reserved_radar_required = radar_required;
	sdata->reserved_ready = false;

	return 0;
}

static void
ieee80211_vif_chanctx_reservation_complete(struct ieee80211_sub_if_data *sdata)
{
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_OCB:
		ieee80211_queue_work(&sdata->local->hw,
				     &sdata->csa_finalize_work);
		break;
	case NL80211_IFTYPE_STATION:
		ieee80211_queue_work(&sdata->local->hw,
				     &sdata->u.mgd.chswitch_work);
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_NAN:
	case NUM_NL80211_IFTYPES:
		WARN_ON(1);
		break;
	}
}

static void
ieee80211_vif_update_chandef(struct ieee80211_sub_if_data *sdata,
			     const struct cfg80211_chan_def *chandef)
{
	struct ieee80211_sub_if_data *vlan;

	sdata->vif.bss_conf.chandef = *chandef;

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return;

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
		vlan->vif.bss_conf.chandef = *chandef;
}

static int
ieee80211_vif_use_reserved_reassign(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_vif_chanctx_switch vif_chsw[1] = {};
	struct ieee80211_chanctx *old_ctx, *new_ctx;
	const struct cfg80211_chan_def *chandef;
	u32 changed = 0;
	int err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	new_ctx = sdata->reserved_chanctx;
	old_ctx = ieee80211_vif_get_chanctx(sdata);

	if (WARN_ON(!sdata->reserved_ready))
		return -EBUSY;

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(!old_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_REPLACES_OTHER))
		return -EINVAL;

	chandef = ieee80211_chanctx_non_reserved_chandef(local, new_ctx,
				&sdata->reserved_chandef);
	if (WARN_ON(!chandef))
		return -EINVAL;

	ieee80211_change_chanctx(local, new_ctx, chandef);

	vif_chsw[0].vif = &sdata->vif;
	vif_chsw[0].old_ctx = &old_ctx->conf;
	vif_chsw[0].new_ctx = &new_ctx->conf;

	list_del(&sdata->reserved_chanctx_list);
	sdata->reserved_chanctx = NULL;

	err = drv_switch_vif_chanctx(local, vif_chsw, 1,
				     CHANCTX_SWMODE_REASSIGN_VIF);
	if (err) {
		if (ieee80211_chanctx_refcount(local, new_ctx) == 0)
			ieee80211_free_chanctx(local, new_ctx);

		goto out;
	}

	list_move(&sdata->assigned_chanctx_list, &new_ctx->assigned_vifs);
	rcu_assign_pointer(sdata->vif.chanctx_conf, &new_ctx->conf);

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		__ieee80211_vif_copy_chanctx_to_vlans(sdata, false);

	ieee80211_check_fast_xmit_iface(sdata);

	if (ieee80211_chanctx_refcount(local, old_ctx) == 0)
		ieee80211_free_chanctx(local, old_ctx);

	if (sdata->vif.bss_conf.chandef.width != sdata->reserved_chandef.width)
		changed = BSS_CHANGED_BANDWIDTH;

	ieee80211_vif_update_chandef(sdata, &sdata->reserved_chandef);

	ieee80211_recalc_smps_chanctx(local, new_ctx);
	ieee80211_recalc_radar_chanctx(local, new_ctx);
	ieee80211_recalc_chanctx_min_def(local, new_ctx);

	if (changed)
		ieee80211_bss_info_change_notify(sdata, changed);

out:
	ieee80211_vif_chanctx_reservation_complete(sdata);
	return err;
}

static int
ieee80211_vif_use_reserved_assign(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *old_ctx, *new_ctx;
	const struct cfg80211_chan_def *chandef;
	int err;

	old_ctx = ieee80211_vif_get_chanctx(sdata);
	new_ctx = sdata->reserved_chanctx;

	if (WARN_ON(!sdata->reserved_ready))
		return -EINVAL;

	if (WARN_ON(old_ctx))
		return -EINVAL;

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_REPLACES_OTHER))
		return -EINVAL;

	chandef = ieee80211_chanctx_non_reserved_chandef(local, new_ctx,
				&sdata->reserved_chandef);
	if (WARN_ON(!chandef))
		return -EINVAL;

	ieee80211_change_chanctx(local, new_ctx, chandef);

	list_del(&sdata->reserved_chanctx_list);
	sdata->reserved_chanctx = NULL;

	err = ieee80211_assign_vif_chanctx(sdata, new_ctx);
	if (err) {
		if (ieee80211_chanctx_refcount(local, new_ctx) == 0)
			ieee80211_free_chanctx(local, new_ctx);

		goto out;
	}

out:
	ieee80211_vif_chanctx_reservation_complete(sdata);
	return err;
}

static bool
ieee80211_vif_has_in_place_reservation(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_chanctx *old_ctx, *new_ctx;

	lockdep_assert_held(&sdata->local->chanctx_mtx);

	new_ctx = sdata->reserved_chanctx;
	old_ctx = ieee80211_vif_get_chanctx(sdata);

	if (!old_ctx)
		return false;

	if (WARN_ON(!new_ctx))
		return false;

	if (old_ctx->replace_state != IEEE80211_CHANCTX_WILL_BE_REPLACED)
		return false;

	if (new_ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
		return false;

	return true;
}

static int ieee80211_chsw_switch_hwconf(struct ieee80211_local *local,
					struct ieee80211_chanctx *new_ctx)
{
	const struct cfg80211_chan_def *chandef;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	chandef = ieee80211_chanctx_reserved_chandef(local, new_ctx, NULL);
	if (WARN_ON(!chandef))
		return -EINVAL;

	local->hw.conf.radar_enabled = new_ctx->conf.radar_enabled;
	local->_oper_chandef = *chandef;
	ieee80211_hw_config(local, 0);

	return 0;
}

static int ieee80211_chsw_switch_vifs(struct ieee80211_local *local,
				      int n_vifs)
{
	struct ieee80211_vif_chanctx_switch *vif_chsw;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx, *old_ctx;
	int i, err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	vif_chsw = kcalloc(n_vifs, sizeof(vif_chsw[0]), GFP_KERNEL);
	if (!vif_chsw)
		return -ENOMEM;

	i = 0;
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto out;
		}

		list_for_each_entry(sdata, &ctx->reserved_vifs,
				    reserved_chanctx_list) {
			if (!ieee80211_vif_has_in_place_reservation(
					sdata))
				continue;

			old_ctx = ieee80211_vif_get_chanctx(sdata);
			vif_chsw[i].vif = &sdata->vif;
			vif_chsw[i].old_ctx = &old_ctx->conf;
			vif_chsw[i].new_ctx = &ctx->conf;

			i++;
		}
	}

	err = drv_switch_vif_chanctx(local, vif_chsw, n_vifs,
				     CHANCTX_SWMODE_SWAP_CONTEXTS);

out:
	kfree(vif_chsw);
	return err;
}

static int ieee80211_chsw_switch_ctxs(struct ieee80211_local *local)
{
	struct ieee80211_chanctx *ctx;
	int err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (!list_empty(&ctx->replace_ctx->assigned_vifs))
			continue;

		ieee80211_del_chanctx(local, ctx->replace_ctx);
		err = ieee80211_add_chanctx(local, ctx);
		if (err)
			goto err;
	}

	return 0;

err:
	WARN_ON(ieee80211_add_chanctx(local, ctx));
	list_for_each_entry_continue_reverse(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (!list_empty(&ctx->replace_ctx->assigned_vifs))
			continue;

		ieee80211_del_chanctx(local, ctx);
		WARN_ON(ieee80211_add_chanctx(local, ctx->replace_ctx));
	}

	return err;
}

static int ieee80211_vif_use_reserved_switch(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *sdata_tmp;
	struct ieee80211_chanctx *ctx, *ctx_tmp, *old_ctx;
	struct ieee80211_chanctx *new_ctx = NULL;
	int err, n_assigned, n_reserved, n_ready;
	int n_ctx = 0, n_vifs_switch = 0, n_vifs_assign = 0, n_vifs_ctxless = 0;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	/*
	 * If there are 2 independent pairs of channel contexts performing
	 * cross-switch of their vifs this code will still wait until both are
	 * ready even though it could be possible to switch one before the
	 * other is ready.
	 *
	 * For practical reasons and code simplicity just do a single huge
	 * switch.
	 */

	/*
	 * Verify if the reservation is still feasible.
	 *  - if it's not then disconnect
	 *  - if it is but not all vifs necessary are ready then defer
	 */

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto err;
		}

		if (!local->use_chanctx)
			new_ctx = ctx;

		n_ctx++;

		n_assigned = 0;
		n_reserved = 0;
		n_ready = 0;

		list_for_each_entry(sdata, &ctx->replace_ctx->assigned_vifs,
				    assigned_chanctx_list) {
			n_assigned++;
			if (sdata->reserved_chanctx) {
				n_reserved++;
				if (sdata->reserved_ready)
					n_ready++;
			}
		}

		if (n_assigned != n_reserved) {
			if (n_ready == n_reserved) {
				wiphy_info(local->hw.wiphy,
					   "channel context reservation cannot be finalized because some interfaces aren't switching\n");
				err = -EBUSY;
				goto err;
			}

			return -EAGAIN;
		}

		ctx->conf.radar_enabled = false;
		list_for_each_entry(sdata, &ctx->reserved_vifs,
				    reserved_chanctx_list) {
			if (ieee80211_vif_has_in_place_reservation(sdata) &&
			    !sdata->reserved_ready)
				return -EAGAIN;

			old_ctx = ieee80211_vif_get_chanctx(sdata);
			if (old_ctx) {
				if (old_ctx->replace_state ==
				    IEEE80211_CHANCTX_WILL_BE_REPLACED)
					n_vifs_switch++;
				else
					n_vifs_assign++;
			} else {
				n_vifs_ctxless++;
			}

			if (sdata->reserved_radar_required)
				ctx->conf.radar_enabled = true;
		}
	}

	if (WARN_ON(n_ctx == 0) ||
	    WARN_ON(n_vifs_switch == 0 &&
		    n_vifs_assign == 0 &&
		    n_vifs_ctxless == 0) ||
	    WARN_ON(n_ctx > 1 && !local->use_chanctx) ||
	    WARN_ON(!new_ctx && !local->use_chanctx)) {
		err = -EINVAL;
		goto err;
	}

	/*
	 * All necessary vifs are ready. Perform the switch now depending on
	 * reservations and driver capabilities.
	 */

	if (local->use_chanctx) {
		if (n_vifs_switch > 0) {
			err = ieee80211_chsw_switch_vifs(local, n_vifs_switch);
			if (err)
				goto err;
		}

		if (n_vifs_assign > 0 || n_vifs_ctxless > 0) {
			err = ieee80211_chsw_switch_ctxs(local);
			if (err)
				goto err;
		}
	} else {
		err = ieee80211_chsw_switch_hwconf(local, new_ctx);
		if (err)
			goto err;
	}

	/*
	 * Update all structures, values and pointers to point to new channel
	 * context(s).
	 */
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto err;
		}

		list_for_each_entry(sdata, &ctx->reserved_vifs,
				    reserved_chanctx_list) {
			u32 changed = 0;

			if (!ieee80211_vif_has_in_place_reservation(sdata))
				continue;

			rcu_assign_pointer(sdata->vif.chanctx_conf, &ctx->conf);

			if (sdata->vif.type == NL80211_IFTYPE_AP)
				__ieee80211_vif_copy_chanctx_to_vlans(sdata,
								      false);

			ieee80211_check_fast_xmit_iface(sdata);

			sdata->radar_required = sdata->reserved_radar_required;

			if (sdata->vif.bss_conf.chandef.width !=
			    sdata->reserved_chandef.width)
				changed = BSS_CHANGED_BANDWIDTH;

			ieee80211_vif_update_chandef(sdata, &sdata->reserved_chandef);
			if (changed)
				ieee80211_bss_info_change_notify(sdata,
								 changed);

			ieee80211_recalc_txpower(sdata, false);
		}

		ieee80211_recalc_chanctx_chantype(local, ctx);
		ieee80211_recalc_smps_chanctx(local, ctx);
		ieee80211_recalc_radar_chanctx(local, ctx);
		ieee80211_recalc_chanctx_min_def(local, ctx);

		list_for_each_entry_safe(sdata, sdata_tmp, &ctx->reserved_vifs,
					 reserved_chanctx_list) {
			if (ieee80211_vif_get_chanctx(sdata) != ctx)
				continue;

			list_del(&sdata->reserved_chanctx_list);
			list_move(&sdata->assigned_chanctx_list,
				  &ctx->assigned_vifs);
			sdata->reserved_chanctx = NULL;

			ieee80211_vif_chanctx_reservation_complete(sdata);
		}

		/*
		 * This context might have been a dependency for an already
		 * ready re-assign reservation interface that was deferred. Do
		 * not propagate error to the caller though. The in-place
		 * reservation for originally requested interface has already
		 * succeeded at this point.
		 */
		list_for_each_entry_safe(sdata, sdata_tmp, &ctx->reserved_vifs,
					 reserved_chanctx_list) {
			if (WARN_ON(ieee80211_vif_has_in_place_reservation(
					sdata)))
				continue;

			if (WARN_ON(sdata->reserved_chanctx != ctx))
				continue;

			if (!sdata->reserved_ready)
				continue;

			if (ieee80211_vif_get_chanctx(sdata))
				err = ieee80211_vif_use_reserved_reassign(
						sdata);
			else
				err = ieee80211_vif_use_reserved_assign(sdata);

			if (err) {
				sdata_info(sdata,
					   "failed to finalize (re-)assign reservation (err=%d)\n",
					   err);
				ieee80211_vif_unreserve_chanctx(sdata);
				cfg80211_stop_iface(local->hw.wiphy,
						    &sdata->wdev,
						    GFP_KERNEL);
			}
		}
	}

	/*
	 * Finally free old contexts
	 */

	list_for_each_entry_safe(ctx, ctx_tmp, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;

		ctx->replace_ctx->replace_ctx = NULL;
		ctx->replace_ctx->replace_state =
				IEEE80211_CHANCTX_REPLACE_NONE;

		list_del_rcu(&ctx->list);
		kfree_rcu(ctx, rcu_head);
	}

	return 0;

err:
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		list_for_each_entry_safe(sdata, sdata_tmp, &ctx->reserved_vifs,
					 reserved_chanctx_list) {
			ieee80211_vif_unreserve_chanctx(sdata);
			ieee80211_vif_chanctx_reservation_complete(sdata);
		}
	}

	return err;
}

static void __ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	bool use_reserved_switch = false;

	lockdep_assert_held(&local->chanctx_mtx);

	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf)
		return;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (sdata->reserved_chanctx) {
		if (sdata->reserved_chanctx->replace_state ==
		    IEEE80211_CHANCTX_REPLACES_OTHER &&
		    ieee80211_chanctx_num_reserved(local,
						   sdata->reserved_chanctx) > 1)
			use_reserved_switch = true;

		ieee80211_vif_unreserve_chanctx(sdata);
	}

	ieee80211_assign_vif_chanctx(sdata, NULL);
	if (ieee80211_chanctx_refcount(local, ctx) == 0)
		ieee80211_free_chanctx(local, ctx);

	sdata->radar_required = false;

	/* Unreserving may ready an in-place reservation. */
	if (use_reserved_switch)
		ieee80211_vif_use_reserved_switch(local);
}

int ieee80211_vif_use_channel(struct ieee80211_sub_if_data *sdata,
			      const struct cfg80211_chan_def *chandef,
			      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	u8 radar_detect_width = 0;
	int ret;

	lockdep_assert_held(&local->mtx);

	WARN_ON(sdata->dev && netif_carrier_ok(sdata->dev));

	mutex_lock(&local->chanctx_mtx);

	ret = cfg80211_chandef_dfs_required(local->hw.wiphy,
					    chandef,
					    sdata->wdev.iftype);
	if (ret < 0)
		goto out;
	if (ret > 0)
		radar_detect_width = BIT(chandef->width);

	sdata->radar_required = ret;

	ret = ieee80211_check_combinations(sdata, chandef, mode,
					   radar_detect_width);
	if (ret < 0)
		goto out;

	__ieee80211_vif_release_channel(sdata);

	ctx = ieee80211_find_chanctx(local, chandef, mode);
	if (!ctx)
		ctx = ieee80211_new_chanctx(local, chandef, mode);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	ieee80211_vif_update_chandef(sdata, chandef);

	ret = ieee80211_assign_vif_chanctx(sdata, ctx);
	if (ret) {
		/* if assign fails refcount stays the same */
		if (ieee80211_chanctx_refcount(local, ctx) == 0)
			ieee80211_free_chanctx(local, ctx);
		goto out;
	}

	ieee80211_recalc_smps_chanctx(local, ctx);
	ieee80211_recalc_radar_chanctx(local, ctx);
 out:
	if (ret)
		sdata->radar_required = false;

	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

int ieee80211_vif_use_reserved_context(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *new_ctx;
	struct ieee80211_chanctx *old_ctx;
	int err;

	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	new_ctx = sdata->reserved_chanctx;
	old_ctx = ieee80211_vif_get_chanctx(sdata);

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_WILL_BE_REPLACED))
		return -EINVAL;

	if (WARN_ON(sdata->reserved_ready))
		return -EINVAL;

	sdata->reserved_ready = true;

	if (new_ctx->replace_state == IEEE80211_CHANCTX_REPLACE_NONE) {
		if (old_ctx)
			err = ieee80211_vif_use_reserved_reassign(sdata);
		else
			err = ieee80211_vif_use_reserved_assign(sdata);

		if (err)
			return err;
	}

	/*
	 * In-place reservation may need to be finalized now either if:
	 *  a) sdata is taking part in the swapping itself and is the last one
	 *  b) sdata has switched with a re-assign reservation to an existing
	 *     context readying in-place switching of old_ctx
	 *
	 * In case of (b) do not propagate the error up because the requested
	 * sdata already switched successfully. Just spill an extra warning.
	 * The ieee80211_vif_use_reserved_switch() already stops all necessary
	 * interfaces upon failure.
	 */
	if ((old_ctx &&
	     old_ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED) ||
	    new_ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER) {
		err = ieee80211_vif_use_reserved_switch(local);
		if (err && err != -EAGAIN) {
			if (new_ctx->replace_state ==
			    IEEE80211_CHANCTX_REPLACES_OTHER)
				return err;

			wiphy_info(local->hw.wiphy,
				   "depending in-place reservation failed (err=%d)\n",
				   err);
		}
	}

	return 0;
}

int ieee80211_vif_change_bandwidth(struct ieee80211_sub_if_data *sdata,
				   const struct cfg80211_chan_def *chandef,
				   u32 *changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	const struct cfg80211_chan_def *compat;
	int ret;

	if (!cfg80211_chandef_usable(sdata->local->hw.wiphy, chandef,
				     IEEE80211_CHAN_DISABLED))
		return -EINVAL;

	mutex_lock(&local->chanctx_mtx);
	if (cfg80211_chandef_identical(chandef, &sdata->vif.bss_conf.chandef)) {
		ret = 0;
		goto out;
	}

	if (chandef->width == NL80211_CHAN_WIDTH_20_NOHT ||
	    sdata->vif.bss_conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT) {
		ret = -EINVAL;
		goto out;
	}

	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf) {
		ret = -EINVAL;
		goto out;
	}

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	compat = cfg80211_chandef_compatible(&conf->def, chandef);
	if (!compat) {
		ret = -EINVAL;
		goto out;
	}

	switch (ctx->replace_state) {
	case IEEE80211_CHANCTX_REPLACE_NONE:
		if (!ieee80211_chanctx_reserved_chandef(local, ctx, compat)) {
			ret = -EBUSY;
			goto out;
		}
		break;
	case IEEE80211_CHANCTX_WILL_BE_REPLACED:
		/* TODO: Perhaps the bandwidth change could be treated as a
		 * reservation itself? */
		ret = -EBUSY;
		goto out;
	case IEEE80211_CHANCTX_REPLACES_OTHER:
		/* channel context that is going to replace another channel
		 * context doesn't really exist and shouldn't be assigned
		 * anywhere yet */
		WARN_ON(1);
		break;
	}

	ieee80211_vif_update_chandef(sdata, chandef);

	ieee80211_recalc_chanctx_chantype(local, ctx);

	*changed |= BSS_CHANGED_BANDWIDTH;
	ret = 0;
 out:
	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

void ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata)
{
	WARN_ON(sdata->dev && netif_carrier_ok(sdata->dev));

	lockdep_assert_held(&sdata->local->mtx);

	mutex_lock(&sdata->local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);
	mutex_unlock(&sdata->local->chanctx_mtx);
}

void ieee80211_vif_vlan_copy_chanctx(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *ap;
	struct ieee80211_chanctx_conf *conf;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP_VLAN || !sdata->bss))
		return;

	ap = container_of(sdata->bss, struct ieee80211_sub_if_data, u.ap);

	mutex_lock(&local->chanctx_mtx);

	conf = rcu_dereference_protected(ap->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	rcu_assign_pointer(sdata->vif.chanctx_conf, conf);
	mutex_unlock(&local->chanctx_mtx);
}

void ieee80211_iter_chan_contexts_atomic(
	struct ieee80211_hw *hw,
	void (*iter)(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *chanctx_conf,
		     void *data),
	void *iter_data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_chanctx *ctx;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, &local->chanctx_list, list)
		if (ctx->driver_present)
			iter(hw, &ctx->conf, iter_data);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iter_chan_contexts_atomic);
