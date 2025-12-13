/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file incorporates work covered by the following copyright notice:
 * Copyright (c) 2020 Intel Corporation
 * Copyright(c) 2024 Advanced Micro Devices, Inc.
 *
 */

#ifndef SOC_SDW_UTILS_H
#define SOC_SDW_UTILS_H

#include <sound/soc.h>
#include <sound/soc-acpi.h>

#define SOC_SDW_MAX_DAI_NUM             8
#define SOC_SDW_MAX_NO_PROPS		2
#define SOC_SDW_JACK_JDSRC(quirk)	((quirk) & GENMASK(3, 0))

/* If a CODEC has an optional speaker output, this quirk will enable it */
#define SOC_SDW_CODEC_SPKR			BIT(15)
/*
 * If the CODEC has additional devices attached directly to it.
 *
 * For the cs42l43:
 *   - 0 - No speaker output
 *   - SOC_SDW_CODEC_SPKR - CODEC internal speaker
 *   - SOC_SDW_SIDECAR_AMPS - 2x Sidecar amplifiers + CODEC internal speaker
 *   - SOC_SDW_CODEC_SPKR | SOF_SIDECAR_AMPS - Not currently supported
 */
#define SOC_SDW_SIDECAR_AMPS		BIT(16)
#define SOC_SDW_CODEC_MIC		BIT(17)

#define SOC_SDW_UNUSED_DAI_ID		-1
#define SOC_SDW_JACK_OUT_DAI_ID		0
#define SOC_SDW_JACK_IN_DAI_ID		1
#define SOC_SDW_AMP_OUT_DAI_ID		2
#define SOC_SDW_AMP_IN_DAI_ID		3
#define SOC_SDW_DMIC_DAI_ID		4

#define SOC_SDW_DAI_TYPE_JACK		0
#define SOC_SDW_DAI_TYPE_AMP		1
#define SOC_SDW_DAI_TYPE_MIC		2

struct asoc_sdw_codec_info;

struct asoc_sdw_dai_info {
	const bool direction[2]; /* playback & capture support */
	const char *dai_name;
	const char *component_name;
	const int dai_type;
	const int dailink[2]; /* dailink id for each direction */
	const struct snd_kcontrol_new *controls;
	const int num_controls;
	const struct snd_soc_dapm_widget *widgets;
	const int num_widgets;
	int  (*init)(struct snd_soc_card *card,
		     struct snd_soc_dai_link *dai_links,
		     struct asoc_sdw_codec_info *info,
		     bool playback);
	int (*exit)(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);
	int (*rtd_init)(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
	bool rtd_init_done; /* Indicate that the rtd_init callback is done */
	unsigned long quirk;
	bool quirk_exclude;
};

struct asoc_sdw_codec_info {
	const int part_id;
	const int version_id;
	const char *codec_name;
	int amp_num;
	const u8 acpi_id[ACPI_ID_LEN];
	const bool ignore_internal_dmic;
	const struct snd_soc_ops *ops;
	struct asoc_sdw_dai_info dais[SOC_SDW_MAX_DAI_NUM];
	const int dai_num;

	int (*codec_card_late_probe)(struct snd_soc_card *card);

	int  (*count_sidecar)(struct snd_soc_card *card,
			      int *num_dais, int *num_devs);
	int  (*add_sidecar)(struct snd_soc_card *card,
			    struct snd_soc_dai_link **dai_links,
			    struct snd_soc_codec_conf **codec_conf);
};

struct asoc_sdw_mc_private {
	struct snd_soc_card card;
	struct snd_soc_jack sdw_headset;
	struct device *headset_codec_dev; /* only one headset per card */
	struct device *amp_dev1, *amp_dev2;
	bool append_dai_type;
	bool ignore_internal_dmic;
	void *private;
	unsigned long mc_quirk;
	int codec_info_list_count;
};

struct asoc_sdw_endpoint {
	struct list_head list;

	u32 link_mask;
	const char *codec_name;
	const char *name_prefix;
	bool include_sidecar;

	struct asoc_sdw_codec_info *codec_info;
	const struct asoc_sdw_dai_info *dai_info;
};

struct asoc_sdw_dailink {
	bool initialised;

