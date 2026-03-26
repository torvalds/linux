// SPDX-License-Identifier: GPL-2.0-only
/*
 * NAN mode implementation
 * Copyright(c) 2025 Intel Corporation
 */
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"

static void
ieee80211_nan_init_channel(struct ieee80211_nan_channel *nan_channel,
			   struct cfg80211_nan_channel *cfg_nan_channel)
{
	memset(nan_channel, 0, sizeof(*nan_channel));

	nan_channel->chanreq.oper = cfg_nan_channel->chandef;
	memcpy(nan_channel->channel_entry, cfg_nan_channel->channel_entry,
	       sizeof(nan_channel->channel_entry));
	nan_channel->needed_rx_chains = cfg_nan_channel->rx_nss;
}

static void
ieee80211_nan_update_channel(struct ieee80211_local *local,
			     struct ieee80211_nan_channel *nan_channel,
			     struct cfg80211_nan_channel *cfg_nan_channel,
			     bool deferred)
{
	struct ieee80211_chanctx_conf *conf;
	bool reducing_nss;

	if (WARN_ON(!cfg80211_chandef_identical(&nan_channel->chanreq.oper,
						&cfg_nan_channel->chandef)))
		return;

	if (WARN_ON(memcmp(nan_channel->channel_entry,
			   cfg_nan_channel->channel_entry,
			   sizeof(nan_channel->channel_entry))))
		return;

	if (nan_channel->needed_rx_chains == cfg_nan_channel->rx_nss)
		return;

	reducing_nss = nan_channel->needed_rx_chains > cfg_nan_channel->rx_nss;
	nan_channel->needed_rx_chains = cfg_nan_channel->rx_nss;

	conf = nan_channel->chanctx_conf;

	/*
	 * If we are adding NSSs, we need to be ready before notifying the peer,
	 * if we are reducing NSSs, we need to wait until the peer is notified.
	 */
	if (!conf || (deferred && reducing_nss))
		return;

	ieee80211_recalc_smps_chanctx(local, container_of(conf,
							  struct ieee80211_chanctx,
							  conf));
}

static int
ieee80211_nan_use_chanctx(struct ieee80211_sub_if_data *sdata,
			  struct ieee80211_nan_channel *nan_channel,
			  bool assign_on_failure)
{
	struct ieee80211_chanctx *ctx;
	bool reused_ctx;

	if (!nan_channel->chanreq.oper.chan)
		return -EINVAL;

	if (ieee80211_check_combinations(sdata, &nan_channel->chanreq.oper,
					 IEEE80211_CHANCTX_SHARED, 0, -1))
		return -EBUSY;

	ctx = ieee80211_find_or_create_chanctx(sdata, &nan_channel->chanreq,
					       IEEE80211_CHANCTX_SHARED,
					       assign_on_failure,
					       &reused_ctx);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	nan_channel->chanctx_conf = &ctx->conf;

	/*
	 * In case an existing channel context is being used, we marked it as
	 * will_be_used, now that it is assigned - clear this indication
	 */
	if (reused_ctx) {
		WARN_ON(!ctx->will_be_used);
		ctx->will_be_used = false;
	}
	ieee80211_recalc_chanctx_min_def(sdata->local, ctx);
	ieee80211_recalc_smps_chanctx(sdata->local, ctx);

	return 0;
}

static void
ieee80211_nan_remove_channel(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_nan_channel *nan_channel)
{
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;
	struct ieee80211_nan_sched_cfg *sched_cfg = &sdata->vif.cfg.nan_sched;

	if (WARN_ON(!nan_channel))
		return;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (!nan_channel->chanreq.oper.chan)
		return;

	for (int slot = 0; slot < ARRAY_SIZE(sched_cfg->schedule); slot++)
		if (sched_cfg->schedule[slot] == nan_channel)
			sched_cfg->schedule[slot] = NULL;

	conf = nan_channel->chanctx_conf;

	memset(nan_channel, 0, sizeof(*nan_channel));

	/* Update the driver before (possibly) releasing the channel context */
	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);

	/* Channel might not have a chanctx if it was ULWed */
	if (!conf)
		return;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (ieee80211_chanctx_num_assigned(sdata->local, ctx) > 0) {
		ieee80211_recalc_chanctx_chantype(sdata->local, ctx);
		ieee80211_recalc_smps_chanctx(sdata->local, ctx);
		ieee80211_recalc_chanctx_min_def(sdata->local, ctx);
	}

	if (ieee80211_chanctx_refcount(sdata->local, ctx) == 0)
		ieee80211_free_chanctx(sdata->local, ctx, false);
}

