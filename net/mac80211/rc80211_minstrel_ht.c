/*
 * Copyright (C) 2010 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include "rc80211_minstrel_ht.h"

#define AVG_PKT_SIZE	1200
#define SAMPLE_COLUMNS	10
#define EWMA_LEVEL		75

/* Number of bits for an average sized packet */
#define MCS_NBITS (AVG_PKT_SIZE << 3)

/* Number of symbols for a packet with (bps) bits per symbol */
#define MCS_NSYMS(bps) ((MCS_NBITS + (bps) - 1) / (bps))

/* Transmission time for a packet containing (syms) symbols */
#define MCS_SYMBOL_TIME(sgi, syms)					\
	(sgi ?								\
	  ((syms) * 18 + 4) / 5 :	/* syms * 3.6 us */		\
	  (syms) << 2			/* syms * 4 us */		\
	)

/* Transmit duration for the raw data part of an average sized packet */
#define MCS_DURATION(streams, sgi, bps) MCS_SYMBOL_TIME(sgi, MCS_NSYMS((streams) * (bps)))

/* MCS rate information for an MCS group */
#define MCS_GROUP(_streams, _sgi, _ht40) {				\
	.streams = _streams,						\
	.flags =							\
		(_sgi ? IEEE80211_TX_RC_SHORT_GI : 0) |			\
		(_ht40 ? IEEE80211_TX_RC_40_MHZ_WIDTH : 0),		\
	.duration = {							\
		MCS_DURATION(_streams, _sgi, _ht40 ? 54 : 26),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 108 : 52),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 162 : 78),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 216 : 104),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 324 : 156),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 432 : 208),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 486 : 234),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 540 : 260)		\
	}								\
}

/*
 * To enable sufficiently targeted rate sampling, MCS rates are divided into
 * groups, based on the number of streams and flags (HT40, SGI) that they
 * use.
 */
const struct mcs_group minstrel_mcs_groups[] = {
	MCS_GROUP(1, 0, 0),
	MCS_GROUP(2, 0, 0),
#if MINSTREL_MAX_STREAMS >= 3
	MCS_GROUP(3, 0, 0),
#endif

	MCS_GROUP(1, 1, 0),
	MCS_GROUP(2, 1, 0),
#if MINSTREL_MAX_STREAMS >= 3
	MCS_GROUP(3, 1, 0),
#endif

	MCS_GROUP(1, 0, 1),
	MCS_GROUP(2, 0, 1),
#if MINSTREL_MAX_STREAMS >= 3
	MCS_GROUP(3, 0, 1),
#endif

	MCS_GROUP(1, 1, 1),
	MCS_GROUP(2, 1, 1),
#if MINSTREL_MAX_STREAMS >= 3
	MCS_GROUP(3, 1, 1),
#endif
};

static u8 sample_table[SAMPLE_COLUMNS][MCS_GROUP_RATES];

/*
 * Perform EWMA (Exponentially Weighted Moving Average) calculation
 */
static int
minstrel_ewma(int old, int new, int weight)
{
	return (new * (100 - weight) + old * weight) / 100;
}

/*
 * Look up an MCS group index based on mac80211 rate information
 */
static int
minstrel_ht_get_group_idx(struct ieee80211_tx_rate *rate)
{
	int streams = (rate->idx / MCS_GROUP_RATES) + 1;
	u32 flags = IEEE80211_TX_RC_SHORT_GI | IEEE80211_TX_RC_40_MHZ_WIDTH;
	int i;

	for (i = 0; i < ARRAY_SIZE(minstrel_mcs_groups); i++) {
		if (minstrel_mcs_groups[i].streams != streams)
			continue;
		if (minstrel_mcs_groups[i].flags != (rate->flags & flags))
			continue;

		return i;
	}

	WARN_ON(1);
	return 0;
}

static inline struct minstrel_rate_stats *
minstrel_get_ratestats(struct minstrel_ht_sta *mi, int index)
{
	return &mi->groups[index / MCS_GROUP_RATES].rates[index % MCS_GROUP_RATES];
}


/*
 * Recalculate success probabilities and counters for a rate using EWMA
 */
