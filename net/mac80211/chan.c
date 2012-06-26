/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"

static enum ieee80211_chan_mode
__ieee80211_get_channel_mode(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *ignore)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->iflist_mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata == ignore)
			continue;

		if (!ieee80211_sdata_running(sdata))
			continue;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
			continue;
		case NL80211_IFTYPE_STATION:
			if (!sdata->u.mgd.associated)
				continue;
			break;
		case NL80211_IFTYPE_ADHOC:
			if (!sdata->u.ibss.ssid_len)
				continue;
			if (!sdata->u.ibss.fixed_channel)
				return CHAN_MODE_HOPPING;
			break;
		case NL80211_IFTYPE_AP_VLAN:
			/* will also have _AP interface */
			continue;
		case NL80211_IFTYPE_AP:
			if (!sdata->u.ap.beacon)
				continue;
			break;
		case NL80211_IFTYPE_MESH_POINT:
			if (!sdata->wdev.mesh_id_len)
				continue;
			break;
		default:
			break;
		}

		return CHAN_MODE_FIXED;
	}

	return CHAN_MODE_UNDEFINED;
}

enum ieee80211_chan_mode
ieee80211_get_channel_mode(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *ignore)
{
	enum ieee80211_chan_mode mode;

	mutex_lock(&local->iflist_mtx);
	mode = __ieee80211_get_channel_mode(local, ignore);
	mutex_unlock(&local->iflist_mtx);

	return mode;
}

static enum nl80211_channel_type
ieee80211_get_superchan(struct ieee80211_local *local,
			struct ieee80211_sub_if_data *sdata)
{
	enum nl80211_channel_type superchan = NL80211_CHAN_NO_HT;
	struct ieee80211_sub_if_data *tmp;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(tmp, &local->interfaces, list) {
		if (tmp == sdata)
			continue;

		if (!ieee80211_sdata_running(tmp))
			continue;

		switch (tmp->vif.bss_conf.channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			if (superchan > tmp->vif.bss_conf.channel_type)
				break;

			superchan = tmp->vif.bss_conf.channel_type;
			break;
		case NL80211_CHAN_HT40PLUS:
			WARN_ON(superchan == NL80211_CHAN_HT40MINUS);
			superchan = NL80211_CHAN_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			WARN_ON(superchan == NL80211_CHAN_HT40PLUS);
			superchan = NL80211_CHAN_HT40MINUS;
			break;
		}
	}
	mutex_unlock(&local->iflist_mtx);

	return superchan;
}

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

bool ieee80211_set_channel_type(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				enum nl80211_channel_type chantype)
{
	enum nl80211_channel_type superchan;
	enum nl80211_channel_type compatchan;

	superchan = ieee80211_get_superchan(local, sdata);
	if (!ieee80211_channel_types_are_compatible(superchan, chantype,
						    &compatchan))
		return false;

	local->_oper_channel_type = compatchan;

	if (sdata)
		sdata->vif.bss_conf.channel_type = chantype;

	return true;

}

static struct ieee80211_chanctx *
ieee80211_find_chanctx(struct ieee80211_local *local,
		       struct ieee80211_channel *channel,
		       enum nl80211_channel_type channel_type,
		       enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_chanctx *ctx;

	lockdep_assert_held(&local->chanctx_mtx);

	if (mode == IEEE80211_CHANCTX_EXCLUSIVE)
		return NULL;
	if (WARN_ON(!channel))
		return NULL;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE)
			continue;
		if (ctx->conf.channel != channel)
			continue;
		if (ctx->conf.channel_type != channel_type)
			continue;

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

	lockdep_assert_held(&local->chanctx_mtx);

	ctx = kzalloc(sizeof(*ctx) + local->hw.chanctx_data_size, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->conf.channel = channel;
	ctx->conf.channel_type = channel_type;
	ctx->mode = mode;

	list_add(&ctx->list, &local->chanctx_list);

	return ctx;
}

static void ieee80211_free_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *ctx)
{
	lockdep_assert_held(&local->chanctx_mtx);

	WARN_ON_ONCE(ctx->refcount != 0);

	list_del(&ctx->list);
	kfree_rcu(ctx, rcu_head);
}

static int ieee80211_assign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_chanctx *ctx)
{
	struct ieee80211_local *local __maybe_unused = sdata->local;

	lockdep_assert_held(&local->chanctx_mtx);

	rcu_assign_pointer(sdata->vif.chanctx_conf, &ctx->conf);
	ctx->refcount++;

	return 0;
}

static void ieee80211_unassign_vif_chanctx(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_chanctx *ctx)
{
	struct ieee80211_local *local __maybe_unused = sdata->local;

	lockdep_assert_held(&local->chanctx_mtx);

	ctx->refcount--;
	rcu_assign_pointer(sdata->vif.chanctx_conf, NULL);
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

int ieee80211_vif_use_channel(struct ieee80211_sub_if_data *sdata,
			      struct ieee80211_channel *channel,
			      enum nl80211_channel_type channel_type,
			      enum ieee80211_chanctx_mode mode)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	int ret;

	mutex_lock(&local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);

	ctx = ieee80211_find_chanctx(local, channel, channel_type, mode);
	if (!ctx)
		ctx = ieee80211_new_chanctx(local, channel, channel_type, mode);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	ret = ieee80211_assign_vif_chanctx(sdata, ctx);
	if (ret) {
		/* if assign fails refcount stays the same */
		if (ctx->refcount == 0)
			ieee80211_free_chanctx(local, ctx);
		goto out;
	}

 out:
	mutex_unlock(&local->chanctx_mtx);
	return ret;
}

void ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata)
{
	mutex_lock(&sdata->local->chanctx_mtx);
	__ieee80211_vif_release_channel(sdata);
	mutex_unlock(&sdata->local->chanctx_mtx);
}
