/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_COMMON_H__
#define __QCOM_SND_COMMON_H__

#include <dt-bindings/sound/qcom,q6afe.h>
#include <sound/soc.h>

#define LPASS_MAX_PORT			(LPI_MI2S_TX_6 + 1)

struct qcom_snd_tdm_slot_cfg {
	unsigned int tx_mask;
	unsigned int rx_mask;
	unsigned int slots;
	unsigned int slot_width;
};

int qcom_snd_parse_of(struct snd_soc_card *card);
int qcom_snd_get_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd,
			       struct qcom_snd_tdm_slot_cfg *cpu_cfg,
			       struct qcom_snd_tdm_slot_cfg *codec_cfg);
int qcom_snd_apply_dai_tdm_slots_cfg(struct snd_soc_pcm_runtime *rtd,
				     const struct qcom_snd_tdm_slot_cfg *cpu_cfg,
				     const struct qcom_snd_tdm_slot_cfg *codec_cfg);
int qcom_snd_apply_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd);
int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup);
int qcom_snd_dp_jack_setup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_soc_jack *dp_jack, int id);


#endif
