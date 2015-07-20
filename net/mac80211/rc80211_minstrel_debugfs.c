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
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "rc80211_minstrel.h"

ssize_t
minstrel_stats_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	struct minstrel_debugfs_info *ms;

	ms = file->private_data;
	return simple_read_from_buffer(buf, len, ppos, ms->buf, ms->len);
}

int
minstrel_stats_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

int
minstrel_stats_open(struct inode *inode, struct file *file)
{
	struct minstrel_sta_info *mi = inode->i_private;
	struct minstrel_debugfs_info *ms;
	unsigned int i, tp_max, tp_avg, prob, eprob;
	char *p;

	ms = kmalloc(2048, GFP_KERNEL);
	if (!ms)
		return -ENOMEM;

	file->private_data = ms;
	p = ms->buf;
	p += sprintf(p, "\n");
	p += sprintf(p, "best   __________rate_________    ______"
			"statistics______    ________last_______    "
			"______sum-of________\n");
	p += sprintf(p, "rate  [name idx airtime max_tp]  [ ø(tp) ø(prob) "
			"sd(prob)]  [prob.|retry|suc|att]  "
			"[#success | #attempts]\n");

	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		struct minstrel_rate_stats *mrs = &mi->r[i].stats;

		*(p++) = (i == mi->max_tp_rate[0]) ? 'A' : ' ';
		*(p++) = (i == mi->max_tp_rate[1]) ? 'B' : ' ';
		*(p++) = (i == mi->max_tp_rate[2]) ? 'C' : ' ';
		*(p++) = (i == mi->max_tp_rate[3]) ? 'D' : ' ';
		*(p++) = (i == mi->max_prob_rate) ? 'P' : ' ';

		p += sprintf(p, " %3u%s ", mr->bitrate / 2,
				(mr->bitrate & 1 ? ".5" : "  "));
		p += sprintf(p, "%3u  ", i);
		p += sprintf(p, "%6u ", mr->perfect_tx_time);

		tp_max = minstrel_get_tp_avg(mr, MINSTREL_FRAC(100,100));
		tp_avg = minstrel_get_tp_avg(mr, mrs->prob_ewma);
		prob = MINSTREL_TRUNC(mrs->cur_prob * 1000);
		eprob = MINSTREL_TRUNC(mrs->prob_ewma * 1000);

		p += sprintf(p, "%4u.%1u   %4u.%1u   %3u.%1u    %3u.%1u"
				"     %3u.%1u %3u   %3u %-3u   "
				"%9llu   %-9llu\n",
				tp_max / 10, tp_max % 10,
				tp_avg / 10, tp_avg % 10,
				eprob / 10, eprob % 10,
				mrs->prob_ewmsd / 10, mrs->prob_ewmsd % 10,
				prob / 10, prob % 10,
				mrs->retry_count,
				mrs->last_success,
				mrs->last_attempts,
				(unsigned long long)mrs->succ_hist,
				(unsigned long long)mrs->att_hist);
	}
	p += sprintf(p, "\nTotal packet count::    ideal %d      "
			"lookaround %d\n\n",
			mi->total_packets - mi->sample_packets,
			mi->sample_packets);
	ms->len = p - ms->buf;

	WARN_ON(ms->len + sizeof(*ms) > 2048);

	return 0;
}

static const struct file_operations minstrel_stat_fops = {
	.owner = THIS_MODULE,
	.open = minstrel_stats_open,
	.read = minstrel_stats_read,
	.release = minstrel_stats_release,
	.llseek = default_llseek,
};

int
minstrel_stats_csv_open(struct inode *inode, struct file *file)
{
	struct minstrel_sta_info *mi = inode->i_private;
	struct minstrel_debugfs_info *ms;
	unsigned int i, tp_max, tp_avg, prob, eprob;
	char *p;

	ms = kmalloc(2048, GFP_KERNEL);
	if (!ms)
		return -ENOMEM;

	file->private_data = ms;
	p = ms->buf;

	for (i = 0; i < mi->n_rates; i++) {
		struct minstrel_rate *mr = &mi->r[i];
		struct minstrel_rate_stats *mrs = &mi->r[i].stats;

		p += sprintf(p, "%s" ,((i == mi->max_tp_rate[0]) ? "A" : ""));
		p += sprintf(p, "%s" ,((i == mi->max_tp_rate[1]) ? "B" : ""));
		p += sprintf(p, "%s" ,((i == mi->max_tp_rate[2]) ? "C" : ""));
		p += sprintf(p, "%s" ,((i == mi->max_tp_rate[3]) ? "D" : ""));
		p += sprintf(p, "%s" ,((i == mi->max_prob_rate) ? "P" : ""));

		p += sprintf(p, ",%u%s", mr->bitrate / 2,
				(mr->bitrate & 1 ? ".5," : ","));
		p += sprintf(p, "%u,", i);
		p += sprintf(p, "%u,",mr->perfect_tx_time);

		tp_max = minstrel_get_tp_avg(mr, MINSTREL_FRAC(100,100));
		tp_avg = minstrel_get_tp_avg(mr, mrs->prob_ewma);
		prob = MINSTREL_TRUNC(mrs->cur_prob * 1000);
		eprob = MINSTREL_TRUNC(mrs->prob_ewma * 1000);

		p += sprintf(p, "%u.%u,%u.%u,%u.%u,%u.%u,%u.%u,%u,%u,%u,"
				"%llu,%llu,%d,%d\n",
				tp_max / 10, tp_max % 10,
				tp_avg / 10, tp_avg % 10,
				eprob / 10, eprob % 10,
				mrs->prob_ewmsd / 10, mrs->prob_ewmsd % 10,
				prob / 10, prob % 10,
				mrs->retry_count,
				mrs->last_success,
				mrs->last_attempts,
				(unsigned long long)mrs->succ_hist,
				(unsigned long long)mrs->att_hist,
				mi->total_packets - mi->sample_packets,
				mi->sample_packets);

	}
	ms->len = p - ms->buf;

	WARN_ON(ms->len + sizeof(*ms) > 2048);

	return 0;
}

static const struct file_operations minstrel_stat_csv_fops = {
	.owner = THIS_MODULE,
	.open = minstrel_stats_csv_open,
	.read = minstrel_stats_read,
	.release = minstrel_stats_release,
	.llseek = default_llseek,
};

void
minstrel_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir)
{
	struct minstrel_sta_info *mi = priv_sta;

	mi->dbg_stats = debugfs_create_file("rc_stats", S_IRUGO, dir, mi,
			&minstrel_stat_fops);

	mi->dbg_stats_csv = debugfs_create_file("rc_stats_csv", S_IRUGO, dir,
			mi, &minstrel_stat_csv_fops);
}

void
minstrel_remove_sta_debugfs(void *priv, void *priv_sta)
{
	struct minstrel_sta_info *mi = priv_sta;

	debugfs_remove(mi->dbg_stats);

	debugfs_remove(mi->dbg_stats_csv);
}
