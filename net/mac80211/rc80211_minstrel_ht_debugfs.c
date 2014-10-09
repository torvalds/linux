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
#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "rc80211_minstrel.h"
#include "rc80211_minstrel_ht.h"

static char *
minstrel_ht_stats_dump(struct minstrel_ht_sta *mi, int i, char *p)
{
	unsigned int max_mcs = MINSTREL_MAX_STREAMS * MINSTREL_STREAM_GROUPS;
	const struct mcs_group *mg;
	unsigned int j, tp, prob, eprob;
	char htmode = '2';
	char gimode = 'L';

	if (!mi->groups[i].supported)
		return p;

	mg = &minstrel_mcs_groups[i];
	if (mg->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		htmode = '4';
	if (mg->flags & IEEE80211_TX_RC_SHORT_GI)
		gimode = 'S';

	for (j = 0; j < MCS_GROUP_RATES; j++) {
		struct minstrel_rate_stats *mr = &mi->groups[i].rates[j];
		static const int bitrates[4] = { 10, 20, 55, 110 };
		int idx = i * MCS_GROUP_RATES + j;

		if (!(mi->groups[i].supported & BIT(j)))
			continue;

		if (i == max_mcs)
			p += sprintf(p, "CCK/%cP   ", j < 4 ? 'L' : 'S');
		else
			p += sprintf(p, "HT%c0/%cGI ", htmode, gimode);

		*(p++) = (idx == mi->max_tp_rate[0]) ? 'A' : ' ';
		*(p++) = (idx == mi->max_tp_rate[1]) ? 'B' : ' ';
		*(p++) = (idx == mi->max_tp_rate[2]) ? 'C' : ' ';
		*(p++) = (idx == mi->max_tp_rate[3]) ? 'D' : ' ';
		*(p++) = (idx == mi->max_prob_rate) ? 'P' : ' ';

		if (i == max_mcs) {
			int r = bitrates[j % 4];
			p += sprintf(p, " %2u.%1uM", r / 10, r % 10);
		} else {
			p += sprintf(p, " MCS%-2u", (mg->streams - 1) * 8 + j);
		}

		tp = mr->cur_tp / 10;
		prob = MINSTREL_TRUNC(mr->cur_prob * 1000);
		eprob = MINSTREL_TRUNC(mr->probability * 1000);

		p += sprintf(p, "      %6u.%1u   %6u.%1u    %6u.%1u    "
				"%3u            %3u(%3u)  %8llu    %8llu\n",
				tp / 10, tp % 10,
				eprob / 10, eprob % 10,
				prob / 10, prob % 10,
				mr->retry_count,
				mr->last_success,
				mr->last_attempts,
				(unsigned long long)mr->succ_hist,
				(unsigned long long)mr->att_hist);
	}

	return p;
}

static int
minstrel_ht_stats_open(struct inode *inode, struct file *file)
{
	struct minstrel_ht_sta_priv *msp = inode->i_private;
	struct minstrel_ht_sta *mi = &msp->ht;
	struct minstrel_debugfs_info *ms;
	unsigned int i;
	unsigned int max_mcs = MINSTREL_MAX_STREAMS * MINSTREL_STREAM_GROUPS;
	char *p;
	int ret;

	if (!msp->is_ht) {
		inode->i_private = &msp->legacy;
		ret = minstrel_stats_open(inode, file);
		inode->i_private = msp;
		return ret;
	}

	ms = kmalloc(sizeof(*ms) + 8192, GFP_KERNEL);
	if (!ms)
		return -ENOMEM;

	file->private_data = ms;
	p = ms->buf;
	p += sprintf(p, "type           rate     throughput  ewma prob   "
		     "this prob  retry   this succ/attempt   success    attempts\n");

	p = minstrel_ht_stats_dump(mi, max_mcs, p);
	for (i = 0; i < max_mcs; i++)
		p = minstrel_ht_stats_dump(mi, i, p);

	p += sprintf(p, "\nTotal packet count::    ideal %d      "
			"lookaround %d\n",
			max(0, (int) mi->total_packets - (int) mi->sample_packets),
			mi->sample_packets);
	p += sprintf(p, "Average A-MPDU length: %d.%d\n",
		MINSTREL_TRUNC(mi->avg_ampdu_len),
		MINSTREL_TRUNC(mi->avg_ampdu_len * 10) % 10);
	ms->len = p - ms->buf;

	return nonseekable_open(inode, file);
}

static const struct file_operations minstrel_ht_stat_fops = {
	.owner = THIS_MODULE,
	.open = minstrel_ht_stats_open,
	.read = minstrel_stats_read,
	.release = minstrel_stats_release,
	.llseek = no_llseek,
};

void
minstrel_ht_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir)
{
	struct minstrel_ht_sta_priv *msp = priv_sta;

	msp->dbg_stats = debugfs_create_file("rc_stats", S_IRUGO, dir, msp,
			&minstrel_ht_stat_fops);
}

void
minstrel_ht_remove_sta_debugfs(void *priv, void *priv_sta)
{
	struct minstrel_ht_sta_priv *msp = priv_sta;

	debugfs_remove(msp->dbg_stats);
}
