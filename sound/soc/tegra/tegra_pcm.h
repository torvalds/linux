/* SPDX-License-Identifier: GPL-2.0-only */
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
 */

#ifndef __TEGRA_PCM_H__
#define __TEGRA_PCM_H__

#include <sound/dmaengine_pcm.h>
#include <sound/asound.h>

int tegra_pcm_construct(struct snd_soc_component *component,
			struct snd_soc_pcm_runtime *rtd);
int tegra_pcm_open(struct snd_soc_component *component,
		   struct snd_pcm_substream *substream);
int tegra_pcm_close(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream);
int tegra_pcm_hw_params(struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params);
snd_pcm_uframes_t tegra_pcm_pointer(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream);
int tegra_pcm_platform_register(struct device *dev);
int tegra_pcm_platform_register_with_chan_names(struct device *dev,
				struct snd_dmaengine_pcm_config *config,
				char *txdmachan, char *rxdmachan);
void tegra_pcm_platform_unregister(struct device *dev);

#endif
