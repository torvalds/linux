// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
//
// ADMA bandwidth calculation

#include <linux/interconnect.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "tegra_isomgr_bw.h"
#include "tegra210_admaif.h"

/* Max possible rate is 192KHz x 16channel x 4bytes */
#define MAX_BW_PER_DEV 12288

int tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai, bool is_running)
{
	struct device *dev = dai->dev;
	struct tegra_admaif *admaif = snd_soc_dai_get_drvdata(dai);
	struct tegra_adma_isomgr *adma_isomgr = admaif->adma_isomgr;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm *pcm = substream->pcm;
	u32 type = substream->stream, bandwidth = 0;
	int sample_bytes;

	if (!adma_isomgr)
		return 0;

	if (!runtime || !pcm)
		return -EINVAL;

	if (pcm->device >= adma_isomgr->max_pcm_device) {
		dev_err(dev, "%s: PCM device number %d is greater than %d\n", __func__,
			pcm->device, adma_isomgr->max_pcm_device);
		return -EINVAL;
	}

	/*
	 * No action if  stream is running and bandwidth is already set or
	 * stream is not running and bandwidth is already reset
	 */
	if ((adma_isomgr->bw_per_dev[type][pcm->device] && is_running) ||
	    (!adma_isomgr->bw_per_dev[type][pcm->device] && !is_running))
		return 0;

	if (is_running) {
		sample_bytes = snd_pcm_format_width(runtime->format) / 8;
		if (sample_bytes < 0)
			return sample_bytes;

		/* KB/s kilo bytes per sec */
		bandwidth = runtime->channels * (runtime->rate / 1000) *
				sample_bytes;
	}

	mutex_lock(&adma_isomgr->mutex);

	if (is_running) {
		if (bandwidth + adma_isomgr->current_bandwidth > adma_isomgr->max_bw)
			bandwidth = adma_isomgr->max_bw - adma_isomgr->current_bandwidth;

		adma_isomgr->current_bandwidth += bandwidth;
	} else {
		adma_isomgr->current_bandwidth -= adma_isomgr->bw_per_dev[type][pcm->device];
	}

	mutex_unlock(&adma_isomgr->mutex);

	adma_isomgr->bw_per_dev[type][pcm->device] = bandwidth;

	dev_dbg(dev, "Setting up bandwidth to %d KBps\n", adma_isomgr->current_bandwidth);

	return icc_set_bw(adma_isomgr->icc_path_handle,
			  adma_isomgr->current_bandwidth, adma_isomgr->max_bw);
}

int tegra_isomgr_adma_register(struct device *dev)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);
	struct tegra_adma_isomgr *adma_isomgr;
	int i;

	adma_isomgr = devm_kzalloc(dev, sizeof(struct tegra_adma_isomgr), GFP_KERNEL);
	if (!adma_isomgr)
		return -ENOMEM;

	adma_isomgr->icc_path_handle = devm_of_icc_get(dev, "write");
	if (IS_ERR(adma_isomgr->icc_path_handle))
		return dev_err_probe(dev, PTR_ERR(adma_isomgr->icc_path_handle),
				"failed to acquire interconnect path\n");

	/* Either INTERCONNECT config OR interconnect property is not defined */
	if (!adma_isomgr->icc_path_handle) {
		devm_kfree(dev, adma_isomgr);
		return 0;
	}

	adma_isomgr->max_pcm_device = admaif->soc_data->num_ch;
	adma_isomgr->max_bw = STREAM_TYPE * MAX_BW_PER_DEV * adma_isomgr->max_pcm_device;

	for (i = 0; i < STREAM_TYPE; i++) {
		adma_isomgr->bw_per_dev[i] = devm_kzalloc(dev, adma_isomgr->max_pcm_device *
							  sizeof(u32), GFP_KERNEL);
		if (!adma_isomgr->bw_per_dev[i])
			return -ENOMEM;
	}

	adma_isomgr->current_bandwidth = 0;
	mutex_init(&adma_isomgr->mutex);
	admaif->adma_isomgr = adma_isomgr;

	return 0;
}

void tegra_isomgr_adma_unregister(struct device *dev)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);

	if (!admaif->adma_isomgr)
		return;

	mutex_destroy(&admaif->adma_isomgr->mutex);
}

MODULE_AUTHOR("Mohan Kumar <mkumard@nvidia.com>");
MODULE_DESCRIPTION("Tegra ADMA Bandwidth Request driver");
MODULE_LICENSE("GPL");
