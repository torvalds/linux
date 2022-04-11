/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Felix Fietkau <nbd@openwrt.org>
 */

#ifndef __RC_MINSTREL_HT_H
#define __RC_MINSTREL_HT_H

/*
 * The number of streams can be changed to 2 to reduce code
 * size and memory footprint.
 */
#define MINSTREL_MAX_STREAMS		4
#define MINSTREL_HT_STREAM_GROUPS	4 /* BW(=2) * SGI(=2) */
#define MINSTREL_VHT_STREAM_GROUPS	6 /* BW(=3) * SGI(=2) */

#define MINSTREL_HT_GROUPS_NB	(MINSTREL_MAX_STREAMS *		\
				 MINSTREL_HT_STREAM_GROUPS)
#define MINSTREL_VHT_GROUPS_NB	(MINSTREL_MAX_STREAMS *		\
				 MINSTREL_VHT_STREAM_GROUPS)
#define MINSTREL_CCK_GROUPS_NB	1
#define MINSTREL_GROUPS_NB	(MINSTREL_HT_GROUPS_NB +	\
				 MINSTREL_VHT_GROUPS_NB +	\
				 MINSTREL_CCK_GROUPS_NB)

#define MINSTREL_HT_GROUP_0	0
#define MINSTREL_CCK_GROUP	(MINSTREL_HT_GROUP_0 + MINSTREL_HT_GROUPS_NB)
#define MINSTREL_VHT_GROUP_0	(MINSTREL_CCK_GROUP + 1)

#define MCS_GROUP_RATES		10

struct mcs_group {
	u16 flags;
	u8 streams;
	u8 shift;
	u8 bw;
	u16 duration[MCS_GROUP_RATES];
};

extern const struct mcs_group minstrel_mcs_groups[];

struct minstrel_mcs_group_data {
	u8 index;
	u8 column;

	/* sorted rate set within a MCS group*/
	u16 max_group_tp_rate[MAX_THR_RATES];
	u16 max_group_prob_rate;

	/* MCS rate statistics */
	struct minstrel_rate_stats rates[MCS_GROUP_RATES];
};

enum minstrel_sample_mode {
	MINSTREL_SAMPLE_IDLE,
	MINSTREL_SAMPLE_ACTIVE,
	MINSTREL_SAMPLE_PENDING,
};

struct minstrel_ht_sta {
	struct ieee80211_sta *sta;

	/* ampdu length (average, per sampling interval) */
	unsigned int ampdu_len;
	unsigned int ampdu_packets;

	/* ampdu length (EWMA) */
	unsigned int avg_ampdu_len;

	/* overall sorted rate set */
	u16 max_tp_rate[MAX_THR_RATES];
	u16 max_prob_rate;

	/* time of last status update */
	unsigned long last_stats_update;

	/* overhead time in usec for each frame */
	unsigned int overhead;
	unsigned int overhead_rtscts;

	unsigned int total_packets_last;
	unsigned int total_packets_cur;
	unsigned int total_packets;
	unsigned int sample_packets;

	/* tx flags to add for frames for this sta */
	u32 tx_flags;

	u8 sample_wait;
	u8 sample_tries;
	u8 sample_count;
	u8 sample_slow;

	enum minstrel_sample_mode sample_mode;
	u16 sample_rate;

	/* current MCS group to be sampled */
	u8 sample_group;

	u8 cck_supported;
	u8 cck_supported_short;

	/* Bitfield of supported MCS rates of all groups */
	u16 supported[MINSTREL_GROUPS_NB];

	/* MCS rate group info and statistics */
	struct minstrel_mcs_group_data groups[MINSTREL_GROUPS_NB];
};

struct minstrel_ht_sta_priv {
	union {
		struct minstrel_ht_sta ht;
		struct minstrel_sta_info legacy;
	};
	void *ratelist;
	void *sample_table;
	bool is_ht;
};

void minstrel_ht_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);
int minstrel_ht_get_tp_avg(struct minstrel_ht_sta *mi, int group, int rate,
			   int prob_ewma);

#endif
