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
#include <linux/slab.h>
#include <net/mac80211.h>
#include "rate.h"
#include "rc80211_minstrel.h"

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
	return i;
}

/* return current EMWA throughput */
int minstrel_get_tp_avg(struct minstrel_rate *mr, int prob_ewma)
{
	int usecs;

	usecs = mr->perfect_tx_time;
	if (!usecs)
		usecs = 1000000;

	/* reset thr. below 10% success */
	if (mr->stats.prob_ewma < MINSTREL_FRAC(10, 100))
		return 0;

	if (prob_ewma > MINSTREL_FRAC(90, 100))
		return MINSTREL_TRUNC(100000 * (MINSTREL_FRAC(90, 100) / usecs));
	else
		return MINSTREL_TRUNC(100000 * (prob_ewma / usecs));
}

/* find & sort topmost throughput rates */
static inline void
minstrel_sort_best_tp_rates(struct minstrel_sta_info *mi, int i, u8 *tp_list)
{
	int j;
	struct minstrel_rate_stats *tmp_mrs;
	struct minstrel_rate_stats *cur_mrs = &mi->r[i].stats;

	for (j = MAX_THR_RATES; j > 0; --j) {
		tmp_mrs = &mi->r[tp_list[j - 1]].stats;
		if (minstrel_get_tp_avg(&mi->r[i], cur_mrs->prob_ewma) <=
		    minstrel_get_tp_avg(&mi->r[tp_list[j - 1]], tmp_mrs->prob_ewma))
			break;
	}

	if (j < MAX_THR_RATES - 1)
		memmove(&tp_list[j + 1], &tp_list[j], MAX_THR_RATES - (j + 1));
	if (j < MAX_THR_RATES)
		tp_list[j] = i;
}

static void
minstrel_set_rate(struct minstrel_sta_info *mi, struct ieee80211_sta_rates *ratetbl,
		  int offset, int idx)
{
	struct minstrel_rate *r = &mi->r[idx];

	ratetbl->rate[offset].idx = r->rix;
	ratetbl->rate[offset].count = r->adjusted_retry_count;
	ratetbl->rate[offset].count_cts = r->retry_count_cts;
	ratetbl->rate[offset].count_rts = r->stats.retry_count_rtscts;
}

static void
minstrel_update_rates(struct minstrel_priv *mp, struct minstrel_sta_info *mi)
{
	struct ieee80211_sta_rates *ratetbl;
	int i = 0;

	ratetbl = kzalloc(sizeof(*ratetbl), GFP_ATOMIC);
	if (!ratetbl)
		return;

	/* Start with max_tp_rate */
	minstrel_set_rate(mi, ratetbl, i++, mi->max_tp_rate[0]);

	if (mp->hw->max_rates >= 3) {
		/* At least 3 tx rates supported, use max_tp_rate2 next */
		minstrel_set_rate(mi, ratetbl, i++, mi->max_tp_rate[1]);
	}

	if (mp->hw->max_rates >= 2) {
		/* At least 2 tx rates supported, use max_prob_rate next */
		minstrel_set_rate(mi, ratetbl, i++, mi->max_prob_rate);
	}

	/* Use lowest rate last */
	ratetbl->rate[i].idx = mi->lowest_rix;
	ratetbl->rate[i].count = mp->max_retry;
	ratetbl->rate[i].count_cts = mp->max_retry;
	ratetbl->rate[i].count_rts = mp->max_retry;

	rate_control_set_rates(mp->hw, mi->sta, ratetbl);
}

/*
* Recalculate statistics and counters of a given rate
*/
void
minstrel_calc_rate_stats(struct minstrel_rate_stats *mrs)
{
	unsigned int cur_prob;

	if (unlikely(mrs->attempts > 0)) {
		mrs->sample_skipped = 0;
		cur_prob = MINSTREL_FRAC(mrs->success, mrs->attempts);
		if (unlikely(!mrs->att_hist)) {
			mrs->prob_ewma = cur_prob;
		} else {
			/* update exponential weighted moving variance */
			mrs->prob_ewmv = minstrel_ewmv(mrs->prob_ewmv,
							cur_prob,
							mrs->prob_ewma,
							EWMA_LEVEL);

			/*update exponential weighted moving avarage */
			mrs->prob_ewma = minstrel_ewma(mrs->prob_ewma,
						       cur_prob,
						       EWMA_LEVEL);
		}
		mrs->att_hist += mrs->attempts;
		mrs->succ_hist += mrs->success;
	} else {
		mrs->sample_skipped++;
	}

	mrs->last_success = mrs->success;
	mrs->last_attempts = mrs->attempts;
	mrs->success = 0;
	mrs->attempts = 0;
}

