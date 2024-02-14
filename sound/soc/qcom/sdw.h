/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_SDW_H__
#define __QCOM_SND_SDW_H__

#include <linux/soundwire/sdw.h>

int qcom_snd_sdw_prepare(struct snd_pcm_substream *substream,
			 struct sdw_stream_runtime *runtime,
			 bool *stream_prepared);
int qcom_snd_sdw_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct sdw_stream_runtime **psruntime);
int qcom_snd_sdw_hw_free(struct snd_pcm_substream *substream,
			 struct sdw_stream_runtime *sruntime,
			 bool *stream_prepared);
#endif
