/*
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on minstrel.c:
 *   Copyright (C) 2005-2007 Derek Smithies <derek@indranet.co.nz>
 *   Sponsored by Indranet Technologies Ltd
 *
 * Based on sample.c:
 *   Copyright (c) 2005 John Bicket
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer,
 *      without modification.
 *   2. Redistributions in binary form must reproduce at minimum a disclaimer
 *      similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *      redistribution must be conditioned upon including a substantially
 *      similar Disclaimer requirement for further binary redistribution.
 *   3. Neither the names of the above-listed copyright holders nor the names
 *      of any contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *   Alternatively, this software may be distributed under the terms of the
 *   GNU General Public License ("GPL") version 2 as published by the Free
 *   Software Foundation.
 *
 *   NO WARRANTY
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 *   AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 *   THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 *   OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *   IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *   THE POSSIBILITY OF SUCH DAMAGES.
 */
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "rate.h"
#include "rc80211_minstrel.h"

#define SAMPLE_COLUMNS	10
#define SAMPLE_TBL(_mi, _idx, _col) \
		_mi->sample_table[(_idx * SAMPLE_COLUMNS) + _col]

/* convert mac80211 rate index to local array index */
static inline int
rix_to_ndx(struct minstrel_sta_info *mi, int rix)
{
	int i = rix;
	for (i = rix; i >= 0; i--)
		if (mi->r[i].rix == rix)
			break;
	WARN_ON(mi->r[i].rix != rix);
	return i;
}

static inline bool
use_low_rate(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u16 fc;

	fc = le16_to_cpu(hdr->frame_control);

	return ((info->flags & IEEE80211_TX_CTL_NO_ACK) ||
		(fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA ||
		is_multicast_ether_addr(hdr->addr1));
}


static void
minstrel_update_stats(struct minstrel_priv *mp, struct minstrel_sta_info *mi)
{
	u32 max_tp = 0, index_max_tp = 0, index_max_tp2 = 0;
	u32 max_prob = 0, index_max_prob = 0;
	u32 usecs;
	u32 p;
	int i;

	mi->stats_update = jiffies;
	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];

		usecs = mr->perfect_tx_time;
		if (!usecs)
			usecs = 1000000;

		/* To avoid rounding issues, probabilities scale from 0 (0%)
		 * to 18000 (100%) */
		if (mr->attempts) {
			p = (mr->success * 18000) / mr->attempts;
			mr->succ_hist += mr->success;
			mr->att_hist += mr->attempts;
			mr->cur_prob = p;
			p = ((p * (100 - mp->ewma_level)) + (mr->probability *
				mp->ewma_level)) / 100;
			mr->probability = p;
			mr->cur_tp = p * (1000000 / usecs);
		}

		mr->last_success = mr->success;
		mr->last_attempts = mr->attempts;
		mr->success = 0;
		mr->attempts = 0;

		/* Sample less often below the 10% chance of success.
		 * Sample less often above the 95% chance of success. */
		if ((mr->probability > 17100) || (mr->probability < 1800)) {
			mr->adjusted_retry_count = mr->retry_count >> 1;
			if (mr->adjusted_retry_count > 2)
				mr->adjusted_retry_count = 2;
		} else {
			mr->adjusted_retry_count = mr->retry_count;
		}
		if (!mr->adjusted_retry_count)
			mr->adjusted_retry_count = 2;
	}

	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		if (max_tp < mr->cur_tp) {
			index_max_tp = i;
			max_tp = mr->cur_tp;
		}
		if (max_prob < mr->probability) {
			index_max_prob = i;
			max_prob = mr->probability;
		}
	}

	max_tp = 0;
	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];

		if (i == index_max_tp)
			continue;

		if (max_tp < mr->cur_tp) {
			index_max_tp2 = i;
			max_tp = mr->cur_tp;
		}
	}
	mi->max_tp_rate = index_max_tp;
	mi->max_tp_rate2 = index_max_tp2;
	mi->max_prob_rate = index_max_prob;
}

static void
minstrel_tx_status(void *priv, struct ieee80211_supported_band *sband,
                   struct ieee80211_sta *sta, void *priv_sta,
		   struct sk_buff *skb)
{
	struct minstrel_sta_info *mi = priv_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_altrate *ar = info->status.retries;
	struct minstrel_priv *mp = priv;
	int i, ndx, tries;
	int success = 0;

