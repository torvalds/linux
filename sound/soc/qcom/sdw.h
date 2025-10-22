/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_SDW_H__
#define __QCOM_SND_SDW_H__

#include <linux/soundwire/sdw.h>

int qcom_snd_sdw_startup(struct snd_pcm_substream *substream);
void qcom_snd_sdw_shutdown(struct snd_pcm_substream *substream);
int qcom_snd_sdw_prepare(struct snd_pcm_substream *substream,
			 bool *stream_prepared);
struct sdw_stream_runtime *qcom_snd_sdw_get_stream(struct snd_pcm_substream *stream);
int qcom_snd_sdw_hw_free(struct snd_pcm_substream *substream,
			 bool *stream_prepared);
#endif
