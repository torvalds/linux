// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211 - channel management
 * Copyright 2020 - 2025 Intel Corporation
 */

#include <linux/nl80211.h>
#include <linux/export.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"

static int ieee80211_chanctx_num_assigned(struct ieee80211_local *local,
					  struct ieee80211_chanctx *ctx)
{
	struct ieee80211_link_data *link;
	int num = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(link, &ctx->assigned_links, assigned_chanctx_list)
		num++;

	return num;
}

static int ieee80211_chanctx_num_reserved(struct ieee80211_local *local,
					  struct ieee80211_chanctx *ctx)
{
	struct ieee80211_link_data *link;
	int num = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(link, &ctx->reserved_links, reserved_chanctx_list)
		num++;

	return num;
}

int ieee80211_chanctx_refcount(struct ieee80211_local *local,
			       struct ieee80211_chanctx *ctx)
{
	return ieee80211_chanctx_num_assigned(local, ctx) +
	       ieee80211_chanctx_num_reserved(local, ctx);
}

static int ieee80211_num_chanctx(struct ieee80211_local *local, int radio_idx)
{
	struct ieee80211_chanctx *ctx;
	int num = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (radio_idx >= 0 && ctx->conf.radio_idx != radio_idx)
			continue;
		num++;
	}

	return num;
}

static bool ieee80211_can_create_new_chanctx(struct ieee80211_local *local,
					     int radio_idx)
{
	lockdep_assert_wiphy(local->hw.wiphy);

	return ieee80211_num_chanctx(local, radio_idx) <
	       ieee80211_max_num_channels(local, radio_idx);
}

static struct ieee80211_chanctx *
ieee80211_link_get_chanctx(struct ieee80211_link_data *link)
{
	struct ieee80211_local *local __maybe_unused = link->sdata->local;
	struct ieee80211_chanctx_conf *conf;

	conf = rcu_dereference_protected(link->conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));
	if (!conf)
		return NULL;

	return container_of(conf, struct ieee80211_chanctx, conf);
}

bool ieee80211_chanreq_identical(const struct ieee80211_chan_req *a,
				 const struct ieee80211_chan_req *b)
{
	if (!cfg80211_chandef_identical(&a->oper, &b->oper))
		return false;
	if (!a->ap.chan && !b->ap.chan)
		return true;
	return cfg80211_chandef_identical(&a->ap, &b->ap);
}

static const struct ieee80211_chan_req *
ieee80211_chanreq_compatible(const struct ieee80211_chan_req *a,
			     const struct ieee80211_chan_req *b,
			     struct ieee80211_chan_req *tmp)
{
	const struct cfg80211_chan_def *compat;

	if (a->ap.chan && b->ap.chan &&
	    !cfg80211_chandef_identical(&a->ap, &b->ap))
		return NULL;

	compat = cfg80211_chandef_compatible(&a->oper, &b->oper);
	if (!compat)
		return NULL;

	/* Note: later code assumes this always fills & returns tmp if compat */
	tmp->oper = *compat;
	tmp->ap = a->ap.chan ? a->ap : b->ap;
	return tmp;
}

static const struct ieee80211_chan_req *
ieee80211_chanctx_compatible(struct ieee80211_chanctx *ctx,
			     const struct ieee80211_chan_req *req,
			     struct ieee80211_chan_req *tmp)
{
	const struct ieee80211_chan_req *ret;
	struct ieee80211_chan_req tmp2;

	*tmp = (struct ieee80211_chan_req){
		.oper = ctx->conf.def,
		.ap = ctx->conf.ap,
	};

	ret = ieee80211_chanreq_compatible(tmp, req, &tmp2);
	if (!ret)
		return NULL;
	*tmp = *ret;
	return tmp;
}

static const struct ieee80211_chan_req *
ieee80211_chanctx_reserved_chanreq(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx,
				   const struct ieee80211_chan_req *req,
				   struct ieee80211_chan_req *tmp)
{
	struct ieee80211_link_data *link;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(!req))
		return NULL;

	list_for_each_entry(link, &ctx->reserved_links, reserved_chanctx_list) {
		req = ieee80211_chanreq_compatible(&link->reserved, req, tmp);
		if (!req)
			break;
	}

	return req;
}

static const struct ieee80211_chan_req *
ieee80211_chanctx_non_reserved_chandef(struct ieee80211_local *local,
				       struct ieee80211_chanctx *ctx,
				       const struct ieee80211_chan_req *compat,
				       struct ieee80211_chan_req *tmp)
{
	struct ieee80211_link_data *link;
	const struct ieee80211_chan_req *comp_def = compat;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(link, &ctx->assigned_links, assigned_chanctx_list) {
		struct ieee80211_bss_conf *link_conf = link->conf;

		if (link->reserved_chanctx)
			continue;

		comp_def = ieee80211_chanreq_compatible(&link_conf->chanreq,
							comp_def, tmp);
		if (!comp_def)
			break;
	}

	return comp_def;
}

static bool
ieee80211_chanctx_can_reserve(struct ieee80211_local *local,
			      struct ieee80211_chanctx *ctx,
			      const struct ieee80211_chan_req *req)
{
	struct ieee80211_chan_req tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!ieee80211_chanctx_reserved_chanreq(local, ctx, req, &tmp))
		return false;

	if (!ieee80211_chanctx_non_reserved_chandef(local, ctx, req, &tmp))
		return false;

	if (!list_empty(&ctx->reserved_links) &&
	    ieee80211_chanctx_reserved_chanreq(local, ctx, req, &tmp))
		return true;

	return false;
}

static struct ieee80211_chanctx *
ieee80211_find_reservation_chanctx(struct ieee80211_local *local,
				   const struct ieee80211_chan_req *chanreq,
				   enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;

		if (!ieee80211_chanctx_can_reserve(local, ctx, chanreq))
			continue;

		return ctx;
	}

	return NULL;
}

static enum nl80211_chan_width ieee80211_get_sta_bw(struct sta_info *sta,
						    unsigned int link_id)
{
	enum ieee80211_sta_rx_bandwidth width;
	struct link_sta_info *link_sta;

	link_sta = wiphy_dereference(sta->local->hw.wiphy, sta->link[link_id]);

	/* no effect if this STA has no presence on this link */
	if (!link_sta)
		return NL80211_CHAN_WIDTH_20_NOHT;

	/*
	 * We assume that TX/RX might be asymmetric (so e.g. VHT operating
	 * mode notification changes what a STA wants to receive, but not
	 * necessarily what it will transmit to us), and therefore use the
	 * capabilities here. Calling it RX bandwidth capability is a bit
	 * wrong though, since capabilities are in fact symmetric.
	 */
	width = ieee80211_sta_cap_rx_bw(link_sta);

	switch (width) {
	case IEEE80211_STA_RX_BW_20:
		if (link_sta->pub->ht_cap.ht_supported)
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
	case IEEE80211_STA_RX_BW_320:
		return NL80211_CHAN_WIDTH_320;
	default:
		WARN_ON(1);
		return NL80211_CHAN_WIDTH_20;
	}
}

