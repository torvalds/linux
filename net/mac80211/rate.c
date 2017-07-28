/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 * Copyright 2017	Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "rate.h"
#include "ieee80211_i.h"
#include "debugfs.h"

struct rate_control_alg {
	struct list_head list;
	const struct rate_control_ops *ops;
};

static LIST_HEAD(rate_ctrl_algs);
static DEFINE_MUTEX(rate_ctrl_mutex);

static char *ieee80211_default_rc_algo = CONFIG_MAC80211_RC_DEFAULT;
module_param(ieee80211_default_rc_algo, charp, 0644);
MODULE_PARM_DESC(ieee80211_default_rc_algo,
		 "Default rate control algorithm for mac80211 to use");

void rate_control_rate_init(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->sdata->local;
	struct rate_control_ref *ref = sta->rate_ctrl;
	struct ieee80211_sta *ista = &sta->sta;
	void *priv_sta = sta->rate_ctrl_priv;
	struct ieee80211_supported_band *sband;
	struct ieee80211_chanctx_conf *chanctx_conf;

	ieee80211_sta_set_rx_nss(sta);

	if (!ref)
		return;

	rcu_read_lock();

	chanctx_conf = rcu_dereference(sta->sdata->vif.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		return;
	}

	sband = local->hw.wiphy->bands[chanctx_conf->def.chan->band];

	spin_lock_bh(&sta->rate_ctrl_lock);
	ref->ops->rate_init(ref->priv, sband, &chanctx_conf->def, ista,
			    priv_sta);
	spin_unlock_bh(&sta->rate_ctrl_lock);
	rcu_read_unlock();
	set_sta_flag(sta, WLAN_STA_RATE_CONTROL);
}

void rate_control_tx_status(struct ieee80211_local *local,
			    struct ieee80211_supported_band *sband,
			    struct ieee80211_tx_status *st)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	struct sta_info *sta = container_of(st->sta, struct sta_info, sta);
	void *priv_sta = sta->rate_ctrl_priv;

	if (!ref || !test_sta_flag(sta, WLAN_STA_RATE_CONTROL))
		return;

	spin_lock_bh(&sta->rate_ctrl_lock);
	if (ref->ops->tx_status_ext)
		ref->ops->tx_status_ext(ref->priv, sband, priv_sta, st);
	else if (st->skb)
		ref->ops->tx_status(ref->priv, sband, st->sta, priv_sta, st->skb);
	else
		WARN_ON_ONCE(1);

	spin_unlock_bh(&sta->rate_ctrl_lock);
}

void rate_control_rate_update(struct ieee80211_local *local,
				    struct ieee80211_supported_band *sband,
				    struct sta_info *sta, u32 changed)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	struct ieee80211_sta *ista = &sta->sta;
	void *priv_sta = sta->rate_ctrl_priv;
	struct ieee80211_chanctx_conf *chanctx_conf;

	if (ref && ref->ops->rate_update) {
		rcu_read_lock();

		chanctx_conf = rcu_dereference(sta->sdata->vif.chanctx_conf);
		if (WARN_ON(!chanctx_conf)) {
			rcu_read_unlock();
			return;
		}

		spin_lock_bh(&sta->rate_ctrl_lock);
		ref->ops->rate_update(ref->priv, sband, &chanctx_conf->def,
				      ista, priv_sta, changed);
		spin_unlock_bh(&sta->rate_ctrl_lock);
		rcu_read_unlock();
	}
	drv_sta_rc_update(local, sta->sdata, &sta->sta, changed);
}

int ieee80211_rate_control_register(const struct rate_control_ops *ops)
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

void ieee80211_rate_control_unregister(const struct rate_control_ops *ops)
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

static const struct rate_control_ops *
ieee80211_try_rate_control_ops_get(const char *name)
{
	struct rate_control_alg *alg;
	const struct rate_control_ops *ops = NULL;

	if (!name)
		return NULL;

	mutex_lock(&rate_ctrl_mutex);
	list_for_each_entry(alg, &rate_ctrl_algs, list) {
		if (!strcmp(alg->ops->name, name)) {
			ops = alg->ops;
			break;
		}
	}
	mutex_unlock(&rate_ctrl_mutex);
	return ops;
}

