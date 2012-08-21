/*
 * tegra_pcm.h - Definitions for Tegra PCM driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __TEGRA_PCM_H__
#define __TEGRA_PCM_H__

#include <mach/dma.h>

struct tegra_pcm_dma_params {
	unsigned long addr;
	unsigned long wrap;
	unsigned long width;
	unsigned long req_sel;
};

#if defined(CONFIG_TEGRA_SYSTEM_DMA)
struct tegra_runtime_data {
	struct snd_pcm_substream *substream;
	spinlock_t lock;
	int running;
	int dma_pos;
	int dma_pos_end;
	int period_index;
	int dma_req_idx;
	struct tegra_dma_req dma_req[2];
	struct tegra_dma_channel *dma_chan;
};
#endif

int tegra_pcm_platform_register(struct device *dev);
void tegra_pcm_platform_unregister(struct device *dev);

#endif