static enum nl80211_chan_width
ieee80211_get_max_required_bw(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	unsigned int link_id = link->link_id;
	enum nl80211_chan_width max_bw = NL80211_CHAN_WIDTH_20_NOHT;
	struct sta_info *sta;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	list_for_each_entry(sta, &sdata->local->sta_list, list) {
		if (sdata != sta->sdata &&
		    !(sta->sdata->bss && sta->sdata->bss == sdata->bss))
			continue;

		max_bw = max(max_bw, ieee80211_get_sta_bw(sta, link_id));
	}

	return max_bw;
}

static enum nl80211_chan_width
ieee80211_get_chanctx_max_required_bw(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      struct ieee80211_link_data *rsvd_for,
				      bool check_reserved)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_link_data *link;
	enum nl80211_chan_width max_bw = NL80211_CHAN_WIDTH_20_NOHT;

	if (WARN_ON(check_reserved && rsvd_for))
		return ctx->conf.def.width;

	for_each_sdata_link(local, link) {
		enum nl80211_chan_width width = NL80211_CHAN_WIDTH_20_NOHT;

		if (check_reserved) {
			if (link->reserved_chanctx != ctx)
				continue;
		} else if (link != rsvd_for &&
			   rcu_access_pointer(link->conf->chanctx_conf) != &ctx->conf)
			continue;

		switch (link->sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			if (!link->sdata->vif.cfg.assoc) {
				/*
				 * The AP's sta->bandwidth may not yet be set
				 * at this point (pre-association), so simply
				 * take the width from the chandef. We cannot
				 * have TDLS peers yet (only after association).
				 */
				width = link->conf->chanreq.oper.width;
				break;
			}
			/*
			 * otherwise just use min_def like in AP, depending on what
			 * we currently think the AP STA (and possibly TDLS peers)
			 * require(s)
			 */
			fallthrough;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_AP_VLAN:
			width = ieee80211_get_max_required_bw(link);
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
		case NL80211_IFTYPE_NAN:
			continue;
		case NL80211_IFTYPE_MONITOR:
			WARN_ON_ONCE(!ieee80211_hw_check(&local->hw,
							 NO_VIRTUAL_MONITOR));
			fallthrough;
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_OCB:
			width = link->conf->chanreq.oper.width;
			break;
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
			WARN_ON_ONCE(1);
		}

		max_bw = max(max_bw, width);
	}

	/* use the configured bandwidth in case of monitor interface */
	sdata = wiphy_dereference(local->hw.wiphy, local->monitor_sdata);
	if (sdata &&
	    rcu_access_pointer(sdata->vif.bss_conf.chanctx_conf) == &ctx->conf)
		max_bw = max(max_bw, ctx->conf.def.width);

	return max_bw;
}

/*
 * recalc the min required chan width of the channel context, which is
 * the max of min required widths of all the interfaces bound to this
 * channel context.
 */
static u32
_ieee80211_recalc_chanctx_min_def(struct ieee80211_local *local,
				  struct ieee80211_chanctx *ctx,
				  struct ieee80211_link_data *rsvd_for,
				  bool check_reserved)
{
	enum nl80211_chan_width max_bw;
	struct cfg80211_chan_def min_def;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* don't optimize non-20MHz based and radar_enabled confs */
	if (ctx->conf.def.width == NL80211_CHAN_WIDTH_5 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_10 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_1 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_2 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_4 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_8 ||
	    ctx->conf.def.width == NL80211_CHAN_WIDTH_16 ||
	    ctx->conf.radar_enabled) {
		ctx->conf.min_def = ctx->conf.def;
		return 0;
	}

	max_bw = ieee80211_get_chanctx_max_required_bw(local, ctx, rsvd_for,
						       check_reserved);

	/* downgrade chandef up to max_bw */
	min_def = ctx->conf.def;
	while (min_def.width > max_bw)
		ieee80211_chandef_downgrade(&min_def, NULL);

	if (cfg80211_chandef_identical(&ctx->conf.min_def, &min_def))
		return 0;

	ctx->conf.min_def = min_def;
	if (!ctx->driver_present)
		return 0;

	return IEEE80211_CHANCTX_CHANGE_MIN_DEF;
}

static void ieee80211_chan_bw_change(struct ieee80211_local *local,
				     struct ieee80211_chanctx *ctx,
				     bool reserved, bool narrowed)
{
	struct sta_info *sta;
	struct ieee80211_supported_band *sband =
		local->hw.wiphy->bands[ctx->conf.def.chan->band];

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list,
				list) {
		struct ieee80211_sub_if_data *sdata = sta->sdata;
		enum ieee80211_sta_rx_bandwidth new_sta_bw;
		unsigned int link_id;

		if (!ieee80211_sdata_running(sta->sdata))
			continue;

		for (link_id = 0; link_id < ARRAY_SIZE(sta->sdata->link); link_id++) {
			struct ieee80211_link_data *link =
				rcu_dereference(sdata->link[link_id]);
			struct ieee80211_bss_conf *link_conf;
			struct cfg80211_chan_def *new_chandef;
			struct link_sta_info *link_sta;

			if (!link)
				continue;

			link_conf = link->conf;

			if (rcu_access_pointer(link_conf->chanctx_conf) != &ctx->conf)
				continue;

			link_sta = rcu_dereference(sta->link[link_id]);
			if (!link_sta)
				continue;

			if (reserved)
				new_chandef = &link->reserved.oper;
			else
				new_chandef = &link_conf->chanreq.oper;

			new_sta_bw = _ieee80211_sta_cur_vht_bw(link_sta,
							       new_chandef);

			/* nothing change */
			if (new_sta_bw == link_sta->pub->bandwidth)
				continue;

			/* vif changed to narrow BW and narrow BW for station wasn't
			 * requested or vice versa */
			if ((new_sta_bw < link_sta->pub->bandwidth) == !narrowed)
				continue;

			link_sta->pub->bandwidth = new_sta_bw;
			rate_control_rate_update(local, sband, link_sta,
						 IEEE80211_RC_BW_CHANGED);
		}
	}
	rcu_read_unlock();
}

/*
 * recalc the min required chan width of the channel context, which is
 * the max of min required widths of all the interfaces bound to this
 * channel context.
 */
void ieee80211_recalc_chanctx_min_def(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      struct ieee80211_link_data *rsvd_for,
				      bool check_reserved)
{
	u32 changed = _ieee80211_recalc_chanctx_min_def(local, ctx, rsvd_for,
							check_reserved);

	if (!changed)
		return;

	/* check is BW narrowed */
	ieee80211_chan_bw_change(local, ctx, false, true);

	drv_change_chanctx(local, ctx, changed);

	/* check is BW wider */
	ieee80211_chan_bw_change(local, ctx, false, false);
}

static void _ieee80211_change_chanctx(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      struct ieee80211_chanctx *old_ctx,
				      const struct ieee80211_chan_req *chanreq,
				      struct ieee80211_link_data *rsvd_for)
{
	const struct cfg80211_chan_def *chandef = &chanreq->oper;
	struct ieee80211_chan_req ctx_req = {
		.oper = ctx->conf.def,
		.ap = ctx->conf.ap,
	};
	u32 changed = 0;

