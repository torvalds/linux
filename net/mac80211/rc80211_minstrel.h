/*
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

/*
 * Perform EWMV (Exponentially Weighted Moving Variance) calculation
 */
static inline int
minstrel_ewmv(int old_ewmv, int cur_prob, int prob_ewma, int weight)
{
	int diff, incr;

	diff = cur_prob - prob_ewma;
	incr = (EWMA_DIV - weight) * diff / EWMA_DIV;
	return weight * (old_ewmv + MINSTREL_TRUNC(diff * incr)) / EWMA_DIV;
}

struct minstrel_rate_stats {
	/* current / last sampling period attempts/success counters */
	u16 attempts, last_attempts;
	u16 success, last_success;

	/* total attempts/success counters */
	u32 att_hist, succ_hist;

	/* statistis of packet delivery probability
	 *  prob_ewma - exponential weighted moving average of prob
	 *  prob_ewmsd - exp. weighted moving standard deviation of prob */
	u16 prob_ewma;
	u16 prob_ewmv;

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
	int sample_deferred;

	unsigned int sample_row;
	unsigned int sample_column;

	int n_rates;
	struct minstrel_rate *r;
	bool prev_sample;

	/* sampling table */
	u8 *sample_table;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *dbg_stats;
	struct dentry *dbg_stats_csv;
#endif
};

struct minstrel_priv {
	struct ieee80211_hw *hw;
	bool has_mrr;
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
	struct dentry *dbg_fixed_rate;
#endif
};

struct minstrel_debugfs_info {
	size_t len;
	char buf[];
};

/* Get EWMSD (Exponentially Weighted Moving Standard Deviation) * 10 */
static inline int
minstrel_get_ewmsd10(struct minstrel_rate_stats *mrs)
{
	unsigned int ewmv = mrs->prob_ewmv;
	return int_sqrt(MINSTREL_TRUNC(ewmv * 1000 * 1000));
}

extern const struct rate_control_ops mac80211_minstrel;
void minstrel_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);
void minstrel_remove_sta_debugfs(void *priv, void *priv_sta);

/* Recalculate success probabilities and counters for a given rate using EWMA */
void minstrel_calc_rate_stats(struct minstrel_rate_stats *mrs);
int minstrel_get_tp_avg(struct minstrel_rate *mr, int prob_ewma);

/* debugfs */
int minstrel_stats_open(struct inode *inode, struct file *file);
int minstrel_stats_csv_open(struct inode *inode, struct file *file);
ssize_t minstrel_stats_read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
int minstrel_stats_release(struct inode *inode, struct file *file);

#endif