/* Get the rate control algorithm. */
static const struct rate_control_ops *
ieee80211_rate_control_ops_get(const char *name)
{
	const struct rate_control_ops *ops;
	const char *alg_name;

	kernel_param_lock(THIS_MODULE);
	if (!name)
		alg_name = ieee80211_default_rc_algo;
	else
		alg_name = name;

	ops = ieee80211_try_rate_control_ops_get(alg_name);
	if (!ops && name)
		/* try default if specific alg requested but not found */
		ops = ieee80211_try_rate_control_ops_get(ieee80211_default_rc_algo);

	/* Note: check for > 0 is intentional to avoid clang warning */
	if (!ops && (strlen(CONFIG_MAC80211_RC_DEFAULT) > 0))
		/* try built-in one if specific alg requested but not found */
		ops = ieee80211_try_rate_control_ops_get(CONFIG_MAC80211_RC_DEFAULT);

	kernel_param_unlock(THIS_MODULE);

	return ops;
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
	.open = simple_open,
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
		return NULL;
	ref->ops = ieee80211_rate_control_ops_get(name);
	if (!ref->ops)
		goto free;

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfsdir = debugfs_create_dir("rc", local->hw.wiphy->debugfsdir);
	local->debugfs.rcdir = debugfsdir;
	debugfs_create_file("name", 0400, debugfsdir, ref, &rcname_ops);
#endif

	ref->priv = ref->ops->alloc(&local->hw, debugfsdir);
	if (!ref->priv)
		goto free;
	return ref;

free:
	kfree(ref);
	return NULL;
}

static void rate_control_free(struct ieee80211_local *local,
			      struct rate_control_ref *ctrl_ref)
{
	ctrl_ref->ops->free(ctrl_ref->priv);

#ifdef CONFIG_MAC80211_DEBUGFS
	debugfs_remove_recursive(local->debugfs.rcdir);
	local->debugfs.rcdir = NULL;
#endif

	kfree(ctrl_ref);
}

