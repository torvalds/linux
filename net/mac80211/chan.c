/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

static bool
ieee80211_channel_types_are_compatible(enum nl80211_channel_type chantype1,
				       enum nl80211_channel_type chantype2,
				       enum nl80211_channel_type *compat)
{
	/*
	 * start out with chantype1 being the result,
	 * overwriting later if needed
	 */
	if (compat)
		*compat = chantype1;

	switch (chantype1) {
	case NL80211_CHAN_NO_HT:
		if (compat)
			*compat = chantype2;
		break;
	case NL80211_CHAN_HT20:
		/*
		 * allow any change that doesn't go to no-HT
		 * (if it already is no-HT no change is needed)
		 */
		if (chantype2 == NL80211_CHAN_NO_HT)
			break;
		if (compat)
			*compat = chantype2;
		break;
	case NL80211_CHAN_HT40PLUS:
	case NL80211_CHAN_HT40MINUS:
		/* allow smaller bandwidth and same */
		if (chantype2 == NL80211_CHAN_NO_HT)
			break;
		if (chantype2 == NL80211_CHAN_HT20)
			break;
		if (chantype2 == chantype1)
			break;
		return false;
	}

	return true;
}

static void ieee80211_change_chantype(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx,
				      enum nl80211_channel_type chantype)
{
	if (chantype == ctx->conf.channel_type)
		return;

	ctx->conf.channel_type = chantype;
	drv_change_chanctx(local, ctx, IEEE80211_CHANCTX_CHANGE_CHANNEL_TYPE);

	if (!local->use_chanctx) {
		local->_oper_channel_type = chantype;
		ieee80211_hw_config(local, 0);
	}
}

static struct ieee80211_chanctx *
ieee80211_find_chanctx(struct ieee80211_local *local,
		       struct ieee80211_channel *channel,
		       enum nl80211_channel_type channel_type,
		       enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;
	enum nl80211_channel_type compat_type;

	lockdep_assert_held(&local->chanctx_mtx);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;
	if (WARN_ON(!channel))
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		compat_type = ctx->conf.channel_type;

		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;
		if (ctx->conf.channel != channel)
			continue;
		if (!ieee80211_channel_types_are_compatible(ctx->conf.channel_type,
							    channel_type,
							    &compat_type))
			continue;

		ieee80211_change_chantype(local, ctx, compat_type);

		return ctx;
	}

	return NULL;
}

static struct ieee80211_chanctx *
ieee80211_new_chanctx(struct ieee80211_local *local,
		      struct ieee80211_channel *channel,
		      enum nl80211_channel_type channel_type,
		      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;
	int err;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx = kzalloc(sizeof(*ctx) + local->hw.chanctx_data_size, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->conf.channel = channel;
	ctx->conf.channel_type = channel_type;
	ctx->conf.rx_chains_static = 1;
	ctx->conf.rx_chains_dynamic = 1;
	ctx->mode = mode;

	if (!local->use_chanctx) {
		local->_oper_channel_type = channel_type;
		local->_oper_channel = channel;
		ieee80211_hw_config(local, 0);
	} else {
		err = drv_add_chanctx(local, ctx);
		if (err) {
			kfree(ctx);
			return ERR_PTR(err);
		}
	}

	list_add_rcu(&ctx->list, &local->chanctx_list);

	return ctx;
}

static void ieee80211_free_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx)
{
	lockdep_assert_held(&local->chanctx_mtx);

	WARN_ON_ONCE(ctx->refcount != 0);

	if (!local->use_chanctx) {
		local->_oper_channel_type = NL80211_CHAN_NO_HT;
		ieee80211_hw_config(local, 0);
	} else {
		drv_remove_chanctx(local, ctx);
	}

	list_del_rcu(&ctx->list);
	kfree_rcu(ctx, rcu_head);
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

	return 0;
}

static enum nl80211_channel_type
ieee80211_calc_chantype(struct ieee80211_local *local,
			struct ieee80211_chanctx *ctx)
{
	struct ieee80211_chanctx_conf *conf = &ctx->conf;
	struct ieee80211_sub_if_data *sdata;
	enum nl80211_channel_type result = NL80211_CHAN_NO_HT;

	lockdep_assert_held(&local->chanctx_mtx);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (rcu_access_pointer(sdata->vif.chanctx_conf) != conf)
			continue;

		WARN_ON_ONCE(!ieee80211_channel_types_are_compatible(
					sdata->vif.bss_conf.channel_type,
					result, &result));
	}
	rcu_read_unlock();

	return result;
}

static void ieee80211_recalc_chanctx_chantype(struct ieee80211_local *local,
					      struct ieee80211_chanctx *ctx)
{
	enum nl80211_channel_type chantype;

	lockdep_assert_held(&local->chanctx_mtx);

	chantype = ieee80211_calc_chantype(local, ctx);
	ieee80211_change_chantype(local, ctx, chantype);
}

static void ieee80211_unassign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_chanctx *ctx)
{
	struct ieee80211_local *local = sdata->local;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx->refcount--;
	rcu_assign_pointer(sdata->vif.chanctx_conf, NULL);

	drv_unassign_vif_chanctx(local, sdata, ctx);

	if (ctx->refcount > 0) {
		ieee80211_recalc_chanctx_chantype(sdata->local, ctx);
		ieee80211_recalc_smps_chanctx(local, ctx);
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
			      struct ieee80211_channel *channel,
			      enum nl80211_channel_type channel_type,
			      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	int ret;

	WARN_ON(sdata->dev && netif_carrier_ok(sdata->dev));

	mutex_lock(&local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);

	ctx = ieee80211_find_chanctx(local, channel, channel_type, mode);
	if (!ctx)
		ctx = ieee80211_new_chanctx(local, channel, channel_type, mode);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	sdata->vif.bss_conf.channel_type = channel_type;

	ret = ieee80211_assign_vif_chanctx(sdata, ctx);
	if (ret) {
		/* if assign fails refcount stays the same */
		if (ctx->refcount == 0)
			ieee80211_free_chanctx(local, ctx);
		goto out;
	}

	ieee80211_recalc_smps_chanctx(local, ctx);
 out:
	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

void ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata)
{
	WARN_ON(sdata->dev && netif_carrier_ok(sdata->dev));

	mutex_lock(&sdata->local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);
	mutex_unlock(&sdata->local->chanctx_mtx);
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
		iter(hw, &ctx->conf, iter_data);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iter_chan_contexts_atomic);
