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
#define MINSTREL_SCALE  16
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
	return (new * (EWMA_DIV - weight) + old * weight) / EWMA_DIV;
}

struct minstrel_rate_stats {
	/* current / last sampling period attempts/success counters */
	unsigned int attempts, last_attempts;
	unsigned int success, last_success;

	/* total attempts/success counters */
	u64 att_hist, succ_hist;

	/* current throughput */
	unsigned int cur_tp;

	/* packet delivery probabilities */
	unsigned int cur_prob, probability;

	/* maximum retry counts */
	unsigned int retry_count;
	unsigned int retry_count_rtscts;

	u8 sample_skipped;
	bool retry_updated;
};

struct minstrel_rate {
	int bitrate;
	int rix;

	unsigned int perfect_tx_time;
	unsigned int ack_time;

	int sample_limit;
	unsigned int retry_count_cts;
	unsigned int adjusted_retry_count;

	struct minstrel_rate_stats stats;
};

struct minstrel_sta_info {
	struct ieee80211_sta *sta;

	unsigned long stats_update;
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

extern const struct rate_control_ops mac80211_minstrel;
void minstrel_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);
void minstrel_remove_sta_debugfs(void *priv, void *priv_sta);

/* debugfs */
int minstrel_stats_open(struct inode *inode, struct file *file);
ssize_t minstrel_stats_read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
int minstrel_stats_release(struct inode *inode, struct file *file);

#endif