static void
minstrel_calc_rate_ewma(struct minstrel_priv *mp, struct minstrel_rate_stats *mr)
{
	if (unlikely(mr->attempts > 0)) {
		mr->sample_skipped = 0;
		mr->cur_prob = MINSTREL_FRAC(mr->success, mr->attempts);
		if (!mr->att_hist)
			mr->probability = mr->cur_prob;
		else
			mr->probability = minstrel_ewma(mr->probability,
				mr->cur_prob, EWMA_LEVEL);
		mr->att_hist += mr->attempts;
		mr->succ_hist += mr->success;
	} else {
		mr->sample_skipped++;
	}
	mr->last_success = mr->success;
	mr->last_attempts = mr->attempts;
	mr->success = 0;
	mr->attempts = 0;
}

/*
 * Calculate throughput based on the average A-MPDU length, taking into account
 * the expected number of retransmissions and their expected length
 */
static void
minstrel_ht_calc_tp(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
                    int group, int rate)
{
	struct minstrel_rate_stats *mr;
	unsigned int usecs;

	mr = &mi->groups[group].rates[rate];

	if (mr->probability < MINSTREL_FRAC(1, 10)) {
		mr->cur_tp = 0;
		return;
	}

	usecs = mi->overhead / MINSTREL_TRUNC(mi->avg_ampdu_len);
	usecs += minstrel_mcs_groups[group].duration[rate];
	mr->cur_tp = MINSTREL_TRUNC((1000000 / usecs) * mr->probability);
}

/*
 * Update rate statistics and select new primary rates
 *
 * Rules for rate selection:
 *  - max_prob_rate must use only one stream, as a tradeoff between delivery
 *    probability and throughput during strong fluctuations
 *  - as long as the max prob rate has a probability of more than 3/4, pick
 *    higher throughput rates, even if the probablity is a bit lower
 */
static void
minstrel_ht_update_stats(struct minstrel_priv *mp, struct minstrel_ht_sta *mi)
{
	struct minstrel_mcs_group_data *mg;
	struct minstrel_rate_stats *mr;
	int cur_prob, cur_prob_tp, cur_tp, cur_tp2;
	int group, i, index;

	if (mi->ampdu_packets > 0) {
		mi->avg_ampdu_len = minstrel_ewma(mi->avg_ampdu_len,
			MINSTREL_FRAC(mi->ampdu_len, mi->ampdu_packets), EWMA_LEVEL);
		mi->ampdu_len = 0;
		mi->ampdu_packets = 0;
	}

	mi->sample_slow = 0;
	mi->sample_count = 0;
	mi->max_tp_rate = 0;
	mi->max_tp_rate2 = 0;
	mi->max_prob_rate = 0;

	for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
		cur_prob = 0;
		cur_prob_tp = 0;
		cur_tp = 0;
		cur_tp2 = 0;

		mg = &mi->groups[group];
		if (!mg->supported)
			continue;

		mg->max_tp_rate = 0;
		mg->max_tp_rate2 = 0;
		mg->max_prob_rate = 0;
		mi->sample_count++;

		for (i = 0; i < MCS_GROUP_RATES; i++) {
			if (!(mg->supported & BIT(i)))
				continue;

			mr = &mg->rates[i];
			mr->retry_updated = false;
			index = MCS_GROUP_RATES * group + i;
			minstrel_calc_rate_ewma(mp, mr);
			minstrel_ht_calc_tp(mp, mi, group, i);

			if (!mr->cur_tp)
				continue;

			/* ignore the lowest rate of each single-stream group */
			if (!i && minstrel_mcs_groups[group].streams == 1)
				continue;

			if ((mr->cur_tp > cur_prob_tp && mr->probability >
			     MINSTREL_FRAC(3, 4)) || mr->probability > cur_prob) {
				mg->max_prob_rate = index;
				cur_prob = mr->probability;
				cur_prob_tp = mr->cur_tp;
			}

			if (mr->cur_tp > cur_tp) {
				swap(index, mg->max_tp_rate);
				cur_tp = mr->cur_tp;
				mr = minstrel_get_ratestats(mi, index);
			}

			if (index >= mg->max_tp_rate)
				continue;

			if (mr->cur_tp > cur_tp2) {
				mg->max_tp_rate2 = index;
				cur_tp2 = mr->cur_tp;
			}
		}
	}

	/* try to sample up to half of the available rates during each interval */
	mi->sample_count *= 4;

	cur_prob = 0;
	cur_prob_tp = 0;
	cur_tp = 0;
	cur_tp2 = 0;
	for (group = 0; group < ARRAY_SIZE(minstrel_mcs_groups); group++) {
		mg = &mi->groups[group];
		if (!mg->supported)
			continue;

		mr = minstrel_get_ratestats(mi, mg->max_prob_rate);
		if (cur_prob_tp < mr->cur_tp &&
		    minstrel_mcs_groups[group].streams == 1) {
			mi->max_prob_rate = mg->max_prob_rate;
			cur_prob = mr->cur_prob;
			cur_prob_tp = mr->cur_tp;
		}

		mr = minstrel_get_ratestats(mi, mg->max_tp_rate);
		if (cur_tp < mr->cur_tp) {
			mi->max_tp_rate2 = mi->max_tp_rate;
			cur_tp2 = cur_tp;
			mi->max_tp_rate = mg->max_tp_rate;
			cur_tp = mr->cur_tp;
		}

		mr = minstrel_get_ratestats(mi, mg->max_tp_rate2);
		if (cur_tp2 < mr->cur_tp) {
			mi->max_tp_rate2 = mg->max_tp_rate2;
			cur_tp2 = mr->cur_tp;
		}
	}

	mi->stats_update = jiffies;
}

