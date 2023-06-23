/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_COMMON_H__
#define __QCOM_SND_COMMON_H__

#include <sound/soc.h>
#include <linux/soundwire/sdw.h>

int qcom_snd_parse_of(struct snd_soc_card *card);
int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup);

#if IS_ENABLED(CONFIG_SOUNDWIRE)
int qcom_snd_sdw_prepare(struct snd_pcm_substream *substream,
			 struct sdw_stream_runtime *runtime,
			 bool *stream_prepared);
int qcom_snd_sdw_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct sdw_stream_runtime **psruntime);
int qcom_snd_sdw_hw_free(struct snd_pcm_substream *substream,
			 struct sdw_stream_runtime *sruntime,
			 bool *stream_prepared);
#else
static inline int qcom_snd_sdw_prepare(struct snd_pcm_substream *substream,
				       struct sdw_stream_runtime *runtime,
				       bool *stream_prepared)
{
	return -ENOTSUPP;
}

static inline int qcom_snd_sdw_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct sdw_stream_runtime **psruntime)
{
	return -ENOTSUPP;
}

static inline int qcom_snd_sdw_hw_free(struct snd_pcm_substream *substream,
				       struct sdw_stream_runtime *sruntime,
				       bool *stream_prepared)
{
	return -ENOTSUPP;
}
#endif
#endif
