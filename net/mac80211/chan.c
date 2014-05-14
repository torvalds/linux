/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include <linux/export.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

static enum nl80211_chan_width ieee80211_get_sta_bw(struct ieee80211_sta *sta)
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

		if (!sta->uploaded)
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
		case NL80211_IFTYPE_P2P_DEVICE:
			continue;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MESH_POINT:
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
	if (cfg80211_chandef_identical(&ctx->conf.def, chandef))
		return;

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

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;

		compat = cfg80211_chandef_compatible(&ctx->conf.def, chandef);
		if (!compat)
			continue;

		ieee80211_change_chanctx(local, ctx, compat);

		return ctx;
	}

	return NULL;
}

static bool ieee80211_is_radar_required(struct ieee80211_local *local)
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

static struct ieee80211_chanctx *
ieee80211_new_chanctx(struct ieee80211_local *local,
		      const struct cfg80211_chan_def *chandef,
		      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;
	u32 changed;
	int err;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx = kzalloc(sizeof(*ctx) + local->hw.chanctx_data_size, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->conf.def = *chandef;
	ctx->conf.rx_chains_static = 1;
	ctx->conf.rx_chains_dynamic = 1;
	ctx->mode = mode;
	ctx->conf.radar_enabled = ieee80211_is_radar_required(local);
	ieee80211_recalc_chanctx_min_def(local, ctx);
	if (!local->use_chanctx)
		local->hw.conf.radar_enabled = ctx->conf.radar_enabled;

	/* we hold the mutex to prevent idle from changing */
	lockdep_assert_held(&local->mtx);
	/* turn idle off *before* setting channel -- some drivers need that */
	changed = ieee80211_idle_off(local);
	if (changed)
		ieee80211_hw_config(local, changed);

	if (!local->use_chanctx) {
		local->_oper_chandef = *chandef;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	} else {
		err = drv_add_chanctx(local, ctx);
		if (err) {
			kfree(ctx);
			ieee80211_recalc_idle(local);
			return ERR_PTR(err);
		}
	}

	/* and keep the mutex held until the new chanctx is on the list */
	list_add_rcu(&ctx->list, &local->chanctx_list);

	return ctx;
}

static void ieee80211_free_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx)
{
	bool check_single_channel = false;
	lockdep_assert_held(&local->chanctx_mtx);

	WARN_ON_ONCE(ctx->refcount != 0);

	if (!local->use_chanctx) {
		struct cfg80211_chan_def *chandef = &local->_oper_chandef;
		chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef->center_freq1 = chandef->chan->center_freq;
		chandef->center_freq2 = 0;

		/* NOTE: Disabling radar is only valid here for
		 * single channel context. To be sure, check it ...
		 */
		if (local->hw.conf.radar_enabled)
			check_single_channel = true;
		local->hw.conf.radar_enabled = false;

		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	} else {
		drv_remove_chanctx(local, ctx);
	}

	list_del_rcu(&ctx->list);
	kfree_rcu(ctx, rcu_head);

	/* throw a warning if this wasn't the only channel context. */
	WARN_ON(check_single_channel && !list_empty(&local->chanctx_list));

	ieee80211_recalc_idle(local);
}

static int ieee80211_assign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_chanctx *ctx)
{
	struct ieee80211_local *local = sdata->local;
	int ret;

	lockdep_assert_held(&local->chanctx_mtx);

	ret = drv_assign_vif_chanctx(local, sdata, ctx);
	if (ret)
		return ret;

	rcu_assign_pointer(sdata->vif.chanctx_conf, &ctx->conf);
	ctx->refcount++;

	ieee80211_recalc_txpower(sdata);
	ieee80211_recalc_chanctx_min_def(local, ctx);
	sdata->vif.bss_conf.idle = false;

	if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_MONITOR)
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_IDLE);

	return 0;
}

static void ieee80211_recalc_chanctx_chantype(struct ieee80211_local *local,
					      struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	struct ieee80211_sub_if_data *sdata;
	const struct cfg80211_chan_def *compat = NULL;

	lockdep_assert_held(&local->chanctx_mtx);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {

		if (!ieee80211_sdata_running(sdata))
			continue;
		if (rcu_access_pointer(sdata->vif.chanctx_conf) != conf)
			continue;

		if (!compat)
			compat = &sdata->vif.bss_conf.chandef;

		compat = cfg80211_chandef_compatible(
				&sdata->vif.bss_conf.chandef, compat);
		if (!compat)
			break;
	}
	rcu_read_unlock();

	if (WARN_ON_ONCE(!compat))
		return;

	ieee80211_change_chanctx(local, ctx, compat);
}

static void ieee80211_recalc_radar_chanctx(struct ieee80211_local *local,
					   struct ieee80211_chanctx *chanctx)
{
	bool radar_enabled;