void ieee80211_check_rate_mask(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	u32 user_mask, basic_rates = sdata->vif.bss_conf.basic_rates;
	enum nl80211_band band;

	if (WARN_ON(!sdata->vif.bss_conf.chandef.chan))
		return;

	if (WARN_ON_ONCE(!basic_rates))
		return;

	band = sdata->vif.bss_conf.chandef.chan->band;
	user_mask = sdata->rc_rateidx_mask[band];
	sband = local->hw.wiphy->bands[band];

	if (user_mask & basic_rates)
		return;

	sdata_dbg(sdata,
		  "no overlap between basic rates (0x%x) and user mask (0x%x on band %d) - clearing the latter",
		  basic_rates, user_mask, band);
	sdata->rc_rateidx_mask[band] = (1 << sband->n_bitrates) - 1;
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

static void rc_send_low_basicrate(s8 *idx, u32 basic_rates,
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

static void __rate_control_send_low(struct ieee80211_hw *hw,
				    struct ieee80211_supported_band *sband,
				    struct ieee80211_sta *sta,
				    struct ieee80211_tx_info *info,
				    u32 rate_mask)
{
	int i;
	u32 rate_flags =
		ieee80211_chandef_rate_flags(&hw->conf.chandef);

	if ((sband->band == NL80211_BAND_2GHZ) &&
	    (info->flags & IEEE80211_TX_CTL_NO_CCK_RATE))
		rate_flags |= IEEE80211_RATE_ERP_G;

	info->control.rates[0].idx = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if (!(rate_mask & BIT(i)))
			continue;

		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;

		if (!rate_supported(sta, sband->band, i))
			continue;

		info->control.rates[0].idx = i;
		break;
	}
	WARN_ONCE(i == sband->n_bitrates,
		  "no supported rates (0x%x) in rate_mask 0x%x with flags 0x%x\n",
		  sta ? sta->supp_rates[sband->band] : -1,
		  rate_mask, rate_flags);

	info->control.rates[0].count =
		(info->flags & IEEE80211_TX_CTL_NO_ACK) ?
		1 : hw->max_rate_tries;

	info->control.skip_table = 1;
}


bool rate_control_send_low(struct ieee80211_sta *pubsta,
			   void *priv_sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	struct ieee80211_supported_band *sband = txrc->sband;
	struct sta_info *sta;
	int mcast_rate;
	bool use_basicrate = false;

	if (!pubsta || !priv_sta || rc_no_data_or_no_ack_use_min(txrc)) {
		__rate_control_send_low(txrc->hw, sband, pubsta, info,
					txrc->rate_idx_mask);

		if (!pubsta && txrc->bss) {
			mcast_rate = txrc->bss_conf->mcast_rate[sband->band];
			if (mcast_rate > 0) {
				info->control.rates[0].idx = mcast_rate - 1;
				return true;
			}
			use_basicrate = true;
		} else if (pubsta) {
			sta = container_of(pubsta, struct sta_info, sta);
			if (ieee80211_vif_is_mesh(&sta->sdata->vif))
				use_basicrate = true;
		}

		if (use_basicrate)
			rc_send_low_basicrate(&info->control.rates[0].idx,
					      txrc->bss_conf->basic_rates,
					      sband);

		return true;
	}
	return false;
}
EXPORT_SYMBOL(rate_control_send_low);

static bool rate_idx_match_legacy_mask(s8 *rate_idx, int n_bitrates, u32 mask)
{
	int j;

	/* See whether the selected rate or anything below it is allowed. */
	for (j = *rate_idx; j >= 0; j--) {
		if (mask & (1 << j)) {
			/* Okay, found a suitable rate. Use it. */
			*rate_idx = j;
			return true;
		}
	}

	/* Try to find a higher rate that would be allowed */
	for (j = *rate_idx + 1; j < n_bitrates; j++) {
		if (mask & (1 << j)) {
			/* Okay, found a suitable rate. Use it. */
			*rate_idx = j;
			return true;
		}
	}
	return false;
}

static bool rate_idx_match_mcs_mask(s8 *rate_idx, u8 *mcs_mask)
{
	int i, j;
	int ridx, rbit;

	ridx = *rate_idx / 8;
	rbit = *rate_idx % 8;

	/* sanity check */
	if (ridx < 0 || ridx >= IEEE80211_HT_MCS_MASK_LEN)
		return false;

	/* See whether the selected rate or anything below it is allowed. */
	for (i = ridx; i >= 0; i--) {
		for (j = rbit; j >= 0; j--)
			if (mcs_mask[i] & BIT(j)) {
				*rate_idx = i * 8 + j;
				return true;
			}
		rbit = 7;
	}

	/* Try to find a higher rate that would be allowed */
	ridx = (*rate_idx + 1) / 8;
	rbit = (*rate_idx + 1) % 8;

	for (i = ridx; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		for (j = rbit; j < 8; j++)
			if (mcs_mask[i] & BIT(j)) {
				*rate_idx = i * 8 + j;
				return true;
			}
		rbit = 0;
	}
	return false;
}

static bool rate_idx_match_vht_mcs_mask(s8 *rate_idx, u16 *vht_mask)
{
	int i, j;
	int ridx, rbit;

	ridx = *rate_idx >> 4;
	rbit = *rate_idx & 0xf;

	if (ridx < 0 || ridx >= NL80211_VHT_NSS_MAX)
		return false;

	/* See whether the selected rate or anything below it is allowed. */
	for (i = ridx; i >= 0; i--) {
		for (j = rbit; j >= 0; j--) {
			if (vht_mask[i] & BIT(j)) {
				*rate_idx = (i << 4) | j;
				return true;
			}
		}
		rbit = 15;
	}

	/* Try to find a higher rate that would be allowed */
	ridx = (*rate_idx + 1) >> 4;
	rbit = (*rate_idx + 1) & 0xf;

	for (i = ridx; i < NL80211_VHT_NSS_MAX; i++) {
		for (j = rbit; j < 16; j++) {
			if (vht_mask[i] & BIT(j)) {
				*rate_idx = (i << 4) | j;
				return true;
			}
		}
		rbit = 0;
	}
	return false;
}

static void rate_idx_match_mask(s8 *rate_idx, u16 *rate_flags,
				struct ieee80211_supported_band *sband,
				enum nl80211_chan_width chan_width,
				u32 mask,
				u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN],
				u16 vht_mask[NL80211_VHT_NSS_MAX])
{
	if (*rate_flags & IEEE80211_TX_RC_VHT_MCS) {
		/* handle VHT rates */
		if (rate_idx_match_vht_mcs_mask(rate_idx, vht_mask))
			return;

		*rate_idx = 0;
		/* keep protection flags */
		*rate_flags &= (IEEE80211_TX_RC_USE_RTS_CTS |
				IEEE80211_TX_RC_USE_CTS_PROTECT |
				IEEE80211_TX_RC_USE_SHORT_PREAMBLE);

		*rate_flags |= IEEE80211_TX_RC_MCS;
		if (chan_width == NL80211_CHAN_WIDTH_40)
			*rate_flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;

		if (rate_idx_match_mcs_mask(rate_idx, mcs_mask))
			return;

		/* also try the legacy rates. */
		*rate_flags &= ~(IEEE80211_TX_RC_MCS |
				 IEEE80211_TX_RC_40_MHZ_WIDTH);
		if (rate_idx_match_legacy_mask(rate_idx, sband->n_bitrates,
					       mask))
			return;
	} else if (*rate_flags & IEEE80211_TX_RC_MCS) {
		/* handle HT rates */
		if (rate_idx_match_mcs_mask(rate_idx, mcs_mask))
			return;

		/* also try the legacy rates. */
		*rate_idx = 0;
		/* keep protection flags */
		*rate_flags &= (IEEE80211_TX_RC_USE_RTS_CTS |
				IEEE80211_TX_RC_USE_CTS_PROTECT |
				IEEE80211_TX_RC_USE_SHORT_PREAMBLE);
		if (rate_idx_match_legacy_mask(rate_idx, sband->n_bitrates,
					       mask))
			return;
	} else {
		/* handle legacy rates */
		if (rate_idx_match_legacy_mask(rate_idx, sband->n_bitrates,
					       mask))
			return;

		/* if HT BSS, and we handle a data frame, also try HT rates */
		switch (chan_width) {
		case NL80211_CHAN_WIDTH_20_NOHT:
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
			return;
		default:
			break;
		}

		*rate_idx = 0;
		/* keep protection flags */
		*rate_flags &= (IEEE80211_TX_RC_USE_RTS_CTS |
				IEEE80211_TX_RC_USE_CTS_PROTECT |
				IEEE80211_TX_RC_USE_SHORT_PREAMBLE);

		*rate_flags |= IEEE80211_TX_RC_MCS;

		if (chan_width == NL80211_CHAN_WIDTH_40)
			*rate_flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;

		if (rate_idx_match_mcs_mask(rate_idx, mcs_mask))
			return;
	}

	/*
	 * Uh.. No suitable rate exists. This should not really happen with
	 * sane TX rate mask configurations. However, should someone manage to
	 * configure supported rates and TX rate mask in incompatible way,
	 * allow the frame to be transmitted with whatever the rate control
	 * selected.
	 */
}