static struct ieee80211_nan_channel *
ieee80211_nan_find_free_channel(struct ieee80211_nan_sched_cfg *sched_cfg)
{
	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		if (!sched_cfg->channels[i].chanreq.oper.chan)
			return &sched_cfg->channels[i];
	}

	return NULL;
}

int ieee80211_nan_set_local_sched(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_nan_local_sched *sched)
{
	struct ieee80211_nan_channel *sched_idx_to_chan[IEEE80211_NAN_MAX_CHANNELS] = {};
	struct ieee80211_nan_sched_cfg *sched_cfg = &sdata->vif.cfg.nan_sched;
	struct ieee80211_nan_sched_cfg backup_sched;
	int ret;

	if (sched->n_channels > IEEE80211_NAN_MAX_CHANNELS)
		return -EOPNOTSUPP;

	if (sched->nan_avail_blob_len > IEEE80211_NAN_AVAIL_BLOB_MAX_LEN)
		return -EINVAL;

	/*
	 * If a deferred schedule update is pending completion, new updates are
	 * not allowed. Only allow to configure an empty schedule so NAN can be
	 * stopped in the middle of a deferred update. This is fine because
	 * empty schedule means the local NAN device will not be available for
	 * peers anymore so there is no need to update peers about a new
	 * schedule.
	 */
	if (WARN_ON(sched_cfg->deferred && sched->n_channels))
		return -EBUSY;

	bitmap_zero(sdata->u.nan.removed_channels, IEEE80211_NAN_MAX_CHANNELS);

	memcpy(backup_sched.schedule, sched_cfg->schedule,
	       sizeof(backup_sched.schedule));
	memcpy(backup_sched.channels, sched_cfg->channels,
	       sizeof(backup_sched.channels));
	memcpy(backup_sched.avail_blob, sched_cfg->avail_blob,
	       sizeof(backup_sched.avail_blob));
	backup_sched.avail_blob_len = sched_cfg->avail_blob_len;

	memcpy(sched_cfg->avail_blob, sched->nan_avail_blob,
	       sched->nan_avail_blob_len);
	sched_cfg->avail_blob_len = sched->nan_avail_blob_len;

	/*
	 * Remove channels that are no longer in the new schedule to free up
	 * resources before adding new channels. For deferred schedule, channels
	 * will be removed when the schedule is applied.
	 * Create a mapping from sched index to sched_cfg channel
	 */
	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		bool still_needed = false;

		if (!sched_cfg->channels[i].chanreq.oper.chan)
			continue;

		for (int j = 0; j < sched->n_channels; j++) {
			if (cfg80211_chandef_identical(&sched_cfg->channels[i].chanreq.oper,
						       &sched->nan_channels[j].chandef)) {
				sched_idx_to_chan[j] =
					&sched_cfg->channels[i];
				still_needed = true;
				break;
			}
		}

		if (!still_needed) {
			__set_bit(i, sdata->u.nan.removed_channels);
			if (!sched->deferred)
				ieee80211_nan_remove_channel(sdata,
							     &sched_cfg->channels[i]);
		}
	}

	for (int i = 0; i < sched->n_channels; i++) {
		struct ieee80211_nan_channel *chan = sched_idx_to_chan[i];

		if (chan) {
			ieee80211_nan_update_channel(sdata->local, chan,
						     &sched->nan_channels[i],
						     sched->deferred);
		} else {
			chan = ieee80211_nan_find_free_channel(sched_cfg);
			if (WARN_ON(!chan)) {
				ret = -EINVAL;
				goto err;
			}

			sched_idx_to_chan[i] = chan;
			ieee80211_nan_init_channel(chan,
						   &sched->nan_channels[i]);

			ret = ieee80211_nan_use_chanctx(sdata, chan, false);
			if (ret) {
				memset(chan, 0, sizeof(*chan));
				goto err;
			}
		}
	}

	for (int s = 0; s < ARRAY_SIZE(sched_cfg->schedule); s++) {
		if (sched->schedule[s] < ARRAY_SIZE(sched_idx_to_chan))
			sched_cfg->schedule[s] =
				sched_idx_to_chan[sched->schedule[s]];
		else
			sched_cfg->schedule[s] = NULL;
	}

	sched_cfg->deferred = sched->deferred;

	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);

	/*
	 * For deferred update, don't update NDI carriers yet as the new
	 * schedule is not yet applied so common slots don't change. The NDI
	 * carrier will be updated once the driver notifies the new schedule is
	 * applied.
	 */
	if (sched_cfg->deferred)
		return 0;

	bitmap_zero(sdata->u.nan.removed_channels, IEEE80211_NAN_MAX_CHANNELS);

	return 0;