static void
minstrel_update_stats(struct minstrel_priv *mp, struct minstrel_sta_info *mi)
{
	u8 tmp_tp_rate[MAX_THR_RATES];
	u8 tmp_prob_rate = 0;
	int i, tmp_cur_tp, tmp_prob_tp;

	for (i = 0; i < MAX_THR_RATES; i++)
	    tmp_tp_rate[i] = 0;

	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		struct minstrel_rate_stats *mrs = &mi->r[i].stats;
		struct minstrel_rate_stats *tmp_mrs = &mi->r[tmp_prob_rate].stats;

		/* Update statistics of success probability per rate */
		minstrel_calc_rate_stats(mrs);

		/* Sample less often below the 10% chance of success.
		 * Sample less often above the 95% chance of success. */
		if (mrs->prob_ewma > MINSTREL_FRAC(95, 100) ||
		    mrs->prob_ewma < MINSTREL_FRAC(10, 100)) {
			mr->adjusted_retry_count = mrs->retry_count >> 1;
			if (mr->adjusted_retry_count > 2)
				mr->adjusted_retry_count = 2;
			mr->sample_limit = 4;
		} else {
			mr->sample_limit = -1;
			mr->adjusted_retry_count = mrs->retry_count;
		}
		if (!mr->adjusted_retry_count)
			mr->adjusted_retry_count = 2;

		minstrel_sort_best_tp_rates(mi, i, tmp_tp_rate);

		/* To determine the most robust rate (max_prob_rate) used at
		 * 3rd mmr stage we distinct between two cases:
		 * (1) if any success probabilitiy >= 95%, out of those rates
		 * choose the maximum throughput rate as max_prob_rate
		 * (2) if all success probabilities < 95%, the rate with
		 * highest success probability is chosen as max_prob_rate */
		if (mrs->prob_ewma >= MINSTREL_FRAC(95, 100)) {
			tmp_cur_tp = minstrel_get_tp_avg(mr, mrs->prob_ewma);
			tmp_prob_tp = minstrel_get_tp_avg(&mi->r[tmp_prob_rate],
							  tmp_mrs->prob_ewma);
			if (tmp_cur_tp >= tmp_prob_tp)
				tmp_prob_rate = i;
		} else {
			if (mrs->prob_ewma >= tmp_mrs->prob_ewma)
				tmp_prob_rate = i;
		}
	}

	/* Assign the new rate set */
	memcpy(mi->max_tp_rate, tmp_tp_rate, sizeof(mi->max_tp_rate));
	mi->max_prob_rate = tmp_prob_rate;

#ifdef CONFIG_MAC80211_DEBUGFS
	/* use fixed index if set */
	if (mp->fixed_rate_idx != -1) {
		mi->max_tp_rate[0] = mp->fixed_rate_idx;
		mi->max_tp_rate[1] = mp->fixed_rate_idx;
		mi->max_prob_rate = mp->fixed_rate_idx;
	}
#endif

	/* Reset update timer */
	mi->last_stats_update = jiffies;

	minstrel_update_rates(mp, mi);
}

static void
minstrel_tx_status(void *priv, struct ieee80211_supported_band *sband,
		   void *priv_sta, struct ieee80211_tx_status *st)
{
	struct ieee80211_tx_info *info = st->info;
	struct minstrel_priv *mp = priv;
	struct minstrel_sta_info *mi = priv_sta;
	struct ieee80211_tx_rate *ar = info->status.rates;
	int i, ndx;
	int success;

	success = !!(info->flags & IEEE80211_TX_STAT_ACK);

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (ar[i].idx < 0)
			break;

		ndx = rix_to_ndx(mi, ar[i].idx);
		if (ndx < 0)
			continue;

		mi->r[ndx].stats.attempts += ar[i].count;

		if ((i != IEEE80211_TX_MAX_RATES - 1) && (ar[i + 1].idx < 0))
			mi->r[ndx].stats.success += success;
	}

	if ((info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) && (i >= 0))
		mi->sample_packets++;

	if (mi->sample_deferred > 0)
		mi->sample_deferred--;

	if (time_after(jiffies, mi->last_stats_update +
				(mp->update_interval * HZ) / 1000))
		minstrel_update_stats(mp, mi);
}