static void rate_fixup_ratelist(struct ieee80211_vif *vif,
				struct ieee80211_supported_band *sband,
				struct ieee80211_tx_info *info,
				struct ieee80211_tx_rate *rates,
				int max_rates)
{
	struct ieee80211_rate *rate;
	bool inval = false;
	int i;

	/*
	 * Set up the RTS/CTS rate as the fastest basic rate
	 * that is not faster than the data rate unless there
	 * is no basic rate slower than the data rate, in which
	 * case we pick the slowest basic rate
	 *
	 * XXX: Should this check all retry rates?
	 */
	if (!(rates[0].flags &
	      (IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_VHT_MCS))) {
		u32 basic_rates = vif->bss_conf.basic_rates;
		s8 baserate = basic_rates ? ffs(basic_rates) - 1 : 0;

		rate = &sband->bitrates[rates[0].idx];

		for (i = 0; i < sband->n_bitrates; i++) {
			/* must be a basic rate */
			if (!(basic_rates & BIT(i)))
				continue;
			/* must not be faster than the data rate */
			if (sband->bitrates[i].bitrate > rate->bitrate)
				continue;
			/* maximum */
			if (sband->bitrates[baserate].bitrate <
			     sband->bitrates[i].bitrate)
				baserate = i;
		}

		info->control.rts_cts_rate_idx = baserate;
	}

	for (i = 0; i < max_rates; i++) {
		/*
		 * make sure there's no valid rate following
		 * an invalid one, just in case drivers don't
		 * take the API seriously to stop at -1.
		 */
		if (inval) {
			rates[i].idx = -1;
			continue;
		}
		if (rates[i].idx < 0) {
			inval = true;
			continue;
		}

		/*
		 * For now assume MCS is already set up correctly, this
		 * needs to be fixed.
		 */
		if (rates[i].flags & IEEE80211_TX_RC_MCS) {
			WARN_ON(rates[i].idx > 76);

			if (!(rates[i].flags & IEEE80211_TX_RC_USE_RTS_CTS) &&
			    info->control.use_cts_prot)
				rates[i].flags |=
					IEEE80211_TX_RC_USE_CTS_PROTECT;
			continue;
		}

		if (rates[i].flags & IEEE80211_TX_RC_VHT_MCS) {
			WARN_ON(ieee80211_rate_get_vht_mcs(&rates[i]) > 9);
			continue;
		}

		/* set up RTS protection if desired */
		if (info->control.use_rts) {
			rates[i].flags |= IEEE80211_TX_RC_USE_RTS_CTS;
			info->control.use_cts_prot = false;
		}

		/* RC is busted */
		if (WARN_ON_ONCE(rates[i].idx >= sband->n_bitrates)) {
			rates[i].idx = -1;
			continue;
		}

		rate = &sband->bitrates[rates[i].idx];

		/* set up short preamble */
		if (info->control.short_preamble &&
		    rate->flags & IEEE80211_RATE_SHORT_PREAMBLE)
			rates[i].flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;

		/* set up G protection */
		if (!(rates[i].flags & IEEE80211_TX_RC_USE_RTS_CTS) &&
		    info->control.use_cts_prot &&
		    rate->flags & IEEE80211_RATE_ERP_G)
			rates[i].flags |= IEEE80211_TX_RC_USE_CTS_PROTECT;
	}
}