	/* expected to handle only 20/40/80/160/320 channel widths */
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_320:
		break;
	default:
		WARN_ON(1);
	}

	/* Check maybe BW narrowed - we do this _before_ calling recalc_chanctx_min_def
	 * due to maybe not returning from it, e.g in case new context was added
	 * first time with all parameters up to date.
	 */
	ieee80211_chan_bw_change(local, old_ctx, false, true);

	if (ieee80211_chanreq_identical(&ctx_req, chanreq)) {
		ieee80211_recalc_chanctx_min_def(local, ctx, rsvd_for, false);
		return;
	}

	WARN_ON(ieee80211_chanctx_refcount(local, ctx) > 1 &&
		!cfg80211_chandef_compatible(&ctx->conf.def, &chanreq->oper));

	ieee80211_remove_wbrf(local, &ctx->conf.def);

	if (!cfg80211_chandef_identical(&ctx->conf.def, &chanreq->oper)) {
		if (ctx->conf.def.width != chanreq->oper.width)
			changed |= IEEE80211_CHANCTX_CHANGE_WIDTH;
		if (ctx->conf.def.punctured != chanreq->oper.punctured)
			changed |= IEEE80211_CHANCTX_CHANGE_PUNCTURING;
	}
	if (!cfg80211_chandef_identical(&ctx->conf.ap, &chanreq->ap))
		changed |= IEEE80211_CHANCTX_CHANGE_AP;
	ctx->conf.def = *chandef;
	ctx->conf.ap = chanreq->ap;

	/* check if min chanctx also changed */
	changed |= _ieee80211_recalc_chanctx_min_def(local, ctx, rsvd_for, false);

	ieee80211_add_wbrf(local, &ctx->conf.def);

	drv_change_chanctx(local, ctx, changed);

	/* check if BW is wider */
	ieee80211_chan_bw_change(local, old_ctx, false, false);
}

static void ieee80211_change_chanctx(struct ieee80211_local *local,
				     struct ieee80211_chanctx *ctx,
				     struct ieee80211_chanctx *old_ctx,
				     const struct ieee80211_chan_req *chanreq)
{
	_ieee80211_change_chanctx(local, ctx, old_ctx, chanreq, NULL);
}

/* Note: if successful, the returned chanctx is reserved for the link */
static struct ieee80211_chanctx *
ieee80211_find_chanctx(struct ieee80211_local *local,
		       struct ieee80211_link_data *link,
		       const struct ieee80211_chan_req *chanreq,
		       enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chan_req tmp;
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;

	if (WARN_ON(link->reserved_chanctx))
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		const struct ieee80211_chan_req *compat;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACE_NONE)
			continue;

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;

		compat = ieee80211_chanctx_compatible(ctx, chanreq, &tmp);
		if (!compat)
			continue;

		compat = ieee80211_chanctx_reserved_chanreq(local, ctx,
							    compat, &tmp);
		if (!compat)
			continue;

		/*
		 * Reserve the chanctx temporarily, as the driver might change
		 * active links during callbacks we make into it below and/or
		 * later during assignment, which could (otherwise) cause the
		 * context to actually be removed.
		 */
		link->reserved_chanctx = ctx;
		list_add(&link->reserved_chanctx_list,
			 &ctx->reserved_links);

		ieee80211_change_chanctx(local, ctx, ctx, compat);

		return ctx;
	}

	return NULL;
}

bool ieee80211_is_radar_required(struct ieee80211_local *local,
				 struct cfg80211_scan_request *req)
{
	struct wiphy *wiphy = local->hw.wiphy;
	struct ieee80211_link_data *link;
	struct ieee80211_channel *chan;
	int radio_idx;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!req)
		return false;

	for_each_sdata_link(local, link) {
		if (link->radar_required) {
			if (wiphy->n_radio < 2)
				return true;

			chan = link->conf->chanreq.oper.chan;
			radio_idx = cfg80211_get_radio_idx_by_chan(wiphy, chan);
			/*
			 * The radio index (radio_idx) is expected to be valid,
			 * as it's derived from a channel tied to a link. If
			 * it's invalid (i.e., negative), return true to avoid
			 * potential issues with radar-sensitive operations.
			 */
			if (radio_idx < 0)
				return true;

			if (ieee80211_is_radio_idx_in_scan_req(wiphy, req,
							       radio_idx))
				return true;
		}
	}

	return false;
}

static bool
ieee80211_chanctx_radar_required(struct ieee80211_local *local,
				 struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	struct ieee80211_link_data *link;

	lockdep_assert_wiphy(local->hw.wiphy);

	for_each_sdata_link(local, link) {
		if (rcu_access_pointer(link->conf->chanctx_conf) != conf)
			continue;
		if (!link->radar_required)
			continue;
		return true;
	}

	return false;
}

static struct ieee80211_chanctx *
ieee80211_alloc_chanctx(struct ieee80211_local *local,
			const struct ieee80211_chan_req *chanreq,
			enum ieee80211_chanctx_mode mode,
			int radio_idx)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(local->hw.wiphy);

	ctx = kzalloc(sizeof(*ctx) + local->hw.chanctx_data_size, GFP_KERNEL);
	if (!ctx)
		return NULL;

	INIT_LIST_HEAD(&ctx->assigned_links);
	INIT_LIST_HEAD(&ctx->reserved_links);
	ctx->conf.def = chanreq->oper;
	ctx->conf.ap = chanreq->ap;
	ctx->conf.rx_chains_static = 1;
	ctx->conf.rx_chains_dynamic = 1;
	ctx->mode = mode;
	ctx->conf.radar_enabled = false;
	ctx->conf.radio_idx = radio_idx;
	ctx->radar_detected = false;
	_ieee80211_recalc_chanctx_min_def(local, ctx, NULL, false);

	return ctx;
}

static int ieee80211_add_chanctx(struct ieee80211_local *local,
				 struct ieee80211_chanctx *ctx)
{
	u32 changed;
	int err;

	lockdep_assert_wiphy(local->hw.wiphy);

	ieee80211_add_wbrf(local, &ctx->conf.def);

	/* turn idle off *before* setting channel -- some drivers need that */
	changed = ieee80211_idle_off(local);
	if (changed)
		ieee80211_hw_config(local, changed);

	err = drv_add_chanctx(local, ctx);
	if (err) {
		ieee80211_recalc_idle(local);
		return err;
	}

	return 0;
}

static struct ieee80211_chanctx *
ieee80211_new_chanctx(struct ieee80211_local *local,
		      const struct ieee80211_chan_req *chanreq,
		      enum ieee80211_chanctx_mode mode,
		      bool assign_on_failure,
		      int radio_idx)
{
	struct ieee80211_chanctx *ctx;
	int err;

	lockdep_assert_wiphy(local->hw.wiphy);

	ctx = ieee80211_alloc_chanctx(local, chanreq, mode, radio_idx);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	err = ieee80211_add_chanctx(local, ctx);
	if (!assign_on_failure && err) {
		kfree(ctx);
		return ERR_PTR(err);
	}
	/* We ignored a driver error, see _ieee80211_set_active_links */
	WARN_ON_ONCE(err && !local->in_reconfig);

	list_add_rcu(&ctx->list, &local->chanctx_list);
	return ctx;
}

