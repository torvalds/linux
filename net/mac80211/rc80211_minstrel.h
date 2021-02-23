/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 */

#ifndef __RC_MINSTREL_H
#define __RC_MINSTREL_H

#define EWMA_LEVEL	96	/* ewma weighting factor [/EWMA_DIV] */
#define EWMA_DIV	128
#define SAMPLE_COLUMNS	10	/* number of columns in sample table */

/* scaled fraction values */
#define MINSTREL_SCALE  12
#define MINSTREL_FRAC(val, div) (((val) << MINSTREL_SCALE) / div)
#define MINSTREL_TRUNC(val) ((val) >> MINSTREL_SCALE)

/* number of highest throughput rates to consider*/
#define MAX_THR_RATES 4

/*
 * Coefficients for moving average with noise filter (period=16),
 * scaled by 10 bits
 *
 * a1 = exp(-pi * sqrt(2) / period)
 * coeff2 = 2 * a1 * cos(sqrt(2) * 2 * pi / period)
 * coeff3 = -sqr(a1)
 * coeff1 = 1 - coeff2 - coeff3
 */
#define MINSTREL_AVG_COEFF1		(MINSTREL_FRAC(1, 1) - \
					 MINSTREL_AVG_COEFF2 - \
					 MINSTREL_AVG_COEFF3)
#define MINSTREL_AVG_COEFF2		0x00001499
#define MINSTREL_AVG_COEFF3		-0x0000092e

/*
 * Perform EWMA (Exponentially Weighted Moving Average) calculation
 */
static inline int
minstrel_ewma(int old, int new, int weight)
{
	int diff, incr;

	diff = new - old;
	incr = (EWMA_DIV - weight) * diff / EWMA_DIV;

	return old + incr;
}

static inline int minstrel_filter_avg_add(u16 *prev_1, u16 *prev_2, s32 in)
{
	s32 out_1 = *prev_1;
	s32 out_2 = *prev_2;
	s32 val;

	if (!in)
		in += 1;

	if (!out_1) {
		val = out_1 = in;
		goto out;
	}

	val = MINSTREL_AVG_COEFF1 * in;
	val += MINSTREL_AVG_COEFF2 * out_1;
	val += MINSTREL_AVG_COEFF3 * out_2;
	val >>= MINSTREL_SCALE;

	if (val > 1 << MINSTREL_SCALE)
		val = 1 << MINSTREL_SCALE;
	if (val < 0)
		val = 1;

out:
	*prev_2 = out_1;
	*prev_1 = val;

	return val;
}

struct minstrel_rate_stats {
	/* current / last sampling period attempts/success counters */
	u16 attempts, last_attempts;
	u16 success, last_success;

	/* total attempts/success counters */
	u32 att_hist, succ_hist;

	/* prob_avg - moving average of prob */
	u16 prob_avg;
	u16 prob_avg_1;

	/* maximum retry counts */
	u8 retry_count;
	u8 retry_count_rtscts;

	u8 sample_skipped;
	bool retry_updated;
};

struct minstrel_rate {
	int bitrate;

	s8 rix;
	u8 retry_count_cts;
	u8 adjusted_retry_count;

	unsigned int perfect_tx_time;
	unsigned int ack_time;

	int sample_limit;

	struct minstrel_rate_stats stats;
};

struct minstrel_sta_info {
	struct ieee80211_sta *sta;

	unsigned long last_stats_update;
	unsigned int sp_ack_dur;
	unsigned int rate_avg;

	unsigned int lowest_rix;

	u8 max_tp_rate[MAX_THR_RATES];
	u8 max_prob_rate;
	unsigned int total_packets;
	unsigned int sample_packets;

	unsigned int sample_row;
	unsigned int sample_column;

	int n_rates;
	struct minstrel_rate *r;
	bool prev_sample;

	/* sampling table */
	u8 *sample_table;
};

struct minstrel_priv {
	struct ieee80211_hw *hw;
	bool has_mrr;
	bool new_avg;
	u32 sample_switch;
	unsigned int cw_min;
	unsigned int cw_max;
	unsigned int max_retry;
	unsigned int segment_size;
	unsigned int update_interval;
	unsigned int lookaround_rate;
	unsigned int lookaround_rate_mrr;

	u8 cck_rates[4];

#ifdef CONFIG_MAC80211_DEBUGFS
	/*
	 * enable fixed rate processing per RC
	 *   - write static index to debugfs:ieee80211/phyX/rc/fixed_rate_idx
	 *   - write -1 to enable RC processing again
	 *   - setting will be applied on next update
	 */
	u32 fixed_rate_idx;
#endif
};

struct minstrel_debugfs_info {
	size_t len;
	char buf[];
};

extern const struct rate_control_ops mac80211_minstrel;
void minstrel_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);

/* Recalculate success probabilities and counters for a given rate using EWMA */
void minstrel_calc_rate_stats(struct minstrel_priv *mp,
			      struct minstrel_rate_stats *mrs);
int minstrel_get_tp_avg(struct minstrel_rate *mr, int prob_avg);

/* debugfs */
int minstrel_stats_open(struct inode *inode, struct file *file);
int minstrel_stats_csv_open(struct inode *inode, struct file *file);

#endif