static void rate_control_fill_sta_table(struct ieee80211_sta *sta,
					struct ieee80211_tx_info *info,
					struct ieee80211_tx_rate *rates,
					int max_rates)
{
	struct ieee80211_sta_rates *ratetbl = NULL;
	int i;

	if (sta && !info->control.skip_table)
		ratetbl = rcu_dereference(sta->rates);

	/* Fill remaining rate slots with data from the sta rate table. */
	max_rates = min_t(int, max_rates, IEEE80211_TX_RATE_TABLE_SIZE);
	for (i = 0; i < max_rates; i++) {
		if (i < ARRAY_SIZE(info->control.rates) &&
		    info->control.rates[i].idx >= 0 &&
		    info->control.rates[i].count) {
			if (rates != info->control.rates)
				rates[i] = info->control.rates[i];
		} else if (ratetbl) {
			rates[i].idx = ratetbl->rate[i].idx;
			rates[i].flags = ratetbl->rate[i].flags;
			if (info->control.use_rts)
				rates[i].count = ratetbl->rate[i].count_rts;
			else if (info->control.use_cts_prot)
				rates[i].count = ratetbl->rate[i].count_cts;
			else
				rates[i].count = ratetbl->rate[i].count;
		} else {
			rates[i].idx = -1;
			rates[i].count = 0;
		}

		if (rates[i].idx < 0 || !rates[i].count)
			break;
	}
}