static bool
minstrel_ht_txstat_valid(struct ieee80211_tx_rate *rate)
{
	if (rate->idx < 0)
		return false;

	if (!rate->count)
		return false;

	return !!(rate->flags & IEEE80211_TX_RC_MCS);
}

static void
minstrel_next_sample_idx(struct minstrel_ht_sta *mi)
{
	struct minstrel_mcs_group_data *mg;

	for (;;) {
		mi->sample_group++;
		mi->sample_group %= ARRAY_SIZE(minstrel_mcs_groups);
		mg = &mi->groups[mi->sample_group];

		if (!mg->supported)
			continue;

		if (++mg->index >= MCS_GROUP_RATES) {
			mg->index = 0;
			if (++mg->column >= ARRAY_SIZE(sample_table))
				mg->column = 0;
		}
		break;
	}
}

static void
minstrel_downgrade_rate(struct minstrel_ht_sta *mi, unsigned int *idx,
			bool primary)
{
	int group, orig_group;

	orig_group = group = *idx / MCS_GROUP_RATES;
	while (group > 0) {
		group--;

		if (!mi->groups[group].supported)
			continue;

		if (minstrel_mcs_groups[group].streams >
		    minstrel_mcs_groups[orig_group].streams)
			continue;

		if (primary)
			*idx = mi->groups[group].max_tp_rate;
		else
			*idx = mi->groups[group].max_tp_rate2;
		break;
	}
}