	if (!info->status.excessive_retries)
		success = 1;

	if (!mp->has_mrr || (ar[0].rate_idx < 0)) {
		ndx = rix_to_ndx(mi, info->tx_rate_idx);
		tries = info->status.retry_count + 1;
		mi->r[ndx].success += success;
		mi->r[ndx].attempts += tries;
		return;
	}

	for (i = 0; i < 4; i++) {
		if (ar[i].rate_idx < 0)
			break;

		ndx = rix_to_ndx(mi, ar[i].rate_idx);
		mi->r[ndx].attempts += ar[i].limit + 1;

		if ((i != 3) && (ar[i + 1].rate_idx < 0))
			mi->r[ndx].success += success;
	}

	if ((info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) && (i >= 0))
		mi->sample_count++;

	if (mi->sample_deferred > 0)
		mi->sample_deferred--;
}


static inline unsigned int
minstrel_get_retry_count(struct minstrel_rate *mr,
                         struct ieee80211_tx_info *info)
{
	unsigned int retry = mr->adjusted_retry_count;

	if (info->flags & IEEE80211_TX_CTL_USE_RTS_CTS)
		retry = max(2U, min(mr->retry_count_rtscts, retry));
	else if (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT)
		retry = max(2U, min(mr->retry_count_cts, retry));
	return retry;
}


static int
minstrel_get_next_sample(struct minstrel_sta_info *mi)
{
	unsigned int sample_ndx;
	sample_ndx = SAMPLE_TBL(mi, mi->sample_idx, mi->sample_column);
	mi->sample_idx++;
	if (mi->sample_idx > (mi->n_rates - 2)) {
		mi->sample_idx = 0;
		mi->sample_column++;
		if (mi->sample_column >= SAMPLE_COLUMNS)
			mi->sample_column = 0;
	}
	return sample_ndx;
}

void
minstrel_get_rate(void *priv, struct ieee80211_supported_band *sband,
                  struct ieee80211_sta *sta, void *priv_sta,
                  struct sk_buff *skb, struct rate_selection *sel)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct minstrel_sta_info *mi = priv_sta;
	struct minstrel_priv *mp = priv;
	struct ieee80211_tx_altrate *ar = info->control.retries;
	unsigned int ndx, sample_ndx = 0;
	bool mrr;
	bool sample_slower = false;
	bool sample = false;
	int i, delta;
	int mrr_ndx[3];
	int sample_rate;

	if (!sta || !mi || use_low_rate(skb)) {
		sel->rate_idx = rate_lowest_index(sband, sta);
		return;
	}

	mrr = mp->has_mrr;

	/* mac80211 does not allow mrr for RTS/CTS */
	if ((info->flags & IEEE80211_TX_CTL_USE_RTS_CTS) ||
	    (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT))
		mrr = false;

	if (time_after(jiffies, mi->stats_update + (mp->update_interval *
			HZ) / 1000))
		minstrel_update_stats(mp, mi);

	ndx = mi->max_tp_rate;

	if (mrr)
		sample_rate = mp->lookaround_rate_mrr;
	else
		sample_rate = mp->lookaround_rate;

	mi->packet_count++;
	delta = (mi->packet_count * sample_rate / 100) -
			(mi->sample_count + mi->sample_deferred / 2);

	/* delta > 0: sampling required */
	if (delta > 0) {
		if (mi->packet_count >= 10000) {
			mi->sample_deferred = 0;
			mi->sample_count = 0;
			mi->packet_count = 0;
		} else if (delta > mi->n_rates * 2) {
			/* With multi-rate retry, not every planned sample
			 * attempt actually gets used, due to the way the retry
			 * chain is set up - [max_tp,sample,prob,lowest] for
			 * sample_rate < max_tp.
			 *
			 * If there's too much sampling backlog and the link
			 * starts getting worse, minstrel would start bursting
			 * out lots of sampling frames, which would result
			 * in a large throughput loss. */
			mi->sample_count += (delta - mi->n_rates * 2);
		}

		sample_ndx = minstrel_get_next_sample(mi);
		sample = true;
		sample_slower = mrr && (mi->r[sample_ndx].perfect_tx_time >
			mi->r[ndx].perfect_tx_time);

		if (!sample_slower) {
			ndx = sample_ndx;
			mi->sample_count++;
		} else {
			/* Only use IEEE80211_TX_CTL_RATE_CTRL_PROBE to mark
			 * packets that have the sampling rate deferred to the
			 * second MRR stage. Increase the sample counter only
			 * if the deferred sample rate was actually used.
			 * Use the sample_deferred counter to make sure that
			 * the sampling is not done in large bursts */
			info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
			mi->sample_deferred++;
		}
	}
	sel->rate_idx = mi->r[ndx].rix;
	info->control.retry_limit = minstrel_get_retry_count(&mi->r[ndx], info);

	if (!mrr) {
		ar[0].rate_idx = mi->lowest_rix;
		ar[0].limit = mp->max_retry;
		ar[1].rate_idx = -1;
		return;
	}

	/* MRR setup */
	if (sample) {
		if (sample_slower)
			mrr_ndx[0] = sample_ndx;
		else
			mrr_ndx[0] = mi->max_tp_rate;
	} else {
		mrr_ndx[0] = mi->max_tp_rate2;
	}
	mrr_ndx[1] = mi->max_prob_rate;
	mrr_ndx[2] = 0;
	for (i = 0; i < 3; i++) {
		ar[i].rate_idx = mi->r[mrr_ndx[i]].rix;
		ar[i].limit = mi->r[mrr_ndx[i]].adjusted_retry_count;
	}
}