static bool rate_control_cap_mask(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_supported_band *sband,
				  struct ieee80211_sta *sta, u32 *mask,
				  u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN],
				  u16 vht_mask[NL80211_VHT_NSS_MAX])
{
	u32 i, flags;

	*mask = sdata->rc_rateidx_mask[sband->band];
	flags = ieee80211_chandef_rate_flags(&sdata->vif.bss_conf.chandef);
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((flags & sband->bitrates[i].flags) != flags)
			*mask &= ~BIT(i);
	}

	if (*mask == (1 << sband->n_bitrates) - 1 &&
	    !sdata->rc_has_mcs_mask[sband->band] &&
	    !sdata->rc_has_vht_mcs_mask[sband->band])
		return false;

	if (sdata->rc_has_mcs_mask[sband->band])
		memcpy(mcs_mask, sdata->rc_rateidx_mcs_mask[sband->band],
		       IEEE80211_HT_MCS_MASK_LEN);
	else
		memset(mcs_mask, 0xff, IEEE80211_HT_MCS_MASK_LEN);

	if (sdata->rc_has_vht_mcs_mask[sband->band])
		memcpy(vht_mask, sdata->rc_rateidx_vht_mcs_mask[sband->band],
		       sizeof(u16) * NL80211_VHT_NSS_MAX);
	else
		memset(vht_mask, 0xff, sizeof(u16) * NL80211_VHT_NSS_MAX);

	if (sta) {
		__le16 sta_vht_cap;
		u16 sta_vht_mask[NL80211_VHT_NSS_MAX];

		/* Filter out rates that the STA does not support */
		*mask &= sta->supp_rates[sband->band];
		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
			mcs_mask[i] &= sta->ht_cap.mcs.rx_mask[i];

		sta_vht_cap = sta->vht_cap.vht_mcs.rx_mcs_map;
		ieee80211_get_vht_mask_from_cap(sta_vht_cap, sta_vht_mask);
		for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
			vht_mask[i] &= sta_vht_mask[i];
	}

	return true;
}

static void
rate_control_apply_mask_ratetbl(struct sta_info *sta,
				struct ieee80211_supported_band *sband,
				struct ieee80211_sta_rates *rates)
{
	int i;
	u32 mask;
	u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN];
	u16 vht_mask[NL80211_VHT_NSS_MAX];
	enum nl80211_chan_width chan_width;

	if (!rate_control_cap_mask(sta->sdata, sband, &sta->sta, &mask,
				   mcs_mask, vht_mask))
		return;

	chan_width = sta->sdata->vif.bss_conf.chandef.width;
	for (i = 0; i < IEEE80211_TX_RATE_TABLE_SIZE; i++) {
		if (rates->rate[i].idx < 0)
			break;

		rate_idx_match_mask(&rates->rate[i].idx, &rates->rate[i].flags,
				    sband, chan_width, mask, mcs_mask,
				    vht_mask);
	}
}

static void rate_control_apply_mask(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_sta *sta,
				    struct ieee80211_supported_band *sband,
				    struct ieee80211_tx_rate *rates,
				    int max_rates)
{
	enum nl80211_chan_width chan_width;
	u8 mcs_mask[IEEE80211_HT_MCS_MASK_LEN];
	u32 mask;
	u16 rate_flags, vht_mask[NL80211_VHT_NSS_MAX];
	int i;

	/*
	 * Try to enforce the rateidx mask the user wanted. skip this if the
	 * default mask (allow all rates) is used to save some processing for
	 * the common case.
	 */
	if (!rate_control_cap_mask(sdata, sband, sta, &mask, mcs_mask,
				   vht_mask))
		return;

	/*
	 * Make sure the rate index selected for each TX rate is
	 * included in the configured mask and change the rate indexes
	 * if needed.
	 */
	chan_width = sdata->vif.bss_conf.chandef.width;
	for (i = 0; i < max_rates; i++) {
		/* Skip invalid rates */
		if (rates[i].idx < 0)
			break;

		rate_flags = rates[i].flags;
		rate_idx_match_mask(&rates[i].idx, &rate_flags, sband,
				    chan_width, mask, mcs_mask, vht_mask);
		rates[i].flags = rate_flags;
	}
}