static void ieee80211_del_chanctx(struct ieee80211_local *local,
				  struct ieee80211_chanctx *ctx,
				  bool skip_idle_recalc)
{
	lockdep_assert_wiphy(local->hw.wiphy);

	drv_remove_chanctx(local, ctx);

	if (!skip_idle_recalc)
		ieee80211_recalc_idle(local);

	ieee80211_remove_wbrf(local, &ctx->conf.def);
}

static void ieee80211_free_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx,
				   bool skip_idle_recalc)
{
	lockdep_assert_wiphy(local->hw.wiphy);

	WARN_ON_ONCE(ieee80211_chanctx_refcount(local, ctx) != 0);

	list_del_rcu(&ctx->list);
	ieee80211_del_chanctx(local, ctx, skip_idle_recalc);
	kfree_rcu(ctx, rcu_head);
}

void ieee80211_recalc_chanctx_chantype(struct ieee80211_local *local,
				       struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	const struct ieee80211_chan_req *compat = NULL;
	struct ieee80211_link_data *link;
	struct ieee80211_chan_req tmp;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	for_each_sdata_link(local, link) {
		struct ieee80211_bss_conf *link_conf;

		if (link->sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			continue;

		link_conf = link->conf;

		if (rcu_access_pointer(link_conf->chanctx_conf) != conf)
			continue;

		if (!compat)
			compat = &link_conf->chanreq;

		compat = ieee80211_chanreq_compatible(&link_conf->chanreq,
						      compat, &tmp);
		if (WARN_ON_ONCE(!compat))
			return;
	}

	if (WARN_ON_ONCE(!compat))
		return;

	/* TDLS peers can sometimes affect the chandef width */
	list_for_each_entry(sta, &local->sta_list, list) {
		struct ieee80211_sub_if_data *sdata = sta->sdata;
		struct ieee80211_chan_req tdls_chanreq = {};
		int tdls_link_id;

		if (!sta->uploaded ||
		    !test_sta_flag(sta, WLAN_STA_TDLS_WIDER_BW) ||
		    !test_sta_flag(sta, WLAN_STA_AUTHORIZED) ||
		    !sta->tdls_chandef.chan)
			continue;

		tdls_link_id = ieee80211_tdls_sta_link_id(sta);
		link = sdata_dereference(sdata->link[tdls_link_id], sdata);
		if (!link)
			continue;

		if (rcu_access_pointer(link->conf->chanctx_conf) != conf)
			continue;

		tdls_chanreq.oper = sta->tdls_chandef;

		/* note this always fills and returns &tmp if compat */
		compat = ieee80211_chanreq_compatible(&tdls_chanreq,
						      compat, &tmp);
		if (WARN_ON_ONCE(!compat))
			return;
	}

	ieee80211_change_chanctx(local, ctx, ctx, compat);
}

static void ieee80211_recalc_radar_chanctx(struct ieee80211_local *local,
					   struct ieee80211_chanctx *chanctx)
{
	bool radar_enabled;

	lockdep_assert_wiphy(local->hw.wiphy);

	radar_enabled = ieee80211_chanctx_radar_required(local, chanctx);

	if (radar_enabled == chanctx->conf.radar_enabled)
		return;

	chanctx->conf.radar_enabled = radar_enabled;

	drv_change_chanctx(local, chanctx, IEEE80211_CHANCTX_CHANGE_RADAR);
}

static int ieee80211_assign_link_chanctx(struct ieee80211_link_data *link,
					 struct ieee80211_chanctx *new_ctx,
					 bool assign_on_failure)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *curr_ctx = NULL;
	bool new_idle;
	int ret;

	if (WARN_ON(sdata->vif.type == NL80211_IFTYPE_NAN))
		return -EOPNOTSUPP;

	conf = rcu_dereference_protected(link->conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));

	if (conf) {
		curr_ctx = container_of(conf, struct ieee80211_chanctx, conf);

		drv_unassign_vif_chanctx(local, sdata, link->conf, curr_ctx);
		conf = NULL;
		list_del(&link->assigned_chanctx_list);
	}

	if (new_ctx) {
		/* recalc considering the link we'll use it for now */
		ieee80211_recalc_chanctx_min_def(local, new_ctx, link, false);

		ret = drv_assign_vif_chanctx(local, sdata, link->conf, new_ctx);
		if (assign_on_failure || !ret) {
			/* Need to continue, see _ieee80211_set_active_links */
			WARN_ON_ONCE(ret && !local->in_reconfig);
			ret = 0;

			/* succeeded, so commit it to the data structures */
			conf = &new_ctx->conf;
			list_add(&link->assigned_chanctx_list,
				 &new_ctx->assigned_links);
		}
	} else {
		ret = 0;
	}

	rcu_assign_pointer(link->conf->chanctx_conf, conf);

	if (curr_ctx && ieee80211_chanctx_num_assigned(local, curr_ctx) > 0) {
		ieee80211_recalc_chanctx_chantype(local, curr_ctx);
		ieee80211_recalc_smps_chanctx(local, curr_ctx);
		ieee80211_recalc_radar_chanctx(local, curr_ctx);
		ieee80211_recalc_chanctx_min_def(local, curr_ctx, NULL, false);
	}

	if (new_ctx && ieee80211_chanctx_num_assigned(local, new_ctx) > 0) {
		ieee80211_recalc_txpower(link, false);
		ieee80211_recalc_chanctx_min_def(local, new_ctx, NULL, false);
	}

	if (conf) {
		new_idle = false;
	} else {
		struct ieee80211_link_data *tmp;

		new_idle = true;
		for_each_sdata_link(local, tmp) {
			if (rcu_access_pointer(tmp->conf->chanctx_conf)) {
				new_idle = false;
				break;
			}
		}
	}

	if (new_idle != sdata->vif.cfg.idle) {
		sdata->vif.cfg.idle = new_idle;

		if (sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR)
			ieee80211_vif_cfg_change_notify(sdata, BSS_CHANGED_IDLE);
	}

	ieee80211_check_fast_xmit_iface(sdata);

	return ret;
}