static void
calc_rate_durations(struct minstrel_sta_info *mi, struct ieee80211_local *local,
                    struct minstrel_rate *d, struct ieee80211_rate *rate)
{
	int erp = !!(rate->flags & IEEE80211_RATE_ERP_G);

	d->perfect_tx_time = ieee80211_frame_duration(local, 1200,
			rate->bitrate, erp, 1);
	d->ack_time = ieee80211_frame_duration(local, 10,
			rate->bitrate, erp, 1);
}

static void
init_sample_table(struct minstrel_sta_info *mi)
{
	unsigned int i, col, new_idx;
	unsigned int n_srates = mi->n_rates - 1;
	u8 rnd[8];

	mi->sample_column = 0;
	mi->sample_idx = 0;
	memset(mi->sample_table, 0, SAMPLE_COLUMNS * mi->n_rates);

	for (col = 0; col < SAMPLE_COLUMNS; col++) {
		for (i = 0; i < n_srates; i++) {
			get_random_bytes(rnd, sizeof(rnd));
			new_idx = (i + rnd[i & 7]) % n_srates;

			while (SAMPLE_TBL(mi, new_idx, col) != 0)
				new_idx = (new_idx + 1) % n_srates;

			/* Don't sample the slowest rate (i.e. slowest base
			 * rate). We must presume that the slowest rate works
			 * fine, or else other management frames will also be
			 * failing and the link will break */
			SAMPLE_TBL(mi, new_idx, col) = i + 1;
		}
	}
}

static void
minstrel_rate_init(void *priv, struct ieee80211_supported_band *sband,
               struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_sta_info *mi = priv_sta;
	struct minstrel_priv *mp = priv;
	struct minstrel_rate *mr_ctl;
	unsigned int i, n = 0;
	unsigned int t_slot = 9; /* FIXME: get real slot time */

	mi->lowest_rix = rate_lowest_index(sband, sta);
	mr_ctl = &mi->r[rix_to_ndx(mi, mi->lowest_rix)];
	mi->sp_ack_dur = mr_ctl->ack_time;

	for (i = 0; i < sband->n_bitrates; i++) {
		struct minstrel_rate *mr = &mi->r[n];
		unsigned int tx_time = 0, tx_time_cts = 0, tx_time_rtscts = 0;
		unsigned int tx_time_single;
		unsigned int cw = mp->cw_min;

		if (!rate_supported(sta, sband->band, i))
			continue;
		n++;
		memset(mr, 0, sizeof(*mr));

		mr->rix = i;
		mr->bitrate = sband->bitrates[i].bitrate / 5;
		calc_rate_durations(mi, hw_to_local(mp->hw), mr,
				&sband->bitrates[i]);

		/* calculate maximum number of retransmissions before
		 * fallback (based on maximum segment size) */
		mr->retry_count = 1;
		mr->retry_count_cts = 1;
		mr->retry_count_rtscts = 1;
		tx_time = mr->perfect_tx_time + mi->sp_ack_dur;
		do {
			/* add one retransmission */
			tx_time_single = mr->ack_time + mr->perfect_tx_time;

			/* contention window */
			tx_time_single += t_slot + min(cw, mp->cw_max);
			cw = (cw + 1) << 1;

			tx_time += tx_time_single;
			tx_time_cts += tx_time_single + mi->sp_ack_dur;
			tx_time_rtscts += tx_time_single + 2 * mi->sp_ack_dur;
			if ((tx_time_cts < mp->segment_size) &&
				(mr->retry_count_cts < mp->max_retry))
				mr->retry_count_cts++;
			if ((tx_time_rtscts < mp->segment_size) &&
				(mr->retry_count_rtscts < mp->max_retry))
				mr->retry_count_rtscts++;
		} while ((tx_time < mp->segment_size) &&
				(++mr->retry_count < mp->max_retry));
		mr->adjusted_retry_count = mr->retry_count;
	}

	for (i = n; i < sband->n_bitrates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		mr->rix = -1;
	}

	mi->n_rates = n;
	mi->stats_update = jiffies;

	init_sample_table(mi);
}