err:
	/* Remove newly added channels */
	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		struct cfg80211_chan_def *chan_def =
			&sched_cfg->channels[i].chanreq.oper;

		if (!chan_def->chan)
			continue;

		if (!cfg80211_chandef_identical(&backup_sched.channels[i].chanreq.oper,
						chan_def))
			ieee80211_nan_remove_channel(sdata,
						     &sched_cfg->channels[i]);
	}

	/* Re-add all backed up channels */
	for (int i = 0; i < ARRAY_SIZE(backup_sched.channels); i++) {
		struct ieee80211_nan_channel *chan = &sched_cfg->channels[i];

		*chan = backup_sched.channels[i];

		/*
		 * For deferred update, no channels were removed and the channel
		 * context didn't change, so nothing else to do.
		 */
		if (!chan->chanctx_conf || sched->deferred)
			continue;

		if (test_bit(i, sdata->u.nan.removed_channels)) {
			/* Clear the stale chanctx pointer */
			chan->chanctx_conf = NULL;
			/*
			 * We removed the newly added channels so we don't lack
			 * resources. So the only reason that this would fail
			 * is a FW error which we ignore. Therefore, this
			 * should never fail.
			 */
			WARN_ON(ieee80211_nan_use_chanctx(sdata, chan, true));
		} else {
			struct ieee80211_chanctx_conf *conf = chan->chanctx_conf;

			/* FIXME: detect no-op? */
			/* Channel was not removed but may have been updated */
			ieee80211_recalc_smps_chanctx(sdata->local,
						     container_of(conf,
								  struct ieee80211_chanctx,
								  conf));
		}
	}

	memcpy(sched_cfg->schedule, backup_sched.schedule,
	       sizeof(backup_sched.schedule));
	memcpy(sched_cfg->avail_blob, backup_sched.avail_blob,
	       sizeof(backup_sched.avail_blob));
	sched_cfg->avail_blob_len = backup_sched.avail_blob_len;
	sched_cfg->deferred = false;
	bitmap_zero(sdata->u.nan.removed_channels, IEEE80211_NAN_MAX_CHANNELS);

	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);
	return ret;
}

void ieee80211_nan_sched_update_done(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_nan_sched_cfg *sched_cfg = &vif->cfg.nan_sched;
	unsigned int i;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (WARN_ON(!sched_cfg->deferred))
		return;

	/*
	 * Clear the deferred flag before removing channels. Removing channels
	 * will trigger another schedule update to the driver, and there is no
	 * need for this update to be deferred since removed channels are not
	 * part of the schedule anymore, so no need to notify peers about
	 * removing them.
	 */
	sched_cfg->deferred = false;

	for (i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		struct ieee80211_nan_channel *chan = &sched_cfg->channels[i];
		struct ieee80211_chanctx_conf *conf = chan->chanctx_conf;

		if (!chan->chanreq.oper.chan)
			continue;

		if (test_bit(i, sdata->u.nan.removed_channels))
			ieee80211_nan_remove_channel(sdata, chan);
		else if (conf)
			/*
			 * We might have called this already for some channels,
			 * but this knows to handle a no-op.
			 */
			ieee80211_recalc_smps_chanctx(sdata->local,
						      container_of(conf,
								   struct ieee80211_chanctx,
								   conf));
	}

	bitmap_zero(sdata->u.nan.removed_channels, IEEE80211_NAN_MAX_CHANNELS);
	cfg80211_nan_sched_update_done(ieee80211_vif_to_wdev(vif), true,
				       GFP_KERNEL);
}
EXPORT_SYMBOL(ieee80211_nan_sched_update_done);
