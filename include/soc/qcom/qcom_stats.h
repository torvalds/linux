/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_STATS_H__
#define __QCOM_STATS_H__

struct ddr_stats_ss_vote_info {
	u32 ab; /* vote_x */
	u32 ib; /* vote_y */
};

#if IS_ENABLED(CONFIG_QCOM_STATS)

int ddr_stats_get_ss_count(void);
int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info);
#else

static inline int ddr_stats_get_ss_count(void)
{ return -ENODEV; }
static inline int ddr_stats_get_ss_vote_info(int ss_count,
					     struct ddr_stats_ss_vote_info *vote_info)
{ return -ENODEV; }

#endif
#endif /*__QCOM_STATS_H__ */