void ieee80211_recalc_smps_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *chanctx)
{
	struct ieee80211_sub_if_data *sdata;
	u8 rx_chains_static, rx_chains_dynamic;
	struct ieee80211_link_data *link;

	lockdep_assert_wiphy(local->hw.wiphy);

	rx_chains_static = 1;
	rx_chains_dynamic = 1;

	for_each_sdata_link(local, link) {
		u8 needed_static, needed_dynamic;

		switch (link->sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			if (!link->sdata->u.mgd.associated)
				continue;
			break;
		case NL80211_IFTYPE_MONITOR:
			if (!ieee80211_hw_check(&local->hw, NO_VIRTUAL_MONITOR))
				continue;
			break;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_MESH_POINT:
		case NL80211_IFTYPE_OCB:
			break;
		default:
			continue;
		}

		if (rcu_access_pointer(link->conf->chanctx_conf) != &chanctx->conf)
			continue;

		if (link->sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			rx_chains_dynamic = rx_chains_static = local->rx_chains;
			break;
		}

		switch (link->smps_mode) {
		default:
			WARN_ONCE(1, "Invalid SMPS mode %d\n",
				  link->smps_mode);
			fallthrough;
		case IEEE80211_SMPS_OFF:
			needed_static = link->needed_rx_chains;
			needed_dynamic = link->needed_rx_chains;
			break;
		case IEEE80211_SMPS_DYNAMIC:
			needed_static = 1;
			needed_dynamic = link->needed_rx_chains;
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
	sdata = wiphy_dereference(local->hw.wiphy, local->monitor_sdata);
	if (sdata &&
	    rcu_access_pointer(sdata->vif.bss_conf.chanctx_conf) == &chanctx->conf)
		rx_chains_dynamic = rx_chains_static = local->rx_chains;

	if (rx_chains_static == chanctx->conf.rx_chains_static &&
	    rx_chains_dynamic == chanctx->conf.rx_chains_dynamic)
		return;

	chanctx->conf.rx_chains_static = rx_chains_static;
	chanctx->conf.rx_chains_dynamic = rx_chains_dynamic;
	drv_change_chanctx(local, chanctx, IEEE80211_CHANCTX_CHANGE_RX_CHAINS);
}

static void
__ieee80211_link_copy_chanctx_to_vlans(struct ieee80211_link_data *link,
				       bool clear)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	unsigned int link_id = link->link_id;
	struct ieee80211_bss_conf *link_conf = link->conf;
	struct ieee80211_local *local __maybe_unused = sdata->local;
	struct ieee80211_sub_if_data *vlan;
	struct ieee80211_chanctx_conf *conf;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP))
		return;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* Check that conf exists, even when clearing this function
	 * must be called with the AP's channel context still there
	 * as it would otherwise cause VLANs to have an invalid
	 * channel context pointer for a while, possibly pointing
	 * to a channel context that has already been freed.
	 */
	conf = rcu_dereference_protected(link_conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));
	WARN_ON(!conf);

	if (clear)
		conf = NULL;

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
		struct ieee80211_bss_conf *vlan_conf;

		vlan_conf = wiphy_dereference(local->hw.wiphy,
					      vlan->vif.link_conf[link_id]);
		if (WARN_ON(!vlan_conf))
			continue;

		rcu_assign_pointer(vlan_conf->chanctx_conf, conf);
	}
}

void ieee80211_link_copy_chanctx_to_vlans(struct ieee80211_link_data *link,
					  bool clear)
{
	struct ieee80211_local *local = link->sdata->local;

	lockdep_assert_wiphy(local->hw.wiphy);

	__ieee80211_link_copy_chanctx_to_vlans(link, clear);
}

int ieee80211_link_unreserve_chanctx(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_chanctx *ctx = link->reserved_chanctx;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (WARN_ON(!ctx))
		return -EINVAL;

	list_del(&link->reserved_chanctx_list);
	link->reserved_chanctx = NULL;

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
			ieee80211_free_chanctx(sdata->local, ctx, false);
		}
	}

	return 0;
}

static struct ieee80211_chanctx *
ieee80211_replace_chanctx(struct ieee80211_local *local,
			  const struct ieee80211_chan_req *chanreq,
			  enum ieee80211_chanctx_mode mode,
			  struct ieee80211_chanctx *curr_ctx)
{
	struct ieee80211_chanctx *new_ctx, *ctx;
	struct wiphy *wiphy = local->hw.wiphy;
	const struct wiphy_radio *radio;

	if (!curr_ctx || (curr_ctx->replace_state ==
			  IEEE80211_CHANCTX_WILL_BE_REPLACED) ||
	    !list_empty(&curr_ctx->reserved_links)) {
		/*
		 * Another link already requested this context for a
		 * reservation. Find another one hoping all links assigned
		 * to it will also switch soon enough.
		 *
		 * TODO: This needs a little more work as some cases
		 * (more than 2 chanctx capable devices) may fail which could
		 * otherwise succeed provided some channel context juggling was
		 * performed.
		 *
		 * Consider ctx1..3, link1..6, each ctx has 2 links. link1 and
		 * link2 from ctx1 request new different chandefs starting 2
		 * in-place reservations with ctx4 and ctx5 replacing ctx1 and
		 * ctx2 respectively. Next link5 and link6 from ctx3 reserve
		 * ctx4. If link3 and link4 remain on ctx2 as they are then this
		 * fails unless `replace_ctx` from ctx5 is replaced with ctx3.
		 */
		list_for_each_entry(ctx, &local->chanctx_list, list) {
			if (ctx->replace_state !=
			    IEEE80211_CHANCTX_REPLACE_NONE)
				continue;

			if (!list_empty(&ctx->reserved_links))
				continue;

			if (ctx->conf.radio_idx >= 0) {
				radio = &wiphy->radio[ctx->conf.radio_idx];
				if (!cfg80211_radio_chandef_valid(radio, &chanreq->oper))
					continue;
			}

			curr_ctx = ctx;
			break;
		}
	}

	/*
	 * If that's true then all available contexts already have reservations
	 * and cannot be used.
	 */
	if (!curr_ctx || (curr_ctx->replace_state ==
			  IEEE80211_CHANCTX_WILL_BE_REPLACED) ||
	    !list_empty(&curr_ctx->reserved_links))
		return ERR_PTR(-EBUSY);

	new_ctx = ieee80211_alloc_chanctx(local, chanreq, mode, -1);
	if (!new_ctx)
		return ERR_PTR(-ENOMEM);

	new_ctx->replace_ctx = curr_ctx;
	new_ctx->replace_state = IEEE80211_CHANCTX_REPLACES_OTHER;

	curr_ctx->replace_ctx = new_ctx;
	curr_ctx->replace_state = IEEE80211_CHANCTX_WILL_BE_REPLACED;

	list_add_rcu(&new_ctx->list, &local->chanctx_list);

	return new_ctx;
}

static bool
ieee80211_find_available_radio(struct ieee80211_local *local,
			       const struct ieee80211_chan_req *chanreq,
			       u32 radio_mask, int *radio_idx)
{
	struct wiphy *wiphy = local->hw.wiphy;
	const struct wiphy_radio *radio;
	int i;

	*radio_idx = -1;
	if (!wiphy->n_radio)
		return true;

	for (i = 0; i < wiphy->n_radio; i++) {
		if (!(radio_mask & BIT(i)))
			continue;

		radio = &wiphy->radio[i];
		if (!cfg80211_radio_chandef_valid(radio, &chanreq->oper))
			continue;

		if (!ieee80211_can_create_new_chanctx(local, i))
			continue;

		*radio_idx = i;
		return true;
	}

	return false;
}

int ieee80211_link_reserve_chanctx(struct ieee80211_link_data *link,
				   const struct ieee80211_chan_req *chanreq,
				   enum ieee80211_chanctx_mode mode,
				   bool radar_required)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *new_ctx, *curr_ctx;
	int radio_idx;

	lockdep_assert_wiphy(local->hw.wiphy);

	curr_ctx = ieee80211_link_get_chanctx(link);
	if (curr_ctx && !local->ops->switch_vif_chanctx)
		return -EOPNOTSUPP;

	new_ctx = ieee80211_find_reservation_chanctx(local, chanreq, mode);
	if (!new_ctx) {
		if (ieee80211_can_create_new_chanctx(local, -1) &&
		    ieee80211_find_available_radio(local, chanreq,
						   sdata->wdev.radio_mask,
						   &radio_idx))
			new_ctx = ieee80211_new_chanctx(local, chanreq, mode,
							false, radio_idx);
		else
			new_ctx = ieee80211_replace_chanctx(local, chanreq,
							    mode, curr_ctx);
		if (IS_ERR(new_ctx))
			return PTR_ERR(new_ctx);
	}

	list_add(&link->reserved_chanctx_list, &new_ctx->reserved_links);
	link->reserved_chanctx = new_ctx;
	link->reserved = *chanreq;
	link->reserved_radar_required = radar_required;
	link->reserved_ready = false;

	return 0;
}