void ieee80211_get_tx_rates(struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    struct ieee80211_tx_rate *dest,
			    int max_rates)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_supported_band *sband;

	rate_control_fill_sta_table(sta, info, dest, max_rates);

	if (!vif)
		return;

	sdata = vif_to_sdata(vif);
	sband = sdata->local->hw.wiphy->bands[info->band];

	if (ieee80211_is_data(hdr->frame_control))
		rate_control_apply_mask(sdata, sta, sband, dest, max_rates);

	if (dest[0].idx < 0)
		__rate_control_send_low(&sdata->local->hw, sband, sta, info,
					sdata->rc_rateidx_mask[info->band]);

	if (sta)
		rate_fixup_ratelist(vif, sband, info, dest, max_rates);
}
EXPORT_SYMBOL(ieee80211_get_tx_rates);

void rate_control_get_rate(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta,
			   struct ieee80211_tx_rate_control *txrc)
{
	struct rate_control_ref *ref = sdata->local->rate_ctrl;
	void *priv_sta = NULL;
	struct ieee80211_sta *ista = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	int i;

	if (sta && test_sta_flag(sta, WLAN_STA_RATE_CONTROL)) {
		ista = &sta->sta;
		priv_sta = sta->rate_ctrl_priv;
	}

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		info->control.rates[i].idx = -1;
		info->control.rates[i].flags = 0;
		info->control.rates[i].count = 0;
	}

	if (ieee80211_hw_check(&sdata->local->hw, HAS_RATE_CONTROL))
		return;

	if (ista) {
		spin_lock_bh(&sta->rate_ctrl_lock);
		ref->ops->get_rate(ref->priv, ista, priv_sta, txrc);
		spin_unlock_bh(&sta->rate_ctrl_lock);
	} else {
		ref->ops->get_rate(ref->priv, NULL, NULL, txrc);
	}

	if (ieee80211_hw_check(&sdata->local->hw, SUPPORTS_RC_TABLE))
		return;

	ieee80211_get_tx_rates(&sdata->vif, ista, txrc->skb,
			       info->control.rates,
			       ARRAY_SIZE(info->control.rates));
}

int rate_control_set_rates(struct ieee80211_hw *hw,
			   struct ieee80211_sta *pubsta,
			   struct ieee80211_sta_rates *rates)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_sta_rates *old;
	struct ieee80211_supported_band *sband;

	sband = ieee80211_get_sband(sta->sdata);
	if (!sband)
		return -EINVAL;
	rate_control_apply_mask_ratetbl(sta, sband, rates);
	/*
	 * mac80211 guarantees that this function will not be called
	 * concurrently, so the following RCU access is safe, even without
	 * extra locking. This can not be checked easily, so we just set
	 * the condition to true.
	 */
	old = rcu_dereference_protected(pubsta->rates, true);
	rcu_assign_pointer(pubsta->rates, rates);
	if (old)
		kfree_rcu(old, rcu_head);

	drv_sta_rate_tbl_update(hw_to_local(hw), sta->sdata, pubsta);

	ieee80211_sta_set_expected_throughput(pubsta, sta_get_expected_throughput(sta));

	return 0;
}
EXPORT_SYMBOL(rate_control_set_rates);

int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name)
{
	struct rate_control_ref *ref;

	ASSERT_RTNL();

	if (local->open_count)
		return -EBUSY;

	if (ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL)) {
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
	rate_control_free(local, ref);
}