static void *
minstrel_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct ieee80211_supported_band *sband;
	struct minstrel_sta_info *mi;
	struct minstrel_priv *mp = priv;
	struct ieee80211_hw *hw = mp->hw;
	int max_rates = 0;
	int i;

	mi = kzalloc(sizeof(struct minstrel_sta_info), gfp);
	if (!mi)
		return NULL;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = hw->wiphy->bands[hw->conf.channel->band];
		if (sband->n_bitrates > max_rates)
			max_rates = sband->n_bitrates;
	}

	mi->r = kzalloc(sizeof(struct minstrel_rate) * max_rates, gfp);
	if (!mi->r)
		goto error;

	mi->sample_table = kmalloc(SAMPLE_COLUMNS * max_rates, gfp);
	if (!mi->sample_table)
		goto error1;

	mi->stats_update = jiffies;
	return mi;

error1:
	kfree(mi->r);
error:
	kfree(mi);
	return NULL;
}

static void
minstrel_free_sta(void *priv, struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_sta_info *mi = priv_sta;

	kfree(mi->sample_table);
	kfree(mi->r);
	kfree(mi);
}

static void
minstrel_clear(void *priv)
{
}

static void *
minstrel_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	struct minstrel_priv *mp;

	mp = kzalloc(sizeof(struct minstrel_priv), GFP_ATOMIC);
	if (!mp)
		return NULL;

	/* contention window settings
	 * Just an approximation. Using the per-queue values would complicate
	 * the calculations and is probably unnecessary */
	mp->cw_min = 15;
	mp->cw_max = 1023;

	/* number of packets (in %) to use for sampling other rates
	 * sample less often for non-mrr packets, because the overhead
	 * is much higher than with mrr */
	mp->lookaround_rate = 5;
	mp->lookaround_rate_mrr = 10;

	/* moving average weight for EWMA */
	mp->ewma_level = 75;

	/* maximum time that the hw is allowed to stay in one MRR segment */
	mp->segment_size = 6000;

	if (hw->max_altrate_tries > 0)
		mp->max_retry = hw->max_altrate_tries;
	else
		/* safe default, does not necessarily have to match hw properties */
		mp->max_retry = 7;

	if (hw->max_altrates >= 3)
		mp->has_mrr = true;

	mp->hw = hw;
	mp->update_interval = 100;

	return mp;
}

static void
minstrel_free(void *priv)
{
	kfree(priv);
}

static struct rate_control_ops mac80211_minstrel = {
	.name = "minstrel",
	.tx_status = minstrel_tx_status,
	.get_rate = minstrel_get_rate,
	.rate_init = minstrel_rate_init,
	.clear = minstrel_clear,
	.alloc = minstrel_alloc,
	.free = minstrel_free,
	.alloc_sta = minstrel_alloc_sta,
	.free_sta = minstrel_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = minstrel_add_sta_debugfs,
	.remove_sta_debugfs = minstrel_remove_sta_debugfs,
#endif
};

int __init
rc80211_minstrel_init(void)
{
	return ieee80211_rate_control_register(&mac80211_minstrel);
}

void
rc80211_minstrel_exit(void)
{
	ieee80211_rate_control_unregister(&mac80211_minstrel);
}

