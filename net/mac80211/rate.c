/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "rate.h"
#include "ieee80211_i.h"
#include "debugfs.h"

struct rate_control_alg {
	struct list_head list;
	struct rate_control_ops *ops;
};

static LIST_HEAD(rate_ctrl_algs);
static DEFINE_MUTEX(rate_ctrl_mutex);

static char *ieee80211_default_rc_algo = CONFIG_MAC80211_RC_DEFAULT;
module_param(ieee80211_default_rc_algo, charp, 0644);
MODULE_PARM_DESC(ieee80211_default_rc_algo,
		 "Default rate control algorithm for mac80211 to use");

int ieee80211_rate_control_register(struct rate_control_ops *ops)
{
	struct rate_control_alg *alg;

	if (!ops->name)
		return -EINVAL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!strcmp(alg->ops->name, ops->name)) {
			/* don't register an algorithm twice */
			WARN_ON(1);
			mutex_unlock(&rate_ctrl_mutex);
			return -EALREADY;
		}
	}

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL) {
		mutex_unlock(&rate_ctrl_mutex);
		return -ENOMEM;
	}
	alg->ops = ops;

	list_add_tail(&alg->list, &rate_ctrl_algs);
	mutex_unlock(&rate_ctrl_mutex);

	return 0;
}
EXPORT_SYMBOL(ieee80211_rate_control_register);

void ieee80211_rate_control_unregister(struct rate_control_ops *ops)
{
	struct rate_control_alg *alg;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (alg->ops == ops) {
			list_del(&alg->list);
			kfree(alg);
			break;
		}
	}
	mutex_unlock(&rate_ctrl_mutex);
}
EXPORT_SYMBOL(ieee80211_rate_control_unregister);

static struct rate_control_ops *
ieee80211_try_rate_control_ops_get(const char *name)
{
	struct rate_control_alg *alg;
	struct rate_control_ops *ops = NULL;

	if (!name)
		return NULL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!strcmp(alg->ops->name, name))
			if (try_module_get(alg->ops->module)) {
				ops = alg->ops;
				break;
			}
	}
	mutex_unlock(&rate_ctrl_mutex);
	return ops;
}

/* Get the rate control algorithm. */
static struct rate_control_ops *
ieee80211_rate_control_ops_get(const char *name)
{
	struct rate_control_ops *ops;
	const char *alg_name;

	kparam_block_sysfs_write(ieee80211_default_rc_algo);
	if (!name)
		alg_name = ieee80211_default_rc_algo;
	else
		alg_name = name;

	ops = ieee80211_try_rate_control_ops_get(alg_name);
	if (!ops) {
		request_module("rc80211_%s", alg_name);
		ops = ieee80211_try_rate_control_ops_get(alg_name);
	}
	if (!ops && name)
		/* try default if specific alg requested but not found */
		ops = ieee80211_try_rate_control_ops_get(ieee80211_default_rc_algo);

	/* try built-in one if specific alg requested but not found */
	if (!ops && strlen(CONFIG_MAC80211_RC_DEFAULT))
		ops = ieee80211_try_rate_control_ops_get(CONFIG_MAC80211_RC_DEFAULT);
	kparam_unblock_sysfs_write(ieee80211_default_rc_algo);

	return ops;
}

static void ieee80211_rate_control_ops_put(struct rate_control_ops *ops)
{
	module_put(ops->module);
}

#ifdef CONFIG_MAC80211_DEBUGFS
static ssize_t rcname_read(struct file *file, char __user *userbuf,
			   size_t count, loff_t *ppos)
{
	struct rate_control_ref *ref = file->private_data;
	int len = strlen(ref->ops->name);

	return simple_read_from_buffer(userbuf, count, ppos,
				       ref->ops->name, len);
}

static const struct file_operations rcname_ops = {
	.read = rcname_read,
	.open = mac80211_open_file_generic,
	.llseek = default_llseek,
};
#endif

static struct rate_control_ref *rate_control_alloc(const char *name,
					    struct ieee80211_local *local)
{
	struct dentry *debugfsdir = NULL;
	struct rate_control_ref *ref;

	ref = kmalloc(sizeof(struct rate_control_ref), GFP_KERNEL);
	if (!ref)
		goto fail_ref;
	ref->local = local;
	ref->ops = ieee80211_rate_control_ops_get(name);
	if (!ref->ops)
		goto fail_ops;

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfsdir = debugfs_create_dir("rc", local->hw.wiphy->debugfsdir);
	local->debugfs.rcdir = debugfsdir;
	debugfs_create_file("name", 0400, debugfsdir, ref, &rcname_ops);
#endif

	ref->priv = ref->ops->alloc(&local->hw, debugfsdir);
	if (!ref->priv)
		goto fail_priv;
	return ref;

fail_priv:
	ieee80211_rate_control_ops_put(ref->ops);
fail_ops:
	kfree(ref);
fail_ref:
	return NULL;
}