static void
ieee80211_link_chanctx_reservation_complete(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_OCB:
		wiphy_work_queue(sdata->local->hw.wiphy,
				 &link->csa.finalize_work);
		break;
	case NL80211_IFTYPE_STATION:
		wiphy_delayed_work_queue(sdata->local->hw.wiphy,
					 &link->u.mgd.csa.switch_work, 0);
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
ieee80211_link_update_chanreq(struct ieee80211_link_data *link,
			      const struct ieee80211_chan_req *chanreq)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	unsigned int link_id = link->link_id;
	struct ieee80211_sub_if_data *vlan;

	link->conf->chanreq = *chanreq;

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return;

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
		struct ieee80211_bss_conf *vlan_conf;

		vlan_conf = wiphy_dereference(sdata->local->hw.wiphy,
					      vlan->vif.link_conf[link_id]);
		if (WARN_ON(!vlan_conf))
			continue;

		vlan_conf->chanreq = *chanreq;
	}
}

static int
ieee80211_link_use_reserved_reassign(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_bss_conf *link_conf = link->conf;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_vif_chanctx_switch vif_chsw[1] = {};
	struct ieee80211_chanctx *old_ctx, *new_ctx;
	const struct ieee80211_chan_req *chanreq;
	struct ieee80211_chan_req tmp;
	u64 changed = 0;
	int err;

	lockdep_assert_wiphy(local->hw.wiphy);

	new_ctx = link->reserved_chanctx;
	old_ctx = ieee80211_link_get_chanctx(link);

	if (WARN_ON(!link->reserved_ready))
		return -EBUSY;

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(!old_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_REPLACES_OTHER))
		return -EINVAL;

	chanreq = ieee80211_chanctx_non_reserved_chandef(local, new_ctx,
							 &link->reserved,
							 &tmp);
	if (WARN_ON(!chanreq))
		return -EINVAL;

	if (link_conf->chanreq.oper.width != link->reserved.oper.width)
		changed = BSS_CHANGED_BANDWIDTH;

	ieee80211_link_update_chanreq(link, &link->reserved);

	_ieee80211_change_chanctx(local, new_ctx, old_ctx, chanreq, link);

	vif_chsw[0].vif = &sdata->vif;
	vif_chsw[0].old_ctx = &old_ctx->conf;
	vif_chsw[0].new_ctx = &new_ctx->conf;
	vif_chsw[0].link_conf = link->conf;

	list_del(&link->reserved_chanctx_list);
	link->reserved_chanctx = NULL;

	err = drv_switch_vif_chanctx(local, vif_chsw, 1,
				     CHANCTX_SWMODE_REASSIGN_VIF);
	if (err) {
		if (ieee80211_chanctx_refcount(local, new_ctx) == 0)
			ieee80211_free_chanctx(local, new_ctx, false);

		goto out;
	}

	list_move(&link->assigned_chanctx_list, &new_ctx->assigned_links);
	rcu_assign_pointer(link_conf->chanctx_conf, &new_ctx->conf);

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		__ieee80211_link_copy_chanctx_to_vlans(link, false);

	ieee80211_check_fast_xmit_iface(sdata);

	if (ieee80211_chanctx_refcount(local, old_ctx) == 0)
		ieee80211_free_chanctx(local, old_ctx, false);

	ieee80211_recalc_chanctx_min_def(local, new_ctx, NULL, false);
	ieee80211_recalc_smps_chanctx(local, new_ctx);
	ieee80211_recalc_radar_chanctx(local, new_ctx);

	if (changed)
		ieee80211_link_info_change_notify(sdata, link, changed);

out:
	ieee80211_link_chanctx_reservation_complete(link);
	return err;
}

static int
ieee80211_link_use_reserved_assign(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *old_ctx, *new_ctx;
	const struct ieee80211_chan_req *chanreq;
	struct ieee80211_chan_req tmp;
	int err;

	old_ctx = ieee80211_link_get_chanctx(link);
	new_ctx = link->reserved_chanctx;

	if (WARN_ON(!link->reserved_ready))
		return -EINVAL;

	if (WARN_ON(old_ctx))
		return -EINVAL;

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_REPLACES_OTHER))
		return -EINVAL;

	chanreq = ieee80211_chanctx_non_reserved_chandef(local, new_ctx,
							 &link->reserved,
							 &tmp);
	if (WARN_ON(!chanreq))
		return -EINVAL;

	ieee80211_change_chanctx(local, new_ctx, new_ctx, chanreq);

	list_del(&link->reserved_chanctx_list);
	link->reserved_chanctx = NULL;

	err = ieee80211_assign_link_chanctx(link, new_ctx, false);
	if (err) {
		if (ieee80211_chanctx_refcount(local, new_ctx) == 0)
			ieee80211_free_chanctx(local, new_ctx, false);

		goto out;
	}

out:
	ieee80211_link_chanctx_reservation_complete(link);
	return err;
}