static inline unsigned int
minstrel_get_retry_count(struct minstrel_rate *mr,
			 struct ieee80211_tx_info *info)
{
	u8 retry = mr->adjusted_retry_count;

	if (info->control.use_rts)
		retry = max_t(u8, 2, min(mr->stats.retry_count_rtscts, retry));
	else if (info->control.use_cts_prot)
		retry = max_t(u8, 2, min(mr->retry_count_cts, retry));
	return retry;
}


static int
minstrel_get_next_sample(struct minstrel_sta_info *mi)
{
	unsigned int sample_ndx;
	sample_ndx = SAMPLE_TBL(mi, mi->sample_row, mi->sample_column);
	mi->sample_row++;
	if ((int) mi->sample_row >= mi->n_rates) {
		mi->sample_row = 0;
		mi->sample_column++;
		if (mi->sample_column >= SAMPLE_COLUMNS)
			mi->sample_column = 0;
	}
	return sample_ndx;
}

static void
minstrel_get_rate(void *priv, struct ieee80211_sta *sta,
		  void *priv_sta, struct ieee80211_tx_rate_control *txrc)
{
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct minstrel_sta_info *mi = priv_sta;
	struct minstrel_priv *mp = priv;
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct minstrel_rate *msr, *mr;
	unsigned int ndx;
	bool mrr_capable;
	bool prev_sample;
	int delta;
	int sampling_ratio;

	/* management/no-ack frames do not use rate control */
	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	/* check multi-rate-retry capabilities & adjust lookaround_rate */
	mrr_capable = mp->has_mrr &&
		      !txrc->rts &&
		      !txrc->bss_conf->use_cts_prot;
	if (mrr_capable)
		sampling_ratio = mp->lookaround_rate_mrr;
	else
		sampling_ratio = mp->lookaround_rate;

	/* increase sum packet counter */
	mi->total_packets++;

#ifdef CONFIG_MAC80211_DEBUGFS
	if (mp->fixed_rate_idx != -1)
		return;
#endif

	/* Don't use EAPOL frames for sampling on non-mrr hw */
	if (mp->hw->max_rates == 1 &&
	    (info->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO))
		return;

	delta = (mi->total_packets * sampling_ratio / 100) -
			(mi->sample_packets + mi->sample_deferred / 2);

	/* delta < 0: no sampling required */
	prev_sample = mi->prev_sample;
	mi->prev_sample = false;
	if (delta < 0 || (!mrr_capable && prev_sample))
		return;

	if (mi->total_packets >= 10000) {
		mi->sample_deferred = 0;
		mi->sample_packets = 0;
		mi->total_packets = 0;
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
		mi->sample_packets += (delta - mi->n_rates * 2);
	}

	/* get next random rate sample */
	ndx = minstrel_get_next_sample(mi);
	msr = &mi->r[ndx];
	mr = &mi->r[mi->max_tp_rate[0]];

	/* Decide if direct ( 1st mrr stage) or indirect (2nd mrr stage)
	 * rate sampling method should be used.
	 * Respect such rates that are not sampled for 20 interations.
	 */
	if (mrr_capable &&
	    msr->perfect_tx_time > mr->perfect_tx_time &&
	    msr->stats.sample_skipped < 20) {
		/* Only use IEEE80211_TX_CTL_RATE_CTRL_PROBE to mark
		 * packets that have the sampling rate deferred to the
		 * second MRR stage. Increase the sample counter only
		 * if the deferred sample rate was actually used.
		 * Use the sample_deferred counter to make sure that
		 * the sampling is not done in large bursts */
		info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
		rate++;
		mi->sample_deferred++;
	} else {
		if (!msr->sample_limit)
			return;

		mi->sample_packets++;
		if (msr->sample_limit > 0)
			msr->sample_limit--;
	}

	/* If we're not using MRR and the sampling rate already
	 * has a probability of >95%, we shouldn't be attempting
	 * to use it, as this only wastes precious airtime */
	if (!mrr_capable &&
	   (mi->r[ndx].stats.prob_ewma > MINSTREL_FRAC(95, 100)))
		return;

	mi->prev_sample = true;

	rate->idx = mi->r[ndx].rix;
	rate->count = minstrel_get_retry_count(&mi->r[ndx], info);
}