	lockdep_assert_held(&local->chanctx_mtx);
	/* for setting local->radar_detect_enabled */
	lockdep_assert_held(&local->mtx);

	radar_enabled = ieee80211_is_radar_required(local);

	if (radar_enabled == chanctx->conf.radar_enabled)
		return;

	chanctx->conf.radar_enabled = radar_enabled;
	local->radar_detect_enabled = chanctx->conf.radar_enabled;

	if (!local->use_chanctx) {
		local->hw.conf.radar_enabled = chanctx->conf.radar_enabled;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	}

	drv_change_chanctx(local, chanctx, IEEE80211_CHANCTX_CHANGE_RADAR);
}

static void ieee80211_unassign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_chanctx *ctx)
{
	struct ieee80211_local *local = sdata->local;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx->refcount--;
	rcu_assign_pointer(sdata->vif.chanctx_conf, NULL);

	sdata->vif.bss_conf.idle = true;

	if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_MONITOR)
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_IDLE);

	drv_unassign_vif_chanctx(local, sdata, ctx);

	if (ctx->refcount > 0) {
		ieee80211_recalc_chanctx_chantype(sdata->local, ctx);
		ieee80211_recalc_smps_chanctx(local, ctx);
		ieee80211_recalc_radar_chanctx(local, ctx);
		ieee80211_recalc_chanctx_min_def(local, ctx);
	}
}

static void __ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf)
		return;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	ieee80211_unassign_vif_chanctx(sdata, ctx);
	if (ctx->refcount == 0)
		ieee80211_free_chanctx(local, ctx);
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

int ieee80211_vif_use_channel(struct ieee80211_sub_if_data *sdata,
			      const struct cfg80211_chan_def *chandef,
			      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	int ret;

	lockdep_assert_held(&local->mtx);

	WARN_ON(sdata->dev && netif_carrier_ok(sdata->dev));

	mutex_lock(&local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);

	ctx = ieee80211_find_chanctx(local, chandef, mode);
	if (!ctx)
		ctx = ieee80211_new_chanctx(local, chandef, mode);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	sdata->vif.bss_conf.chandef = *chandef;

	ret = ieee80211_assign_vif_chanctx(sdata, ctx);
	if (ret) {
		/* if assign fails refcount stays the same */
		if (ctx->refcount == 0)
			ieee80211_free_chanctx(local, ctx);
		goto out;
	}

	ieee80211_recalc_smps_chanctx(local, ctx);
	ieee80211_recalc_radar_chanctx(local, ctx);
 out:
	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

int ieee80211_vif_change_channel(struct ieee80211_sub_if_data *sdata,
				 u32 *changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	const struct cfg80211_chan_def *chandef = &sdata->csa_chandef;
	int ret;
	u32 chanctx_changed = 0;

	lockdep_assert_held(&local->mtx);

	/* should never be called if not performing a channel switch. */
	if (WARN_ON(!sdata->vif.csa_active))
		return -EINVAL;

	if (!cfg80211_chandef_usable(sdata->local->hw.wiphy, chandef,
				     IEEE80211_CHAN_DISABLED))
		return -EINVAL;

	mutex_lock(&local->chanctx_mtx);
	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf) {
		ret = -EINVAL;
		goto out;
	}

	ctx = container_of(conf, struct ieee80211_chanctx, conf);
	if (ctx->refcount != 1) {
		ret = -EINVAL;
		goto out;
	}

	if (sdata->vif.bss_conf.chandef.width != chandef->width) {
		chanctx_changed = IEEE80211_CHANCTX_CHANGE_WIDTH;
		*changed |= BSS_CHANGED_BANDWIDTH;
	}

	sdata->vif.bss_conf.chandef = *chandef;
	ctx->conf.def = *chandef;

	chanctx_changed |= IEEE80211_CHANCTX_CHANGE_CHANNEL;
	drv_change_chanctx(local, ctx, chanctx_changed);

	ieee80211_recalc_chanctx_chantype(local, ctx);
	ieee80211_recalc_smps_chanctx(local, ctx);
	ieee80211_recalc_radar_chanctx(local, ctx);
	ieee80211_recalc_chanctx_min_def(local, ctx);

	ret = 0;
 out:
	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

int ieee80211_vif_change_bandwidth(struct ieee80211_sub_if_data *sdata,
				   const struct cfg80211_chan_def *chandef,
				   u32 *changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
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
	if (!cfg80211_chandef_compatible(&conf->def, chandef)) {
		ret = -EINVAL;
		goto out;
	}

	sdata->vif.bss_conf.chandef = *chandef;

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

void ieee80211_vif_copy_chanctx_to_vlans(struct ieee80211_sub_if_data *sdata,
					 bool clear)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *vlan;
	struct ieee80211_chanctx_conf *conf;

	ASSERT_RTNL();

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP))
		return;

	mutex_lock(&local->chanctx_mtx);

	/*
	 * Check that conf exists, even when clearing this function
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