static bool
ieee80211_link_has_in_place_reservation(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_chanctx *old_ctx, *new_ctx;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	new_ctx = link->reserved_chanctx;
	old_ctx = ieee80211_link_get_chanctx(link);

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

static int ieee80211_chsw_switch_vifs(struct ieee80211_local *local,
				      int n_vifs)
{
	struct ieee80211_vif_chanctx_switch *vif_chsw;
	struct ieee80211_link_data *link;
	struct ieee80211_chanctx *ctx, *old_ctx;
	int i, err;

	lockdep_assert_wiphy(local->hw.wiphy);

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

		list_for_each_entry(link, &ctx->reserved_links,
				    reserved_chanctx_list) {
			if (!ieee80211_link_has_in_place_reservation(link))
				continue;

			old_ctx = ieee80211_link_get_chanctx(link);
			vif_chsw[i].vif = &link->sdata->vif;
			vif_chsw[i].old_ctx = &old_ctx->conf;
			vif_chsw[i].new_ctx = &ctx->conf;
			vif_chsw[i].link_conf = link->conf;

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

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (!list_empty(&ctx->replace_ctx->assigned_links))
			continue;

		ieee80211_del_chanctx(local, ctx->replace_ctx, false);
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

		if (!list_empty(&ctx->replace_ctx->assigned_links))
			continue;

		ieee80211_del_chanctx(local, ctx, false);
		WARN_ON(ieee80211_add_chanctx(local, ctx->replace_ctx));
	}

	return err;
}

static int ieee80211_vif_use_reserved_switch(struct ieee80211_local *local)
{
	struct ieee80211_chanctx *ctx, *ctx_tmp, *old_ctx;
	int err, n_assigned, n_reserved, n_ready;
	int n_ctx = 0, n_vifs_switch = 0, n_vifs_assign = 0, n_vifs_ctxless = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

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
		struct ieee80211_link_data *link;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto err;
		}

		n_ctx++;

		n_assigned = 0;
		n_reserved = 0;
		n_ready = 0;

		list_for_each_entry(link, &ctx->replace_ctx->assigned_links,
				    assigned_chanctx_list) {
			n_assigned++;
			if (link->reserved_chanctx) {
				n_reserved++;
				if (link->reserved_ready)
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
		list_for_each_entry(link, &ctx->reserved_links,
				    reserved_chanctx_list) {
			if (ieee80211_link_has_in_place_reservation(link) &&
			    !link->reserved_ready)
				return -EAGAIN;

			old_ctx = ieee80211_link_get_chanctx(link);
			if (old_ctx) {
				if (old_ctx->replace_state ==
				    IEEE80211_CHANCTX_WILL_BE_REPLACED)
					n_vifs_switch++;
				else
					n_vifs_assign++;
			} else {
				n_vifs_ctxless++;
			}

			if (link->reserved_radar_required)
				ctx->conf.radar_enabled = true;
		}
	}

	if (WARN_ON(n_ctx == 0) ||
	    WARN_ON(n_vifs_switch == 0 &&
		    n_vifs_assign == 0 &&
		    n_vifs_ctxless == 0)) {
		err = -EINVAL;
		goto err;
	}

	/* update station rate control and min width before switch */
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		struct ieee80211_link_data *link;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto err;
		}

		list_for_each_entry(link, &ctx->reserved_links,
				    reserved_chanctx_list) {
			if (!ieee80211_link_has_in_place_reservation(link))
				continue;

			ieee80211_chan_bw_change(local,
						 ieee80211_link_get_chanctx(link),
						 true, true);
		}

		ieee80211_recalc_chanctx_min_def(local, ctx, NULL, true);
	}

	/*
	 * All necessary vifs are ready. Perform the switch now depending on
	 * reservations and driver capabilities.
	 */

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

	/*
	 * Update all structures, values and pointers to point to new channel
	 * context(s).
	 */
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		struct ieee80211_link_data *link, *link_tmp;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		if (WARN_ON(!ctx->replace_ctx)) {
			err = -EINVAL;
			goto err;
		}

		list_for_each_entry(link, &ctx->reserved_links,
				    reserved_chanctx_list) {
			struct ieee80211_sub_if_data *sdata = link->sdata;
			struct ieee80211_bss_conf *link_conf = link->conf;
			u64 changed = 0;

			if (!ieee80211_link_has_in_place_reservation(link))
				continue;

			rcu_assign_pointer(link_conf->chanctx_conf,
					   &ctx->conf);

			if (sdata->vif.type == NL80211_IFTYPE_AP)
				__ieee80211_link_copy_chanctx_to_vlans(link,
								       false);

			ieee80211_check_fast_xmit_iface(sdata);

			link->radar_required = link->reserved_radar_required;

			if (link_conf->chanreq.oper.width != link->reserved.oper.width)
				changed = BSS_CHANGED_BANDWIDTH;

			ieee80211_link_update_chanreq(link, &link->reserved);
			if (changed)
				ieee80211_link_info_change_notify(sdata,
								  link,
								  changed);

			ieee80211_recalc_txpower(link, false);
		}

		ieee80211_recalc_chanctx_chantype(local, ctx);
		ieee80211_recalc_smps_chanctx(local, ctx);
		ieee80211_recalc_radar_chanctx(local, ctx);
		ieee80211_recalc_chanctx_min_def(local, ctx, NULL, false);

		list_for_each_entry_safe(link, link_tmp, &ctx->reserved_links,
					 reserved_chanctx_list) {
			if (ieee80211_link_get_chanctx(link) != ctx)
				continue;

			list_del(&link->reserved_chanctx_list);
			list_move(&link->assigned_chanctx_list,
				  &ctx->assigned_links);
			link->reserved_chanctx = NULL;

			ieee80211_link_chanctx_reservation_complete(link);
			ieee80211_chan_bw_change(local, ctx, false, false);
		}

		/*
		 * This context might have been a dependency for an already
		 * ready re-assign reservation interface that was deferred. Do
		 * not propagate error to the caller though. The in-place
		 * reservation for originally requested interface has already
		 * succeeded at this point.
		 */
		list_for_each_entry_safe(link, link_tmp, &ctx->reserved_links,
					 reserved_chanctx_list) {
			if (WARN_ON(ieee80211_link_has_in_place_reservation(link)))
				continue;

			if (WARN_ON(link->reserved_chanctx != ctx))
				continue;

			if (!link->reserved_ready)
				continue;

			if (ieee80211_link_get_chanctx(link))
				err = ieee80211_link_use_reserved_reassign(link);
			else
				err = ieee80211_link_use_reserved_assign(link);

			if (err) {
				link_info(link,
					  "failed to finalize (re-)assign reservation (err=%d)\n",
					  err);
				ieee80211_link_unreserve_chanctx(link);
				cfg80211_stop_iface(local->hw.wiphy,
						    &link->sdata->wdev,
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
		struct ieee80211_link_data *link, *link_tmp;

		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		list_for_each_entry_safe(link, link_tmp, &ctx->reserved_links,
					 reserved_chanctx_list) {
			ieee80211_link_unreserve_chanctx(link);
			ieee80211_link_chanctx_reservation_complete(link);
		}
	}

	return err;
}

void __ieee80211_link_release_channel(struct ieee80211_link_data *link,
				      bool skip_idle_recalc)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_bss_conf *link_conf = link->conf;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	bool use_reserved_switch = false;

	lockdep_assert_wiphy(local->hw.wiphy);

	conf = rcu_dereference_protected(link_conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));
	if (!conf)
		return;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (link->reserved_chanctx) {
		if (link->reserved_chanctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER &&
		    ieee80211_chanctx_num_reserved(local, link->reserved_chanctx) > 1)
			use_reserved_switch = true;

		ieee80211_link_unreserve_chanctx(link);
	}

	ieee80211_assign_link_chanctx(link, NULL, false);
	if (ieee80211_chanctx_refcount(local, ctx) == 0)
		ieee80211_free_chanctx(local, ctx, skip_idle_recalc);

	link->radar_required = false;

	/* Unreserving may ready an in-place reservation. */
	if (use_reserved_switch)
		ieee80211_vif_use_reserved_switch(local);
}

