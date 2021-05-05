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

#define MAX_NO_PROPS 2
#define MAX_HDMI_NUM 4
#define SDW_DMIC_DAI_ID 4
#define SDW_MAX_CPU_DAIS 16
#define SDW_INTEL_BIDIR_PDI_BASE 2

/* 8 combinations with 4 links + unused group 0 */
#define SDW_MAX_GROUPS 9

enum {
	SOF_RT711_JD_SRC_JD1 = 1,
	SOF_RT711_JD_SRC_JD2 = 2,
};

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

#define SOF_RT711_JDSRC(quirk)		((quirk) & GENMASK(1, 0))
#define SOF_SDW_FOUR_SPK		BIT(2)
#define SOF_SDW_TGL_HDMI		BIT(3)
#define SOF_SDW_PCH_DMIC		BIT(4)
#define SOF_SSP_PORT(x)		(((x) & GENMASK(5, 0)) << 5)
#define SOF_SSP_GET_PORT(quirk)	(((quirk) >> 5) & GENMASK(5, 0))
#define SOF_RT715_DAI_ID_FIX		BIT(11)
#define SOF_SDW_NO_AGGREGATION		BIT(12)

/* BT audio offload: reserve 3 bits for future */
#define SOF_BT_OFFLOAD_SSP_SHIFT	13
#define SOF_BT_OFFLOAD_SSP_MASK	(GENMASK(15, 13))
#define SOF_BT_OFFLOAD_SSP(quirk)	\
	(((quirk) << SOF_BT_OFFLOAD_SSP_SHIFT) & SOF_BT_OFFLOAD_SSP_MASK)
#define SOF_SSP_BT_OFFLOAD_PRESENT	BIT(16)

struct sof_sdw_codec_info {
	const int part_id;
	const int version_id;
	int amp_num;
	const u8 acpi_id[ACPI_ID_LEN];
	const bool direction[2]; // playback & capture support
	const bool ignore_pch_dmic;
	const char *dai_name;
	const struct snd_soc_ops *ops;

	int  (*init)(const struct snd_soc_acpi_link_adr *link,
		     struct snd_soc_dai_link *dai_links,
		     struct sof_sdw_codec_info *info,
		     bool playback);

	int (*exit)(struct device *dev, struct snd_soc_dai_link *dai_link);
	bool late_probe;
	int (*codec_card_late_probe)(struct snd_soc_card *card);
};

struct mc_private {
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
	bool idisp_codec;
	struct snd_soc_jack sdw_headset;
};

extern unsigned long sof_sdw_quirk;

int sdw_startup(struct snd_pcm_substream *substream);
int sdw_prepare(struct snd_pcm_substream *substream);
int sdw_trigger(struct snd_pcm_substream *substream, int cmd);
int sdw_hw_free(struct snd_pcm_substream *substream);
void sdw_shutdown(struct snd_pcm_substream *substream);

/* generic HDMI support */
int sof_sdw_hdmi_init(struct snd_soc_pcm_runtime *rtd);

int sof_sdw_hdmi_card_late_probe(struct snd_soc_card *card);

/* DMIC support */
int sof_sdw_dmic_init(struct snd_soc_pcm_runtime *rtd);

/* RT711 support */
int sof_sdw_rt711_init(const struct snd_soc_acpi_link_adr *link,
		       struct snd_soc_dai_link *dai_links,
		       struct sof_sdw_codec_info *info,
		       bool playback);
int sof_sdw_rt711_exit(struct device *dev, struct snd_soc_dai_link *dai_link);

/* RT711-SDCA support */
int sof_sdw_rt711_sdca_init(const struct snd_soc_acpi_link_adr *link,
			    struct snd_soc_dai_link *dai_links,
			    struct sof_sdw_codec_info *info,
			    bool playback);
int sof_sdw_rt711_sdca_exit(struct device *dev, struct snd_soc_dai_link *dai_link);

/* RT700 support */
int sof_sdw_rt700_init(const struct snd_soc_acpi_link_adr *link,
		       struct snd_soc_dai_link *dai_links,
		       struct sof_sdw_codec_info *info,
		       bool playback);

/* RT1308 support */
extern struct snd_soc_ops sof_sdw_rt1308_i2s_ops;

int sof_sdw_rt1308_init(const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback);

/* RT1316 support */
int sof_sdw_rt1316_init(const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback);

/* RT715 support */
int sof_sdw_rt715_init(const struct snd_soc_acpi_link_adr *link,
		       struct snd_soc_dai_link *dai_links,
		       struct sof_sdw_codec_info *info,
		       bool playback);

/* RT715-SDCA support */
int sof_sdw_rt715_sdca_init(const struct snd_soc_acpi_link_adr *link,
			    struct snd_soc_dai_link *dai_links,
			    struct sof_sdw_codec_info *info,
			    bool playback);

/* MAX98373 support */
int sof_sdw_mx8373_init(const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback);

int sof_sdw_mx8373_late_probe(struct snd_soc_card *card);

/* RT5682 support */
int sof_sdw_rt5682_init(const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback);

#endif