static void rate_control_free(struct rate_control_ref *ctrl_ref)
{
	ctrl_ref->ops->free(ctrl_ref->priv);

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfs_remove_recursive(ctrl_ref->local->debugfs.rcdir);
	ctrl_ref->local->debugfs.rcdir = NULL;
#endif

	ieee80211_rate_control_ops_put(ctrl_ref->ops);
	kfree(ctrl_ref);
}

static bool rc_no_data_or_no_ack_use_min(struct ieee80211_tx_rate_control *txrc)
{
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	__le16 fc;

	fc = hdr->frame_control;

	return (info->flags & (IEEE80211_TX_CTL_NO_ACK |
			       IEEE80211_TX_CTL_USE_MINRATE)) ||
		!ieee80211_is_data(fc);
}

static void rc_send_low_broadcast(s8 *idx, u32 basic_rates,
				  struct ieee80211_supported_band *sband)
{
	u8 i;

	if (basic_rates == 0)
		return; /* assume basic rates unknown and accept rate */
	if (*idx < 0)
		return;
	if (basic_rates & (1 << *idx))
		return; /* selected rate is a basic rate */

	for (i = *idx + 1; i <= sband->n_bitrates; i++) {
		if (basic_rates & (1 << i)) {
			*idx = i;
			return;
		}
	}

	/* could not find a basic rate; use original selection */
}

static inline s8
rate_lowest_non_cck_index(struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta)
{
	int i;

	for (i = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *srate = &sband->bitrates[i];
		if ((srate->bitrate == 10) || (srate->bitrate == 20) ||
		    (srate->bitrate == 55) || (srate->bitrate == 110))
			continue;

		if (rate_supported(sta, sband->band, i))
			return i;
	}

	/* No matching rate found */
	return 0;
}


bool rate_control_send_low(struct ieee80211_sta *sta,
			   void *priv_sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	struct ieee80211_supported_band *sband = txrc->sband;
	int mcast_rate;

	if (!sta || !priv_sta || rc_no_data_or_no_ack_use_min(txrc)) {
		if ((sband->band != IEEE80211_BAND_2GHZ) ||
		    !(info->flags & IEEE80211_TX_CTL_NO_CCK_RATE))
			info->control.rates[0].idx =
				rate_lowest_index(txrc->sband, sta);
		else
			info->control.rates[0].idx =
				rate_lowest_non_cck_index(txrc->sband, sta);
		info->control.rates[0].count =
			(info->flags & IEEE80211_TX_CTL_NO_ACK) ?
			1 : txrc->hw->max_rate_tries;
		if (!sta && txrc->bss) {
			mcast_rate = txrc->bss_conf->mcast_rate[sband->band];
			if (mcast_rate > 0) {
				info->control.rates[0].idx = mcast_rate - 1;
				return true;
			}

			rc_send_low_broadcast(&info->control.rates[0].idx,
					      txrc->bss_conf->basic_rates,
					      sband);
		}
		return true;
	}
	return false;
}
EXPORT_SYMBOL(rate_control_send_low);

static bool rate_idx_match_legacy_mask(struct ieee80211_tx_rate *rate,
				       int n_bitrates, u32 mask)
{
	int j;

	/* See whether the selected rate or anything below it is allowed. */
	for (j = rate->idx; j >= 0; j--) {
		if (mask & (1 << j)) {
			/* Okay, found a suitable rate. Use it. */
			rate->idx = j;
			return true;
		}
	}

	/* Try to find a higher rate that would be allowed */
	for (j = rate->idx + 1; j < n_bitrates; j++) {
		if (mask & (1 << j)) {
			/* Okay, found a suitable rate. Use it. */
			rate->idx = j;
			return true;
		}
	}
	return false;
}

static bool rate_idx_match_mcs_mask(struct ieee80211_tx_rate *rate,
				    u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN])
{
	int i, j;
	int ridx, rbit;

	ridx = rate->idx / 8;
	rbit = rate->idx % 8;

	/* sanity check */
	if (ridx < 0 || ridx >= IEEE80211_HT_MCS_MASK_LEN)
		return false;

	/* See whether the selected rate or anything below it is allowed. */
	for (i = ridx; i >= 0; i--) {
		for (j = rbit; j >= 0; j--)
			if (mcs_mask[i] & BIT(j)) {
				rate->idx = i * 8 + j;
				return true;
			}
		rbit = 7;
	}

	/* Try to find a higher rate that would be allowed */
	ridx = (rate->idx + 1) / 8;
	rbit = (rate->idx + 1) % 8;

	for (i = ridx; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		for (j = rbit; j < 8; j++)
			if (mcs_mask[i] & BIT(j)) {
				rate->idx = i * 8 + j;
				return true;
			}
		rbit = 0;
	}
	return false;
}



