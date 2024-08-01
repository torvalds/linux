/* SPDX-License-Identifier: GPL-2.0-only
 *  Copyright (c) 2020 Intel Corporation
 */

/*
 *  sof_sdw_common.h - prototypes for common helpers
 */

#ifndef SND_SOC_SOF_SDW_COMMON_H
#define SND_SOC_SOF_SDW_COMMON_H

#include <linux/bits.h>
#include <linux/types.h>
#include <sound/soc.h>
#include <sound/soc_sdw_utils.h>
#include "sof_hdmi_common.h"

#define MAX_HDMI_NUM 4
#define SOC_SDW_UNUSED_DAI_ID -1
#define SOC_SDW_JACK_OUT_DAI_ID 0
#define SOC_SDW_JACK_IN_DAI_ID 1
#define SOC_SDW_AMP_OUT_DAI_ID 2
#define SOC_SDW_AMP_IN_DAI_ID 3
#define SOC_SDW_DMIC_DAI_ID 4
#define SOC_SDW_MAX_CPU_DAIS 16
#define SOC_SDW_INTEL_BIDIR_PDI_BASE 2

#define SDW_MAX_LINKS		4

/* 8 combinations with 4 links + unused group 0 */
#define SDW_MAX_GROUPS 9

enum {
	SOF_PRE_TGL_HDMI_COUNT = 3,
	SOF_TGL_HDMI_COUNT = 4,
};

enum {
	SOF_I2S_SSP0 = BIT(0),
	SOF_I2S_SSP1 = BIT(1),
	SOF_I2S_SSP2 = BIT(2),
	SOF_I2S_SSP3 = BIT(3),
	SOF_I2S_SSP4 = BIT(4),
	SOF_I2S_SSP5 = BIT(5),
};

/* Deprecated and no longer supported by the code */
#define SOC_SDW_FOUR_SPK		BIT(4)
#define SOF_SDW_TGL_HDMI		BIT(5)
#define SOC_SDW_PCH_DMIC		BIT(6)
#define SOF_SSP_PORT(x)		(((x) & GENMASK(5, 0)) << 7)
#define SOF_SSP_GET_PORT(quirk)	(((quirk) >> 7) & GENMASK(5, 0))
/* Deprecated and no longer supported by the code */
#define SOC_SDW_NO_AGGREGATION		BIT(14)
/* If a CODEC has an optional speaker output, this quirk will enable it */
#define SOC_SDW_CODEC_SPKR		BIT(15)
/*
 * If the CODEC has additional devices attached directly to it.
 *
 * For the cs42l43:
 *   - 0 - No speaker output
 *   - SOC_SDW_CODEC_SPKR - CODEC internal speaker
 *   - SOC_SDW_SIDECAR_AMPS - 2x Sidecar amplifiers + CODEC internal speaker
 *   - SOC_SDW_CODEC_SPKR | SOC_SDW_SIDECAR_AMPS - Not currently supported
 */
#define SOC_SDW_SIDECAR_AMPS		BIT(16)

/* BT audio offload: reserve 3 bits for future */
#define SOF_BT_OFFLOAD_SSP_SHIFT	15
#define SOF_BT_OFFLOAD_SSP_MASK	(GENMASK(17, 15))
#define SOF_BT_OFFLOAD_SSP(quirk)	\
	(((quirk) << SOF_BT_OFFLOAD_SSP_SHIFT) & SOF_BT_OFFLOAD_SSP_MASK)
#define SOF_SSP_BT_OFFLOAD_PRESENT	BIT(18)

#define SOC_SDW_DAI_TYPE_JACK		0
#define SOC_SDW_DAI_TYPE_AMP		1
#define SOC_SDW_DAI_TYPE_MIC		2

struct intel_mc_ctx {
	struct sof_hdmi_private hdmi;
	/* To store SDW Pin index for each SoundWire link */
	unsigned int sdw_pin_index[SDW_MAX_LINKS];
};

extern unsigned long sof_sdw_quirk;

/* generic HDMI support */
int sof_sdw_hdmi_init(struct snd_soc_pcm_runtime *rtd);

int sof_sdw_hdmi_card_late_probe(struct snd_soc_card *card);

/* RT1308 I2S support */
extern const struct snd_soc_ops soc_sdw_rt1308_i2s_ops;

/* generic amp support */
int asoc_sdw_rt_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback);
int asoc_sdw_rt_amp_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);

/* MAXIM codec support */
int asoc_sdw_maxim_init(struct snd_soc_card *card,
			struct snd_soc_dai_link *dai_links,
			struct asoc_sdw_codec_info *info,
			bool playback);

/* CS42L43 support */
int asoc_sdw_cs42l43_spk_init(struct snd_soc_card *card,
			      struct snd_soc_dai_link *dai_links,
			      struct asoc_sdw_codec_info *info,
			      bool playback);

/* CS AMP support */
int asoc_sdw_bridge_cs35l56_count_sidecar(struct snd_soc_card *card,
					  int *num_dais, int *num_devs);
int asoc_sdw_bridge_cs35l56_add_sidecar(struct snd_soc_card *card,
					struct snd_soc_dai_link **dai_links,
					struct snd_soc_codec_conf **codec_conf);
int asoc_sdw_bridge_cs35l56_spk_init(struct snd_soc_card *card,
				     struct snd_soc_dai_link *dai_links,
				     struct asoc_sdw_codec_info *info,
				     bool playback);

int asoc_sdw_cs_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback);

/* dai_link init callbacks */

int asoc_sdw_cs42l42_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_hs_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_maxim_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt_amp_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);

#endif