static void
minstrel_aggr_check(struct minstrel_priv *mp, struct ieee80211_sta *pubsta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	u16 tid;

	if (unlikely(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	if (likely(sta->ampdu_mlme.tid_tx[tid]))
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	ieee80211_start_tx_ba_session(pubsta, tid, 5000);
}

static void
minstrel_ht_tx_status(void *priv, struct ieee80211_supported_band *sband,
                      struct ieee80211_sta *sta, void *priv_sta,
                      struct sk_buff *skb)
{
	struct minstrel_ht_sta_priv *msp = priv_sta;
	struct minstrel_ht_sta *mi = &msp->ht;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *ar = info->status.rates;
	struct minstrel_rate_stats *rate, *rate2;
	struct minstrel_priv *mp = priv;
	bool last = false;
	int group;
	int i = 0;

	if (!msp->is_ht)
		return mac80211_minstrel.tx_status(priv, sband, sta, &msp->legacy, skb);

	/* This packet was aggregated but doesn't carry status info */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	if (!(info->flags & IEEE80211_TX_STAT_AMPDU)) {
		info->status.ampdu_ack_len =
			(info->flags & IEEE80211_TX_STAT_ACK ? 1 : 0);
		info->status.ampdu_len = 1;
	}

	mi->ampdu_packets++;
	mi->ampdu_len += info->status.ampdu_len;

	if (!mi->sample_wait && !mi->sample_tries && mi->sample_count > 0) {
		mi->sample_wait = 16 + 2 * MINSTREL_TRUNC(mi->avg_ampdu_len);
		mi->sample_tries = 2;
		mi->sample_count--;
	}

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		mi->sample_packets += info->status.ampdu_len;

	for (i = 0; !last; i++) {
		last = (i == IEEE80211_TX_MAX_RATES - 1) ||
		       !minstrel_ht_txstat_valid(&ar[i + 1]);

		if (!minstrel_ht_txstat_valid(&ar[i]))
			break;

		group = minstrel_ht_get_group_idx(&ar[i]);
		rate = &mi->groups[group].rates[ar[i].idx % 8];

		if (last)
			rate->success += info->status.ampdu_ack_len;

		rate->attempts += ar[i].count * info->status.ampdu_len;
	}

	/*
	 * check for sudden death of spatial multiplexing,
	 * downgrade to a lower number of streams if necessary.
	 */
	rate = minstrel_get_ratestats(mi, mi->max_tp_rate);
	if (rate->attempts > 30 &&
	    MINSTREL_FRAC(rate->success, rate->attempts) <
	    MINSTREL_FRAC(20, 100))
		minstrel_downgrade_rate(mi, &mi->max_tp_rate, true);

	rate2 = minstrel_get_ratestats(mi, mi->max_tp_rate2);
	if (rate2->attempts > 30 &&
	    MINSTREL_FRAC(rate2->success, rate2->attempts) <
	    MINSTREL_FRAC(20, 100))
		minstrel_downgrade_rate(mi, &mi->max_tp_rate2, false);

	if (time_after(jiffies, mi->stats_update + (mp->update_interval / 2 * HZ) / 1000)) {
		minstrel_ht_update_stats(mp, mi);
		if (!(info->flags & IEEE80211_TX_CTL_AMPDU))
			minstrel_aggr_check(mp, sta, skb);
	}
}

static void
minstrel_calc_retransmit(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
                         int index)
{
	struct minstrel_rate_stats *mr;
	const struct mcs_group *group;
	unsigned int tx_time, tx_time_rtscts, tx_time_data;
	unsigned int cw = mp->cw_min;
	unsigned int ctime = 0;
	unsigned int t_slot = 9; /* FIXME */
	unsigned int ampdu_len = MINSTREL_TRUNC(mi->avg_ampdu_len);

	mr = minstrel_get_ratestats(mi, index);
	if (mr->probability < MINSTREL_FRAC(1, 10)) {
		mr->retry_count = 1;
		mr->retry_count_rtscts = 1;
		return;
	}

	mr->retry_count = 2;
	mr->retry_count_rtscts = 2;
	mr->retry_updated = true;

	group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
	tx_time_data = group->duration[index % MCS_GROUP_RATES] * ampdu_len;

	/* Contention time for first 2 tries */
	ctime = (t_slot * cw) >> 1;
	cw = min((cw << 1) | 1, mp->cw_max);
	ctime += (t_slot * cw) >> 1;
	cw = min((cw << 1) | 1, mp->cw_max);

	/* Total TX time for data and Contention after first 2 tries */
	tx_time = ctime + 2 * (mi->overhead + tx_time_data);
	tx_time_rtscts = ctime + 2 * (mi->overhead_rtscts + tx_time_data);

	/* See how many more tries we can fit inside segment size */
	do {
		/* Contention time for this try */
		ctime = (t_slot * cw) >> 1;
		cw = min((cw << 1) | 1, mp->cw_max);

		/* Total TX time after this try */
		tx_time += ctime + mi->overhead + tx_time_data;
		tx_time_rtscts += ctime + mi->overhead_rtscts + tx_time_data;

		if (tx_time_rtscts < mp->segment_size)
			mr->retry_count_rtscts++;
	} while ((tx_time < mp->segment_size) &&
	         (++mr->retry_count < mp->max_retry));
}


static void
minstrel_ht_set_rate(struct minstrel_priv *mp, struct minstrel_ht_sta *mi,
                     struct ieee80211_tx_rate *rate, int index,
                     struct ieee80211_tx_rate_control *txrc,
                     bool sample, bool rtscts)
{
	const struct mcs_group *group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
	struct minstrel_rate_stats *mr;

	mr = minstrel_get_ratestats(mi, index);
	if (!mr->retry_updated)
		minstrel_calc_retransmit(mp, mi, index);

	if (sample)
		rate->count = 1;
	else if (mr->probability < MINSTREL_FRAC(20, 100))
		rate->count = 2;
	else if (rtscts)
		rate->count = mr->retry_count_rtscts;
	else
		rate->count = mr->retry_count;

	rate->flags = IEEE80211_TX_RC_MCS | group->flags;
	if (rtscts)
		rate->flags |= IEEE80211_TX_RC_USE_RTS_CTS;
	rate->idx = index % MCS_GROUP_RATES + (group->streams - 1) * MCS_GROUP_RATES;
}

static inline int
minstrel_get_duration(int index)
{
	const struct mcs_group *group = &minstrel_mcs_groups[index / MCS_GROUP_RATES];
	return group->duration[index % MCS_GROUP_RATES];
}

static int
minstrel_get_sample_rate(struct minstrel_priv *mp, struct minstrel_ht_sta *mi)
{
	struct minstrel_rate_stats *mr;
	struct minstrel_mcs_group_data *mg;
	int sample_idx = 0;

	if (mi->sample_wait > 0) {
		mi->sample_wait--;
		return -1;
	}

	if (!mi->sample_tries)
		return -1;

	mi->sample_tries--;
	mg = &mi->groups[mi->sample_group];
	sample_idx = sample_table[mg->column][mg->index];
	mr = &mg->rates[sample_idx];
	sample_idx += mi->sample_group * MCS_GROUP_RATES;
	minstrel_next_sample_idx(mi);

	/*
	 * When not using MRR, do not sample if the probability is already
	 * higher than 95% to avoid wasting airtime
	 */
	if (!mp->has_mrr && (mr->probability > MINSTREL_FRAC(95, 100)))
		return -1;

	/*
	 * Make sure that lower rates get sampled only occasionally,
	 * if the link is working perfectly.
	 */
	if (minstrel_get_duration(sample_idx) >
	    minstrel_get_duration(mi->max_tp_rate)) {
		if (mr->sample_skipped < 20)
			return -1;

		if (mi->sample_slow++ > 2)
			return -1;
	}

	return sample_idx;
}

static void
minstrel_ht_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
                     struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	struct ieee80211_tx_rate *ar = info->status.rates;
	struct minstrel_ht_sta_priv *msp = priv_sta;
	struct minstrel_ht_sta *mi = &msp->ht;
	struct minstrel_priv *mp = priv;
	int sample_idx;
	bool sample = false;

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	if (!msp->is_ht)
		return mac80211_minstrel.get_rate(priv, sta, &msp->legacy, txrc);

	info->flags |= mi->tx_flags;

	/* Don't use EAPOL frames for sampling on non-mrr hw */
	if (mp->hw->max_rates == 1 &&
	    txrc->skb->protocol == cpu_to_be16(ETH_P_PAE))
		sample_idx = -1;
	else
		sample_idx = minstrel_get_sample_rate(mp, mi);

#ifdef CONFIG_MAC80211_DEBUGFS
	/* use fixed index if set */
	if (mp->fixed_rate_idx != -1)
		sample_idx = mp->fixed_rate_idx;
#endif

	if (sample_idx >= 0) {
		sample = true;
		minstrel_ht_set_rate(mp, mi, &ar[0], sample_idx,
			txrc, true, false);
		info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
	} else {
		minstrel_ht_set_rate(mp, mi, &ar[0], mi->max_tp_rate,
			txrc, false, false);
	}

	if (mp->hw->max_rates >= 3) {
		/*
		 * At least 3 tx rates supported, use
		 * sample_rate -> max_tp_rate -> max_prob_rate for sampling and
		 * max_tp_rate -> max_tp_rate2 -> max_prob_rate by default.
		 */
		if (sample_idx >= 0)
			minstrel_ht_set_rate(mp, mi, &ar[1], mi->max_tp_rate,
				txrc, false, false);
		else
			minstrel_ht_set_rate(mp, mi, &ar[1], mi->max_tp_rate2,
				txrc, false, true);

		minstrel_ht_set_rate(mp, mi, &ar[2], mi->max_prob_rate,
				     txrc, false, !sample);

		ar[3].count = 0;
		ar[3].idx = -1;
	} else if (mp->hw->max_rates == 2) {
		/*
		 * Only 2 tx rates supported, use
		 * sample_rate -> max_prob_rate for sampling and
		 * max_tp_rate -> max_prob_rate by default.
		 */
		minstrel_ht_set_rate(mp, mi, &ar[1], mi->max_prob_rate,
				     txrc, false, !sample);

		ar[2].count = 0;
		ar[2].idx = -1;
	} else {
		/* Not using MRR, only use the first rate */
		ar[1].count = 0;
		ar[1].idx = -1;
	}

	mi->total_packets++;

	/* wraparound */
	if (mi->total_packets == ~0) {
		mi->total_packets = 0;
		mi->sample_packets = 0;
	}
}

static void
minstrel_ht_update_caps(void *priv, struct ieee80211_supported_band *sband,
                        struct ieee80211_sta *sta, void *priv_sta,
			enum nl80211_channel_type oper_chan_type)
{
	struct minstrel_priv *mp = priv;
	struct minstrel_ht_sta_priv *msp = priv_sta;
	struct minstrel_ht_sta *mi = &msp->ht;
	struct ieee80211_mcs_info *mcs = &sta->ht_cap.mcs;
	struct ieee80211_local *local = hw_to_local(mp->hw);
	u16 sta_cap = sta->ht_cap.cap;
	int n_supported = 0;
	int ack_dur;
	int stbc;
	int i;

	/* fall back to the old minstrel for legacy stations */
	if (!sta->ht_cap.ht_supported)
		goto use_legacy;

	BUILD_BUG_ON(ARRAY_SIZE(minstrel_mcs_groups) !=
		MINSTREL_MAX_STREAMS * MINSTREL_STREAM_GROUPS);

	msp->is_ht = true;
	memset(mi, 0, sizeof(*mi));
	mi->stats_update = jiffies;

	ack_dur = ieee80211_frame_duration(local, 10, 60, 1, 1);
	mi->overhead = ieee80211_frame_duration(local, 0, 60, 1, 1) + ack_dur;
	mi->overhead_rtscts = mi->overhead + 2 * ack_dur;

	mi->avg_ampdu_len = MINSTREL_FRAC(1, 1);

	/* When using MRR, sample more on the first attempt, without delay */
	if (mp->has_mrr) {
		mi->sample_count = 16;
		mi->sample_wait = 0;
	} else {
		mi->sample_count = 8;
		mi->sample_wait = 8;
	}
	mi->sample_tries = 4;

	stbc = (sta_cap & IEEE80211_HT_CAP_RX_STBC) >>
		IEEE80211_HT_CAP_RX_STBC_SHIFT;
	mi->tx_flags |= stbc << IEEE80211_TX_CTL_STBC_SHIFT;

	if (sta_cap & IEEE80211_HT_CAP_LDPC_CODING)
		mi->tx_flags |= IEEE80211_TX_CTL_LDPC;

	if (oper_chan_type != NL80211_CHAN_HT40MINUS &&
	    oper_chan_type != NL80211_CHAN_HT40PLUS)
		sta_cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	for (i = 0; i < ARRAY_SIZE(mi->groups); i++) {
		u16 req = 0;

		mi->groups[i].supported = 0;
		if (minstrel_mcs_groups[i].flags & IEEE80211_TX_RC_SHORT_GI) {
			if (minstrel_mcs_groups[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
				req |= IEEE80211_HT_CAP_SGI_40;
			else
				req |= IEEE80211_HT_CAP_SGI_20;
		}

		if (minstrel_mcs_groups[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			req |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

		if ((sta_cap & req) != req)
			continue;

		mi->groups[i].supported =
			mcs->rx_mask[minstrel_mcs_groups[i].streams - 1];

		if (mi->groups[i].supported)
			n_supported++;
	}

	if (!n_supported)
		goto use_legacy;

	return;

use_legacy:
	msp->is_ht = false;
	memset(&msp->legacy, 0, sizeof(msp->legacy));
	msp->legacy.r = msp->ratelist;
	msp->legacy.sample_table = msp->sample_table;
	return mac80211_minstrel.rate_init(priv, sband, sta, &msp->legacy);
}

static void
minstrel_ht_rate_init(void *priv, struct ieee80211_supported_band *sband,
                      struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_priv *mp = priv;

	minstrel_ht_update_caps(priv, sband, sta, priv_sta, mp->hw->conf.channel_type);
}

static void
minstrel_ht_rate_update(void *priv, struct ieee80211_supported_band *sband,
                        struct ieee80211_sta *sta, void *priv_sta,
                        u32 changed, enum nl80211_channel_type oper_chan_type)
{
	minstrel_ht_update_caps(priv, sband, sta, priv_sta, oper_chan_type);
}

static void *
minstrel_ht_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct ieee80211_supported_band *sband;
	struct minstrel_ht_sta_priv *msp;
	struct minstrel_priv *mp = priv;
	struct ieee80211_hw *hw = mp->hw;
	int max_rates = 0;
	int i;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = hw->wiphy->bands[i];
		if (sband && sband->n_bitrates > max_rates)
			max_rates = sband->n_bitrates;
	}

	msp = kzalloc(sizeof(struct minstrel_ht_sta), gfp);
	if (!msp)
		return NULL;

	msp->ratelist = kzalloc(sizeof(struct minstrel_rate) * max_rates, gfp);
	if (!msp->ratelist)
		goto error;

	msp->sample_table = kmalloc(SAMPLE_COLUMNS * max_rates, gfp);
	if (!msp->sample_table)
		goto error1;

	return msp;

error1:
	kfree(msp->ratelist);
error:
	kfree(msp);
	return NULL;
}

static void
minstrel_ht_free_sta(void *priv, struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_ht_sta_priv *msp = priv_sta;

	kfree(msp->sample_table);
	kfree(msp->ratelist);
	kfree(msp);
}

static void *
minstrel_ht_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return mac80211_minstrel.alloc(hw, debugfsdir);
}

static void
minstrel_ht_free(void *priv)
{
	mac80211_minstrel.free(priv);
}

static struct rate_control_ops mac80211_minstrel_ht = {
	.name = "minstrel_ht",
	.tx_status = minstrel_ht_tx_status,
	.get_rate = minstrel_ht_get_rate,
	.rate_init = minstrel_ht_rate_init,
	.rate_update = minstrel_ht_rate_update,
	.alloc_sta = minstrel_ht_alloc_sta,
	.free_sta = minstrel_ht_free_sta,
	.alloc = minstrel_ht_alloc,
	.free = minstrel_ht_free,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = minstrel_ht_add_sta_debugfs,
	.remove_sta_debugfs = minstrel_ht_remove_sta_debugfs,
#endif
};


static void
init_sample_table(void)
{
	int col, i, new_idx;
	u8 rnd[MCS_GROUP_RATES];

	memset(sample_table, 0xff, sizeof(sample_table));
	for (col = 0; col < SAMPLE_COLUMNS; col++) {
		for (i = 0; i < MCS_GROUP_RATES; i++) {
			get_random_bytes(rnd, sizeof(rnd));
			new_idx = (i + rnd[i]) % MCS_GROUP_RATES;

			while (sample_table[col][new_idx] != 0xff)
				new_idx = (new_idx + 1) % MCS_GROUP_RATES;

			sample_table[col][new_idx] = i;
		}
	}
}

int __init
rc80211_minstrel_ht_init(void)
{
	init_sample_table();
	return ieee80211_rate_control_register(&mac80211_minstrel_ht);
}

void
rc80211_minstrel_ht_exit(void)
{
	ieee80211_rate_control_unregister(&mac80211_minstrel_ht);
}