static void
calc_rate_durations(enum nl80211_band band,
		    struct minstrel_rate *d,
		    struct ieee80211_rate *rate,
		    struct cfg80211_chan_def *chandef)
{
	int erp = !!(rate->flags & IEEE80211_RATE_ERP_G);
	int shift = ieee80211_chandef_get_shift(chandef);

	d->perfect_tx_time = ieee80211_frame_duration(band, 1200,
			DIV_ROUND_UP(rate->bitrate, 1 << shift), erp, 1,
			shift);
	d->ack_time = ieee80211_frame_duration(band, 10,
			DIV_ROUND_UP(rate->bitrate, 1 << shift), erp, 1,
			shift);
}

static void
init_sample_table(struct minstrel_sta_info *mi)
{
	unsigned int i, col, new_idx;
	u8 rnd[8];

	mi->sample_column = 0;
	mi->sample_row = 0;
	memset(mi->sample_table, 0xff, SAMPLE_COLUMNS * mi->n_rates);

	for (col = 0; col < SAMPLE_COLUMNS; col++) {
		prandom_bytes(rnd, sizeof(rnd));
		for (i = 0; i < mi->n_rates; i++) {
			new_idx = (i + rnd[i & 7]) % mi->n_rates;
			while (SAMPLE_TBL(mi, new_idx, col) != 0xff)
				new_idx = (new_idx + 1) % mi->n_rates;

			SAMPLE_TBL(mi, new_idx, col) = i;
		}
	}
}

