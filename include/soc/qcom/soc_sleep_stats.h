/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __SOC_SLEEP_STATS_H__
#define __SOC_SLEEP_STATS_H__

struct ddr_freq_residency {
	uint32_t freq;
	uint64_t residency;
};

struct ddr_stats_ss_vote_info {
	u32 ab; /* vote_x */
	u32 ib; /* vote_y */
};

struct stats_config {
	unsigned int offset_addr;
	unsigned int ddr_offset_addr;
	unsigned int num_records;
	bool appended_stats_avail;
};

struct appended_stats {
	u32 client_votes;
	u32 reserved[3];
};

#ifdef CONFIG_MSM_BOOT_TIME_MARKER
uint64_t get_aosd_sleep_exit_time(void);
#endif

#if IS_ENABLED(CONFIG_QCOM_SOC_SLEEP_STATS) && IS_ENABLED(CONFIG_MSM_QMP)
int ddr_stats_freq_sync_send_msg(void);
int ddr_stats_get_freq_count(void);
int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data);
int ddr_stats_get_ss_count(void);
int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info);
#else
static inline int ddr_stats_freq_sync_send_msg(void)
{ return -ENODEV; }

static inline int ddr_stats_get_freq_count(void)
{ return -ENODEV; }

static inline int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data)
{ return -ENODEV; }

static inline int ddr_stats_get_ss_count(void)
{ return -ENODEV; }

static inline int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info)
{ return -ENODEV; }
#endif
#endif /*__SOC_SLEEP_STATS_H__ */