int _ieee80211_link_use_channel(struct ieee80211_link_data *link,
				const struct ieee80211_chan_req *chanreq,
				enum ieee80211_chanctx_mode mode,
				bool assign_on_failure)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	u8 radar_detect_width = 0;
	bool reserved = false;
	int radio_idx;
	int ret;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!ieee80211_vif_link_active(&sdata->vif, link->link_id)) {
		ieee80211_link_update_chanreq(link, chanreq);
		return 0;
	}

	ret = cfg80211_chandef_dfs_required(local->hw.wiphy,
					    &chanreq->oper,
					    sdata->wdev.iftype);
	if (ret < 0)
		goto out;
	if (ret > 0)
		radar_detect_width = BIT(chanreq->oper.width);

	link->radar_required = ret;

	ret = ieee80211_check_combinations(sdata, &chanreq->oper, mode,
					   radar_detect_width, -1);
	if (ret < 0)
		goto out;

	__ieee80211_link_release_channel(link, false);

	ctx = ieee80211_find_chanctx(local, link, chanreq, mode);
	/* Note: context is now reserved */
	if (ctx)
		reserved = true;
	else if (!ieee80211_find_available_radio(local, chanreq,
						 sdata->wdev.radio_mask,
						 &radio_idx))
		ctx = ERR_PTR(-EBUSY);
	else
		ctx = ieee80211_new_chanctx(local, chanreq, mode,
					    assign_on_failure, radio_idx);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	ieee80211_link_update_chanreq(link, chanreq);

	ret = ieee80211_assign_link_chanctx(link, ctx, assign_on_failure);

	if (reserved) {
		/* remove reservation */
		WARN_ON(link->reserved_chanctx != ctx);
		link->reserved_chanctx = NULL;
		list_del(&link->reserved_chanctx_list);
	}

	if (ret) {
		/* if assign fails refcount stays the same */
		if (ieee80211_chanctx_refcount(local, ctx) == 0)
			ieee80211_free_chanctx(local, ctx, false);
		goto out;
	}

	ieee80211_recalc_smps_chanctx(local, ctx);
	ieee80211_recalc_radar_chanctx(local, ctx);
 out:
	if (ret)
		link->radar_required = false;

	return ret;
}

int ieee80211_link_use_reserved_context(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *new_ctx;
	struct ieee80211_chanctx *old_ctx;
	int err;

	lockdep_assert_wiphy(local->hw.wiphy);

	new_ctx = link->reserved_chanctx;
	old_ctx = ieee80211_link_get_chanctx(link);

	if (WARN_ON(!new_ctx))
		return -EINVAL;

	if (WARN_ON(new_ctx->replace_state ==
		    IEEE80211_CHANCTX_WILL_BE_REPLACED))
		return -EINVAL;

	if (WARN_ON(link->reserved_ready))
		return -EINVAL;

	link->reserved_ready = true;

	if (new_ctx->replace_state == IEEE80211_CHANCTX_REPLACE_NONE) {
		if (old_ctx)
			return ieee80211_link_use_reserved_reassign(link);

		return ieee80211_link_use_reserved_assign(link);
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

/*
 * This is similar to ieee80211_chanctx_compatible(), but rechecks
 * against all the links actually using it (except the one that's
 * passed, since that one is changing).
 * This is done in order to allow changes to the AP's bandwidth for
 * wider bandwidth OFDMA purposes, which wouldn't be treated as
 * compatible by ieee80211_chanctx_recheck() but is OK if the link
 * requesting the update is the only one using it.
 */
static const struct ieee80211_chan_req *
ieee80211_chanctx_recheck(struct ieee80211_local *local,
			  struct ieee80211_link_data *skip_link,
			  struct ieee80211_chanctx *ctx,
			  const struct ieee80211_chan_req *req,
			  struct ieee80211_chan_req *tmp)
{
	const struct ieee80211_chan_req *ret = req;
	struct ieee80211_link_data *link;

	lockdep_assert_wiphy(local->hw.wiphy);

	for_each_sdata_link(local, link) {
		if (link == skip_link)
			continue;

		if (rcu_access_pointer(link->conf->chanctx_conf) == &ctx->conf) {
			ret = ieee80211_chanreq_compatible(ret,
							   &link->conf->chanreq,
							   tmp);
			if (!ret)
				return NULL;
		}

		if (link->reserved_chanctx == ctx) {
			ret = ieee80211_chanreq_compatible(ret,
							   &link->reserved,
							   tmp);
			if (!ret)
				return NULL;
		}
	}

	*tmp = *ret;
	return tmp;
}

int ieee80211_link_change_chanreq(struct ieee80211_link_data *link,
				  const struct ieee80211_chan_req *chanreq,
				  u64 *changed)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_bss_conf *link_conf = link->conf;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	const struct ieee80211_chan_req *compat;
	struct ieee80211_chan_req tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!cfg80211_chandef_usable(sdata->local->hw.wiphy,
				     &chanreq->oper,
				     IEEE80211_CHAN_DISABLED))
		return -EINVAL;

	/* for non-HT 20 MHz the rest doesn't matter */
	if (chanreq->oper.width == NL80211_CHAN_WIDTH_20_NOHT &&
	    cfg80211_chandef_identical(&chanreq->oper, &link_conf->chanreq.oper))
		return 0;

	/* but you cannot switch to/from it */
	if (chanreq->oper.width == NL80211_CHAN_WIDTH_20_NOHT ||
	    link_conf->chanreq.oper.width == NL80211_CHAN_WIDTH_20_NOHT)
		return -EINVAL;

	conf = rcu_dereference_protected(link_conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));
	if (!conf)
		return -EINVAL;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	compat = ieee80211_chanctx_recheck(local, link, ctx, chanreq, &tmp);
	if (!compat)
		return -EINVAL;

	switch (ctx->replace_state) {
	case IEEE80211_CHANCTX_REPLACE_NONE:
		if (!ieee80211_chanctx_reserved_chanreq(local, ctx, compat,
							&tmp))
			return -EBUSY;
		break;
	case IEEE80211_CHANCTX_WILL_BE_REPLACED:
		/* TODO: Perhaps the bandwidth change could be treated as a
		 * reservation itself? */
		return -EBUSY;
	case IEEE80211_CHANCTX_REPLACES_OTHER:
		/* channel context that is going to replace another channel
		 * context doesn't really exist and shouldn't be assigned
		 * anywhere yet */
		WARN_ON(1);
		break;
	}

	ieee80211_link_update_chanreq(link, chanreq);

	ieee80211_recalc_chanctx_chantype(local, ctx);

	*changed |= BSS_CHANGED_BANDWIDTH;
	return 0;
}

void ieee80211_link_release_channel(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		return;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (rcu_access_pointer(link->conf->chanctx_conf))
		__ieee80211_link_release_channel(link, false);
}

void ieee80211_link_vlan_copy_chanctx(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	unsigned int link_id = link->link_id;
	struct ieee80211_bss_conf *link_conf = link->conf;
	struct ieee80211_bss_conf *ap_conf;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *ap;
	struct ieee80211_chanctx_conf *conf;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP_VLAN || !sdata->bss))
		return;

	ap = container_of(sdata->bss, struct ieee80211_sub_if_data, u.ap);

	ap_conf = wiphy_dereference(local->hw.wiphy,
				    ap->vif.link_conf[link_id]);
	conf = wiphy_dereference(local->hw.wiphy,
				 ap_conf->chanctx_conf);
	rcu_assign_pointer(link_conf->chanctx_conf, conf);
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

void ieee80211_iter_chan_contexts_mtx(
	struct ieee80211_hw *hw,
	void (*iter)(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *chanctx_conf,
		     void *data),
	void *iter_data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(hw->wiphy);

	list_for_each_entry(ctx, &local->chanctx_list, list)
		if (ctx->driver_present)
			iter(hw, &ctx->conf, iter_data);
}
EXPORT_SYMBOL_GPL(ieee80211_iter_chan_contexts_mtx);
