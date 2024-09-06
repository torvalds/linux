/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_STATS_H__
#define __QCOM_STATS_H__

struct ddr_freq_residency {
	uint32_t freq;
	uint64_t residency;
};

struct ddr_stats_ss_vote_info {
	u32 ab; /* vote_x */
	u32 ib; /* vote_y */
};

struct qcom_stats_cx_vote_info {
	u8 level; /* CX LEVEL */
};

#if IS_ENABLED(CONFIG_QCOM_STATS)

int ddr_stats_get_ss_count(void);
int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info);

int qcom_stats_ddr_freqsync_msg(void);
int ddr_stats_get_freq_count(void);
int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data);

bool has_system_slept(void);
bool has_subsystem_slept(void);
bool current_subsystem_sleep(void);
void subsystem_sleep_debug_enable(bool enable);

int cx_stats_get_ss_vote_info(int ss_count,
			       struct qcom_stats_cx_vote_info *vote_info);
uint64_t get_aosd_sleep_exit_time(void);

#else

static inline int ddr_stats_get_ss_count(void)
{ return -ENODEV; }
static inline int ddr_stats_get_ss_vote_info(int ss_count,
					     struct ddr_stats_ss_vote_info *vote_info)
{ return -ENODEV; }
static inline int qcom_stats_ddr_freqsync_msg(void)
{ return -ENODEV; }
static inline int ddr_stats_get_freq_count(void)
{ return -ENODEV; }
int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data)
{ return -ENODEV; }

bool has_system_slept(void)
{ return false; }
bool has_subsystem_slept(void)
{ return false; }
bool current_subsystem_sleep(void)
{ return false; }
void subsystem_sleep_debug_enable(bool enable)
{ return; }

static inline int cx_stats_get_ss_vote_info(int ss_count,
			       struct qcom_stats_cx_vote_info *vote_info)
{ return -ENODEV; }
uint64_t get_aosd_sleep_exit_time(void)
{ return -ENODEV; }

#endif
#endif /*__QCOM_STATS_H__ */
