/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * tegra_isomgr_bw.h - Definitions for ADMA bandwidth calculation
 *
 */

#ifndef __TEGRA_ISOMGR_BW_H__
#define __TEGRA_ISOMGR_BW_H__

/* Playback and Capture streams */
#define STREAM_TYPE 2

struct tegra_adma_isomgr {
	/* Protect pcm devices bandwidth */
	struct mutex mutex;
	/* interconnect path handle */
	struct icc_path *icc_path_handle;
	u32 *bw_per_dev[STREAM_TYPE];
	u32 current_bandwidth;
	u32 max_pcm_device;
	u32 max_bw;
};

int tegra_isomgr_adma_register(struct device *dev);
void tegra_isomgr_adma_unregister(struct device *dev);
int tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai, bool is_running);

#endif