	u8 group_id;
	u32 link_mask[SNDRV_PCM_STREAM_LAST + 1];
	int num_devs[SNDRV_PCM_STREAM_LAST + 1];
	struct list_head endpoints;
};

extern struct asoc_sdw_codec_info codec_info_list[];
int asoc_sdw_get_codec_info_list_count(void);

int asoc_sdw_startup(struct snd_pcm_substream *substream);
int asoc_sdw_prepare(struct snd_pcm_substream *substream);
int asoc_sdw_prepare(struct snd_pcm_substream *substream);
int asoc_sdw_trigger(struct snd_pcm_substream *substream, int cmd);
int asoc_sdw_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params);
int asoc_sdw_hw_free(struct snd_pcm_substream *substream);
void asoc_sdw_shutdown(struct snd_pcm_substream *substream);

const char *asoc_sdw_get_codec_name(struct device *dev,
				    const struct asoc_sdw_codec_info *codec_info,
				    const struct snd_soc_acpi_link_adr *adr_link,
				    int adr_index);

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_part(const u64 adr);

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_acpi(const u8 *acpi_id);

struct asoc_sdw_codec_info *asoc_sdw_find_codec_info_dai(const char *dai_name,
							 int *dai_index);

struct snd_soc_dai_link *asoc_sdw_mc_find_codec_dai_used(struct snd_soc_card *card,
							 const char *dai_name);

void asoc_sdw_mc_dailink_exit_loop(struct snd_soc_card *card);

int asoc_sdw_card_late_probe(struct snd_soc_card *card);

void asoc_sdw_init_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
			    int *be_id, char *name, int playback, int capture,
			    struct snd_soc_dai_link_component *cpus, int cpus_num,
			    struct snd_soc_dai_link_component *platform_component,
			    int num_platforms, struct snd_soc_dai_link_component *codecs,
			    int codecs_num, int no_pcm,
			    int (*init)(struct snd_soc_pcm_runtime *rtd),
			    const struct snd_soc_ops *ops);

int asoc_sdw_init_simple_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
				  int *be_id, char *name, int playback, int capture,
				  const char *cpu_dai_name, const char *platform_comp_name,
				  const char *codec_name, const char *codec_dai_name,
				  int no_pcm, int (*init)(struct snd_soc_pcm_runtime *rtd),
				  const struct snd_soc_ops *ops);

int asoc_sdw_count_sdw_endpoints(struct snd_soc_card *card, int *num_devs, int *num_ends);

struct asoc_sdw_dailink *asoc_sdw_find_dailink(struct asoc_sdw_dailink *dailinks,
					       const struct snd_soc_acpi_endpoint *new);

int asoc_sdw_parse_sdw_endpoints(struct snd_soc_card *card,
				 struct asoc_sdw_dailink *soc_dais,
				 struct asoc_sdw_endpoint *soc_ends,
				 int *num_devs);

int asoc_sdw_rtd_init(struct snd_soc_pcm_runtime *rtd);

/* DMIC support */
int asoc_sdw_dmic_init(struct snd_soc_pcm_runtime *rtd);

/* RT711 support */
int asoc_sdw_rt711_init(struct snd_soc_card *card,
			struct snd_soc_dai_link *dai_links,
			struct asoc_sdw_codec_info *info,
			bool playback);
int asoc_sdw_rt711_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);

/* RT711-SDCA support */
int asoc_sdw_rt_sdca_jack_init(struct snd_soc_card *card,
			       struct snd_soc_dai_link *dai_links,
			       struct asoc_sdw_codec_info *info,
			       bool playback);
int asoc_sdw_rt_sdca_jack_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);

/* RT1308 I2S support */
extern const struct snd_soc_ops soc_sdw_rt1308_i2s_ops;

/* generic amp support */
int asoc_sdw_rt_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback);
int asoc_sdw_rt_amp_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);

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
int asoc_sdw_cs_spk_feedback_rtd_init(struct snd_soc_pcm_runtime *rtd,
				      struct snd_soc_dai *dai);
int asoc_sdw_cs35l56_volume_limit(struct snd_soc_card *card, const char *name_prefix);

/* MAXIM codec support */
int asoc_sdw_maxim_init(struct snd_soc_card *card,
			struct snd_soc_dai_link *dai_links,
			struct asoc_sdw_codec_info *info,
			bool playback);

/* dai_link init callbacks */
int asoc_sdw_rt_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt_sdca_jack_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt_amp_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt700_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt711_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt_mf_sdca_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_rt5682_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l42_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_hs_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs42l43_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_cs_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_maxim_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
/* TI */
int asoc_sdw_ti_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback);
int asoc_sdw_ti_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai);
int asoc_sdw_ti_amp_initial_settings(struct snd_soc_card *card,
				     const char *name_prefix);

#endif