static void rate_idx_match_mask(struct ieee80211_tx_rate *rate,
				struct ieee80211_tx_rate_control *txrc,
				u32 mask,
				u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN])
{
	struct ieee80211_tx_rate alt_rate;

	/* handle HT rates */
	if (rate->flags & IEEE80211_TX_RC_MCS) {
		if (rate_idx_match_mcs_mask(rate, mcs_mask))
			return;

		/* also try the legacy rates. */
		alt_rate.idx = 0;
		/* keep protection flags */
		alt_rate.flags = rate->flags &
				 (IEEE80211_TX_RC_USE_RTS_CTS |
				  IEEE80211_TX_RC_USE_CTS_PROTECT |
				  IEEE80211_TX_RC_USE_SHORT_PREAMBLE);
		alt_rate.count = rate->count;
		if (rate_idx_match_legacy_mask(&alt_rate,
					       txrc->sband->n_bitrates,
					       mask)) {
			*rate = alt_rate;
			return;
		}
	} else {
		struct sk_buff *skb = txrc->skb;
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		__le16 fc;

		/* handle legacy rates */
		if (rate_idx_match_legacy_mask(rate, txrc->sband->n_bitrates,
					       mask))
			return;

		/* if HT BSS, and we handle a data frame, also try HT rates */
		if (txrc->bss_conf->channel_type == NL80211_CHAN_NO_HT)
			return;

		fc = hdr->frame_control;
		if (!ieee80211_is_data(fc))
			return;

		alt_rate.idx = 0;
		/* keep protection flags */
		alt_rate.flags = rate->flags &
				 (IEEE80211_TX_RC_USE_RTS_CTS |
				  IEEE80211_TX_RC_USE_CTS_PROTECT |
				  IEEE80211_TX_RC_USE_SHORT_PREAMBLE);
		alt_rate.count = rate->count;

		alt_rate.flags |= IEEE80211_TX_RC_MCS;

		if ((txrc->bss_conf->channel_type == NL80211_CHAN_HT40MINUS) ||
		    (txrc->bss_conf->channel_type == NL80211_CHAN_HT40PLUS))
			alt_rate.flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;

		if (rate_idx_match_mcs_mask(&alt_rate, mcs_mask)) {
			*rate = alt_rate;
			return;
		}
	}

	/*
	 * Uh.. No suitable rate exists. This should not really happen with
	 * sane TX rate mask configurations. However, should someone manage to
	 * configure supported rates and TX rate mask in incompatible way,
	 * allow the frame to be transmitted with whatever the rate control
	 * selected.
	 */
}

void rate_control_get_rate(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct rate_control_ref *ref = sdata->local->rate_ctrl;
	void *priv_sta = NULL;
	struct ieee80211_sta *ista = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	int i;
	u32 mask;
	u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN];

	if (sta) {
		ista = &sta->sta;
		priv_sta = sta->rate_ctrl_priv;
	}

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		info->control.rates[i].idx = -1;
		info->control.rates[i].flags = 0;
		info->control.rates[i].count = 1;
	}

	if (sdata->local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL)
		return;

	ref->ops->get_rate(ref->priv, ista, priv_sta, txrc);

	/*
	 * Try to enforce the rateidx mask the user wanted. skip this if the
	 * default mask (allow all rates) is used to save some processing for
	 * the common case.
	 */
	mask = sdata->rc_rateidx_mask[info->band];
	memcpy(mcs_mask, sdata->rc_rateidx_mcs_mask[info->band],
	       sizeof(mcs_mask));
	if (mask != (1 << txrc->sband->n_bitrates) - 1) {
		if (sta) {
			/* Filter out rates that the STA does not support */
			mask &= sta->sta.supp_rates[info->band];
			for (i = 0; i < sizeof(mcs_mask); i++)
				mcs_mask[i] &= sta->sta.ht_cap.mcs.rx_mask[i];
		}
		/*
		 * Make sure the rate index selected for each TX rate is
		 * included in the configured mask and change the rate indexes
		 * if needed.
		 */
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			/* Skip invalid rates */
			if (info->control.rates[i].idx < 0)
				break;
			rate_idx_match_mask(&info->control.rates[i], txrc,
					    mask, mcs_mask);
		}
	}

	BUG_ON(info->control.rates[0].idx < 0);
}

int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name)
{
	struct rate_control_ref *ref;

	ASSERT_RTNL();

	if (local->open_count)
		return -EBUSY;

	if (local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL) {
		if (WARN_ON(!local->ops->set_rts_threshold))
			return -EINVAL;
		return 0;
	}

	ref = rate_control_alloc(name, local);
	if (!ref) {
		wiphy_warn(local->hw.wiphy,
			   "Failed to select rate control algorithm\n");
		return -ENOENT;
	}

	WARN_ON(local->rate_ctrl);
	local->rate_ctrl = ref;

	wiphy_debug(local->hw.wiphy, "Selected rate control algorithm '%s'\n",
		    ref->ops->name);

	return 0;
}

void rate_control_deinitialize(struct ieee80211_local *local)
{
	struct rate_control_ref *ref;

	ref = local->rate_ctrl;

	if (!ref)
		return;

	local->rate_ctrl = NULL;
	rate_control_free(ref);
}

