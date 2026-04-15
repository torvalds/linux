/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_COMMON_H__
#define __QCOM_SND_COMMON_H__

#include <dt-bindings/sound/qcom,q6afe.h>
#include <sound/soc.h>

#define LPASS_MAX_PORT			(SENARY_MI2S_TX + 1)

int qcom_snd_parse_of(struct snd_soc_card *card);
int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup);
int qcom_snd_dp_jack_setup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_soc_jack *dp_jack, int id);


#endif