static void
minstrel_rate_init(void *priv, struct ieee80211_supported_band *sband,
		   struct cfg80211_chan_def *chandef,
		   struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_sta_info *mi = priv_sta;
	struct minstrel_priv *mp = priv;
	struct ieee80211_rate *ctl_rate;
	unsigned int i, n = 0;
	unsigned int t_slot = 9; /* FIXME: get real slot time */
	u32 rate_flags;

	mi->sta = sta;
	mi->lowest_rix = rate_lowest_index(sband, sta);
	ctl_rate = &sband->bitrates[mi->lowest_rix];
	mi->sp_ack_dur = ieee80211_frame_duration(sband->band, 10,
				ctl_rate->bitrate,
				!!(ctl_rate->flags & IEEE80211_RATE_ERP_G), 1,
				ieee80211_chandef_get_shift(chandef));

	rate_flags = ieee80211_chandef_rate_flags(&mp->hw->conf.chandef);
	memset(mi->max_tp_rate, 0, sizeof(mi->max_tp_rate));
	mi->max_prob_rate = 0;

	for (i = 0; i < sband->n_bitrates; i++) {
		struct minstrel_rate *mr = &mi->r[n];
		struct minstrel_rate_stats *mrs = &mi->r[n].stats;
		unsigned int tx_time = 0, tx_time_cts = 0, tx_time_rtscts = 0;
		unsigned int tx_time_single;
		unsigned int cw = mp->cw_min;
		int shift;

		if (!rate_supported(sta, sband->band, i))
			continue;
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;

		n++;
		memset(mr, 0, sizeof(*mr));
		memset(mrs, 0, sizeof(*mrs));

		mr->rix = i;
		shift = ieee80211_chandef_get_shift(chandef);
		mr->bitrate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
					   (1 << shift) * 5);
		calc_rate_durations(sband->band, mr, &sband->bitrates[i],
				    chandef);

		/* calculate maximum number of retransmissions before
		 * fallback (based on maximum segment size) */
		mr->sample_limit = -1;
		mrs->retry_count = 1;
		mr->retry_count_cts = 1;
		mrs->retry_count_rtscts = 1;
		tx_time = mr->perfect_tx_time + mi->sp_ack_dur;
		do {
			/* add one retransmission */
			tx_time_single = mr->ack_time + mr->perfect_tx_time;

			/* contention window */
			tx_time_single += (t_slot * cw) >> 1;
			cw = min((cw << 1) | 1, mp->cw_max);

			tx_time += tx_time_single;
			tx_time_cts += tx_time_single + mi->sp_ack_dur;
			tx_time_rtscts += tx_time_single + 2 * mi->sp_ack_dur;
			if ((tx_time_cts < mp->segment_size) &&
				(mr->retry_count_cts < mp->max_retry))
				mr->retry_count_cts++;
			if ((tx_time_rtscts < mp->segment_size) &&
				(mrs->retry_count_rtscts < mp->max_retry))
				mrs->retry_count_rtscts++;
		} while ((tx_time < mp->segment_size) &&
				(++mr->stats.retry_count < mp->max_retry));
		mr->adjusted_retry_count = mrs->retry_count;
		if (!(sband->bitrates[i].flags & IEEE80211_RATE_ERP_G))
			mr->retry_count_cts = mrs->retry_count;
	}

	for (i = n; i < sband->n_bitrates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		mr->rix = -1;
	}

	mi->n_rates = n;
	mi->last_stats_update = jiffies;

	init_sample_table(mi);
	minstrel_update_rates(mp, mi);
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

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		sband = hw->wiphy->bands[i];
		if (sband && sband->n_bitrates > max_rates)
			max_rates = sband->n_bitrates;
	}

	mi->r = kzalloc(sizeof(struct minstrel_rate) * max_rates, gfp);
	if (!mi->r)
		goto error;

	mi->sample_table = kmalloc(SAMPLE_COLUMNS * max_rates, gfp);
	if (!mi->sample_table)
		goto error1;

	mi->last_stats_update = jiffies;
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
minstrel_init_cck_rates(struct minstrel_priv *mp)
{
	static const int bitrates[4] = { 10, 20, 55, 110 };
	struct ieee80211_supported_band *sband;
	u32 rate_flags = ieee80211_chandef_rate_flags(&mp->hw->conf.chandef);
	int i, j;

	sband = mp->hw->wiphy->bands[NL80211_BAND_2GHZ];
	if (!sband)
		return;

	for (i = 0, j = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *rate = &sband->bitrates[i];

		if (rate->flags & IEEE80211_RATE_ERP_G)
			continue;

		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;

		for (j = 0; j < ARRAY_SIZE(bitrates); j++) {
			if (rate->bitrate != bitrates[j])
				continue;

			mp->cck_rates[j] = i;
			break;
		}
	}
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

	/* maximum time that the hw is allowed to stay in one MRR segment */
	mp->segment_size = 6000;

	if (hw->max_rate_tries > 0)
		mp->max_retry = hw->max_rate_tries;
	else
		/* safe default, does not necessarily have to match hw properties */
		mp->max_retry = 7;

	if (hw->max_rates >= 4)
		mp->has_mrr = true;

	mp->hw = hw;
	mp->update_interval = 100;

#ifdef CONFIG_MAC80211_DEBUGFS
	mp->fixed_rate_idx = (u32) -1;
	mp->dbg_fixed_rate = debugfs_create_u32("fixed_rate_idx",
			0666, debugfsdir, &mp->fixed_rate_idx);
#endif

	minstrel_init_cck_rates(mp);

	return mp;
}

static void
minstrel_free(void *priv)
{
#ifdef CONFIG_MAC80211_DEBUGFS
	debugfs_remove(((struct minstrel_priv *)priv)->dbg_fixed_rate);
#endif
	kfree(priv);
}

static u32 minstrel_get_expected_throughput(void *priv_sta)
{
	struct minstrel_sta_info *mi = priv_sta;
	struct minstrel_rate_stats *tmp_mrs;
	int idx = mi->max_tp_rate[0];
	int tmp_cur_tp;

	/* convert pkt per sec in kbps (1200 is the average pkt size used for
	 * computing cur_tp
	 */
	tmp_mrs = &mi->r[idx].stats;
	tmp_cur_tp = minstrel_get_tp_avg(&mi->r[idx], tmp_mrs->prob_ewma) * 10;
	tmp_cur_tp = tmp_cur_tp * 1200 * 8 / 1024;

	return tmp_cur_tp;
}

const struct rate_control_ops mac80211_minstrel = {
	.name = "minstrel",
	.tx_status_ext = minstrel_tx_status,
	.get_rate = minstrel_get_rate,
	.rate_init = minstrel_rate_init,
	.alloc = minstrel_alloc,
	.free = minstrel_free,
	.alloc_sta = minstrel_alloc_sta,
	.free_sta = minstrel_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = minstrel_add_sta_debugfs,
	.remove_sta_debugfs = minstrel_remove_sta_debugfs,
#endif
	.get_expected_throughput = minstrel_get_expected_throughput,
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

