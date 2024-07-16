// SPDX-License-Identifier: GPL-2.0-only
//
// tegra186_asrc.c - Tegra186 ASRC driver
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra186_asrc.h"
#include "tegra_cif.h"

#define ASRC_STREAM_SOURCE_SELECT(id)					       \
	(TEGRA186_ASRC_CFG + ((id) * TEGRA186_ASRC_STREAM_STRIDE))

#define ASRC_STREAM_REG(reg, id) ((reg) + ((id) * TEGRA186_ASRC_STREAM_STRIDE))

#define ASRC_STREAM_REG_DEFAULTS(id)					       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_CFG, id),			       \
	  (((id) + 1) << 4) },						       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, id),		       \
	  0x1 },							       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, id),		       \
	  0x0 },							       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_MUTE_UNMUTE_DURATION, id),	       \
	  0x400 },							       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_RX_CIF_CTRL, id),		       \
	  0x7500 },							       \
	{ ASRC_STREAM_REG(TEGRA186_ASRC_TX_CIF_CTRL, id),		       \
	  0x7500 }

static const struct reg_default tegra186_asrc_reg_defaults[] = {
	ASRC_STREAM_REG_DEFAULTS(0),
	ASRC_STREAM_REG_DEFAULTS(1),
	ASRC_STREAM_REG_DEFAULTS(2),
	ASRC_STREAM_REG_DEFAULTS(3),
	ASRC_STREAM_REG_DEFAULTS(4),
	ASRC_STREAM_REG_DEFAULTS(5),

	{ TEGRA186_ASRC_GLOBAL_ENB, 0},
	{ TEGRA186_ASRC_GLOBAL_SOFT_RESET, 0},
	{ TEGRA186_ASRC_GLOBAL_CG, 0x1 },
	{ TEGRA186_ASRC_GLOBAL_CFG, 0x0 },
	{ TEGRA186_ASRC_GLOBAL_SCRATCH_ADDR, 0},
	{ TEGRA186_ASRC_GLOBAL_SCRATCH_CFG, 0x0c207980 },
	{ TEGRA186_ASRC_RATIO_UPD_RX_CIF_CTRL, 0x00115500 },
	{ TEGRA186_ASRC_GLOBAL_INT_MASK, 0x0},
	{ TEGRA186_ASRC_GLOBAL_INT_SET, 0x0},
	{ TEGRA186_ASRC_GLOBAL_INT_CLEAR, 0x0},
	{ TEGRA186_ASRC_GLOBAL_APR_CTRL, 0x0},
	{ TEGRA186_ASRC_GLOBAL_APR_CTRL_ACCESS_CTRL, 0x0},
	{ TEGRA186_ASRC_GLOBAL_DISARM_APR, 0x0},
	{ TEGRA186_ASRC_GLOBAL_DISARM_APR_ACCESS_CTRL, 0x0},
	{ TEGRA186_ASRC_GLOBAL_RATIO_WR_ACCESS, 0x0},
	{ TEGRA186_ASRC_GLOBAL_RATIO_WR_ACCESS_CTRL, 0x0},
	{ TEGRA186_ASRC_CYA, 0x0},
};

static void tegra186_asrc_lock_stream(struct tegra186_asrc *asrc,
				      unsigned int id)
{
	regmap_write(asrc->regmap,
		     ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_LOCK_STATUS,
				     id),
		     1);
}

static int __maybe_unused tegra186_asrc_runtime_suspend(struct device *dev)
{
	struct tegra186_asrc *asrc = dev_get_drvdata(dev);

	regcache_cache_only(asrc->regmap, true);
	regcache_mark_dirty(asrc->regmap);

	return 0;
}

static int __maybe_unused tegra186_asrc_runtime_resume(struct device *dev)
{
	struct tegra186_asrc *asrc = dev_get_drvdata(dev);
	int id;

	regcache_cache_only(asrc->regmap, false);

	/*
	 * Below sequence is recommended after a runtime PM cycle.
	 * This otherwise leads to transfer failures. The cache
	 * sync is done after this to restore other settings.
	 */
	regmap_write(asrc->regmap, TEGRA186_ASRC_GLOBAL_SCRATCH_ADDR,
		     TEGRA186_ASRC_ARAM_START_ADDR);
	regmap_write(asrc->regmap, TEGRA186_ASRC_GLOBAL_ENB,
		     TEGRA186_ASRC_GLOBAL_EN);

	regcache_sync(asrc->regmap);

	for (id = 0; id < TEGRA186_ASRC_STREAM_MAX; id++) {
		if (asrc->lane[id].ratio_source !=
		    TEGRA186_ASRC_RATIO_SOURCE_SW)
			continue;

		regmap_write(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART,
					id),
			asrc->lane[id].int_part);

		regmap_write(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART,
					id),
			asrc->lane[id].frac_part);

		tegra186_asrc_lock_stream(asrc, id);
	}

	return 0;
}

static int tegra186_asrc_set_audio_cif(struct tegra186_asrc *asrc,
				       struct snd_pcm_hw_params *params,
				       unsigned int reg)
{
	int channels, audio_bits;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		return -EINVAL;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	cif_conf.audio_bits = audio_bits;
	cif_conf.client_bits = TEGRA_ACIF_BITS_24;

	tegra_set_cif(asrc->regmap, reg, &cif_conf);

	return 0;
}

static int tegra186_asrc_in_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra186_asrc *asrc = snd_soc_dai_get_drvdata(dai);
	int ret, id = dai->id;

	/* Set input threshold */
	regmap_write(asrc->regmap,
		     ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, dai->id),
		     asrc->lane[id].input_thresh);

	ret = tegra186_asrc_set_audio_cif(asrc, params,
		ASRC_STREAM_REG(TEGRA186_ASRC_RX_CIF_CTRL, dai->id));
	if (ret) {
		dev_err(dev, "Can't set ASRC RX%d CIF: %d\n", dai->id, ret);
		return ret;
	}

	return ret;
}

static int tegra186_asrc_out_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params,
				       struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra186_asrc *asrc = snd_soc_dai_get_drvdata(dai);
	int ret, id = dai->id - 7;

	 /* Set output threshold */
	regmap_write(asrc->regmap,
		     ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, id),
		     asrc->lane[id].output_thresh);

	ret = tegra186_asrc_set_audio_cif(asrc, params,
		ASRC_STREAM_REG(TEGRA186_ASRC_TX_CIF_CTRL, id));
	if (ret) {
		dev_err(dev, "Can't set ASRC TX%d CIF: %d\n", id, ret);
		return ret;
	}

	/* Set ENABLE_HW_RATIO_COMP */
	if (asrc->lane[id].hwcomp_disable) {
		regmap_update_bits(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_CFG, id),
			TEGRA186_ASRC_STREAM_ENABLE_HW_RATIO_COMP_MASK,
			TEGRA186_ASRC_STREAM_ENABLE_HW_RATIO_COMP_DISABLE);
	} else {
		regmap_update_bits(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_CFG, id),
			TEGRA186_ASRC_STREAM_ENABLE_HW_RATIO_COMP_MASK,
			TEGRA186_ASRC_STREAM_ENABLE_HW_RATIO_COMP_ENABLE);

		regmap_write(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_COMP, id),
			TEGRA186_ASRC_STREAM_DEFAULT_HW_COMP_BIAS_VALUE);
	}

	/* Set lock */
	regmap_update_bits(asrc->regmap,
			   ASRC_STREAM_REG(TEGRA186_ASRC_CFG, id),
			   1, asrc->lane[id].ratio_source);

	if (asrc->lane[id].ratio_source == TEGRA186_ASRC_RATIO_SOURCE_SW) {
		regmap_write(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, id),
			asrc->lane[id].int_part);
		regmap_write(asrc->regmap,
			ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, id),
			asrc->lane[id].frac_part);
		tegra186_asrc_lock_stream(asrc, id);
	}

	return ret;
}

static int tegra186_asrc_get_ratio_source(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *asrc_private =
		(struct soc_enum  *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;

	ucontrol->value.enumerated.item[0] = asrc->lane[id].ratio_source;

	return 0;
}

static int tegra186_asrc_put_ratio_source(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *asrc_private =
		(struct soc_enum  *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;
	bool change = false;

	asrc->lane[id].ratio_source = ucontrol->value.enumerated.item[0];

	regmap_update_bits_check(asrc->regmap, asrc_private->reg,
				 TEGRA186_ASRC_STREAM_RATIO_TYPE_MASK,
				 asrc->lane[id].ratio_source,
				 &change);

	return change ? 1 : 0;
}

static int tegra186_asrc_get_ratio_int(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;

	regmap_read(asrc->regmap,
		    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, id),
		    &asrc->lane[id].int_part);

	ucontrol->value.integer.value[0] = asrc->lane[id].int_part;

	return 0;
}

static int tegra186_asrc_put_ratio_int(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;
	bool change = false;

	if (asrc->lane[id].ratio_source == TEGRA186_ASRC_RATIO_SOURCE_ARAD) {
		dev_err(cmpnt->dev,
			"Lane %d ratio source is ARAD, invalid SW update\n",
			id);
		return -EINVAL;
	}

	asrc->lane[id].int_part = ucontrol->value.integer.value[0];

	regmap_update_bits_check(asrc->regmap,
				 ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART,
						 id),
				 TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK,
				 asrc->lane[id].int_part, &change);

	tegra186_asrc_lock_stream(asrc, id);

	return change ? 1 : 0;
}

static int tegra186_asrc_get_ratio_frac(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mreg_control *asrc_private =
		(struct soc_mreg_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->regbase / TEGRA186_ASRC_STREAM_STRIDE;

	regmap_read(asrc->regmap,
		    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, id),
		    &asrc->lane[id].frac_part);

	ucontrol->value.integer.value[0] = asrc->lane[id].frac_part;

	return 0;
}

static int tegra186_asrc_put_ratio_frac(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mreg_control *asrc_private =
		(struct soc_mreg_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->regbase / TEGRA186_ASRC_STREAM_STRIDE;
	bool change = false;

	if (asrc->lane[id].ratio_source == TEGRA186_ASRC_RATIO_SOURCE_ARAD) {
		dev_err(cmpnt->dev,
			"Lane %d ratio source is ARAD, invalid SW update\n",
			id);
		return -EINVAL;
	}

	asrc->lane[id].frac_part = ucontrol->value.integer.value[0];

	regmap_update_bits_check(asrc->regmap,
				 ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART,
						 id),
				 TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
				 asrc->lane[id].frac_part, &change);

	tegra186_asrc_lock_stream(asrc, id);

	return change ? 1 : 0;
}

static int tegra186_asrc_get_hwcomp_disable(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;

	ucontrol->value.integer.value[0] = asrc->lane[id].hwcomp_disable;

	return 0;
}

static int tegra186_asrc_put_hwcomp_disable(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;
	int value = ucontrol->value.integer.value[0];

	if (value == asrc->lane[id].hwcomp_disable)
		return 0;

	asrc->lane[id].hwcomp_disable = value;

	return 1;
}

static int tegra186_asrc_get_input_threshold(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;

	ucontrol->value.integer.value[0] = (asrc->lane[id].input_thresh & 0x3);

	return 0;
}

static int tegra186_asrc_put_input_threshold(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;
	int value = (asrc->lane[id].input_thresh & ~(0x3)) |
		    ucontrol->value.integer.value[0];

	if (value == asrc->lane[id].input_thresh)
		return 0;

	asrc->lane[id].input_thresh = value;

	return 1;
}

static int tegra186_asrc_get_output_threshold(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;

	ucontrol->value.integer.value[0] = (asrc->lane[id].output_thresh & 0x3);

	return 0;
}

static int tegra186_asrc_put_output_threshold(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *asrc_private =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra186_asrc *asrc = snd_soc_component_get_drvdata(cmpnt);
	unsigned int id = asrc_private->reg / TEGRA186_ASRC_STREAM_STRIDE;
	int value = (asrc->lane[id].output_thresh & ~(0x3)) |
		    ucontrol->value.integer.value[0];

	if (value == asrc->lane[id].output_thresh)
		return 0;

	asrc->lane[id].output_thresh = value;

	return 1;
}

static int tegra186_asrc_widget_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct tegra186_asrc *asrc = dev_get_drvdata(cmpnt->dev);
	unsigned int id =
		(w->reg - TEGRA186_ASRC_ENABLE) / TEGRA186_ASRC_STREAM_STRIDE;

	regmap_write(asrc->regmap,
		     ASRC_STREAM_REG(TEGRA186_ASRC_SOFT_RESET, id),
		     0x1);

	return 0;
}

static const struct snd_soc_dai_ops tegra186_asrc_in_dai_ops = {
	.hw_params	= tegra186_asrc_in_hw_params,
};

static const struct snd_soc_dai_ops tegra186_asrc_out_dai_ops = {
	.hw_params	= tegra186_asrc_out_hw_params,
};

#define IN_DAI(id)						\
	{							\
		.name = "ASRC-RX-CIF"#id,			\
		.playback = {					\
			.stream_name = "RX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 12,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "RX" #id "-CIF-Capture", \
			.channels_min = 1,			\
			.channels_max = 12,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra186_asrc_in_dai_ops,		\
	}

#define OUT_DAI(id)						\
	{							\
		.name = "ASRC-TX-CIF"#id,			\
		.playback = {					\
			.stream_name = "TX" #id "-CIF-Playback",\
			.channels_min = 1,			\
			.channels_max = 12,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "TX" #id "-CIF-Capture",	\
			.channels_min = 1,			\
			.channels_max = 12,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra186_asrc_out_dai_ops,		\
	}

static struct snd_soc_dai_driver tegra186_asrc_dais[] = {
	/* ASRC Input */
	IN_DAI(1),
	IN_DAI(2),
	IN_DAI(3),
	IN_DAI(4),
	IN_DAI(5),
	IN_DAI(6),
	IN_DAI(7),
	/* ASRC Output */
	OUT_DAI(1),
	OUT_DAI(2),
	OUT_DAI(3),
	OUT_DAI(4),
	OUT_DAI(5),
	OUT_DAI(6),
};

static const struct snd_soc_dapm_widget tegra186_asrc_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX4", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX5", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX6", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("RX7", NULL, 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_AIF_OUT_E("TX1", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 0),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT_E("TX2", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 1),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT_E("TX3", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 2),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT_E("TX4", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 3),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT_E("TX5", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 4),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT_E("TX6", NULL, 0,
			       ASRC_STREAM_REG(TEGRA186_ASRC_ENABLE, 5),
			       TEGRA186_ASRC_STREAM_EN_SHIFT, 0,
			       tegra186_asrc_widget_event,
			       SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SPK("Depacketizer", NULL),
};

#define ASRC_STREAM_ROUTE(id, sname)					   \
	{ "RX" #id " XBAR-" sname,      NULL,   "RX" #id " XBAR-TX" },	   \
	{ "RX" #id "-CIF-" sname,       NULL,   "RX" #id " XBAR-" sname }, \
	{ "RX" #id,                     NULL,   "RX" #id "-CIF-" sname },  \
	{ "TX" #id,			NULL,   "RX" #id },		   \
	{ "TX" #id "-CIF-" sname,       NULL,   "TX" #id },		   \
	{ "TX" #id " XBAR-" sname,      NULL,   "TX" #id "-CIF-" sname },  \
	{ "TX" #id " XBAR-RX",          NULL,   "TX" #id " XBAR-" sname },

#define ASRC_ROUTE(id)							   \
	ASRC_STREAM_ROUTE(id, "Playback")				   \
	ASRC_STREAM_ROUTE(id, "Capture")

#define ASRC_RATIO_ROUTE(sname)						   \
	{ "RX7 XBAR-" sname,		NULL,	"RX7 XBAR-TX" },	   \
	{ "RX7-CIF-" sname,		NULL,	"RX7 XBAR-" sname },	   \
	{ "RX7",			NULL,	"RX7-CIF-" sname },	   \
	{ "Depacketizer",		NULL,	"RX7" },

static const struct snd_soc_dapm_route tegra186_asrc_routes[] = {
	ASRC_ROUTE(1)
	ASRC_ROUTE(2)
	ASRC_ROUTE(3)
	ASRC_ROUTE(4)
	ASRC_ROUTE(5)
	ASRC_ROUTE(6)
	ASRC_RATIO_ROUTE("Playback")
	ASRC_RATIO_ROUTE("Capture")
};

static const char * const tegra186_asrc_ratio_source_text[] = {
	"ARAD",
	"SW",
};

#define ASRC_SOURCE_DECL(name, id)					\
	static const struct soc_enum name =				\
		SOC_ENUM_SINGLE(ASRC_STREAM_SOURCE_SELECT(id),		\
				0, 2, tegra186_asrc_ratio_source_text)

ASRC_SOURCE_DECL(src_select1, 0);
ASRC_SOURCE_DECL(src_select2, 1);
ASRC_SOURCE_DECL(src_select3, 2);
ASRC_SOURCE_DECL(src_select4, 3);
ASRC_SOURCE_DECL(src_select5, 4);
ASRC_SOURCE_DECL(src_select6, 5);

#define SOC_SINGLE_EXT_FRAC(xname, xregbase, xmax, xget, xput)		\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= (xname),						\
	.info	= snd_soc_info_xr_sx,					\
	.get	= xget,							\
	.put	= xput,							\
									\
	.private_value = (unsigned long)&(struct soc_mreg_control)	\
	{								\
		.regbase	= xregbase,				\
		.regcount	= 1,					\
		.nbits		= 32,					\
		.invert		= 0,					\
		.min		= 0,					\
		.max		= xmax					\
	}								\
}

static const struct snd_kcontrol_new tegra186_asrc_controls[] = {
	/* Controls for integer part of ratio */
	SOC_SINGLE_EXT("Ratio1 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 0),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	SOC_SINGLE_EXT("Ratio2 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 1),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	SOC_SINGLE_EXT("Ratio3 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 2),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	SOC_SINGLE_EXT("Ratio4 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 3),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	SOC_SINGLE_EXT("Ratio5 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 4),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	SOC_SINGLE_EXT("Ratio6 Integer Part",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_INT_PART, 5),
		       0, TEGRA186_ASRC_STREAM_RATIO_INT_PART_MASK, 0,
		       tegra186_asrc_get_ratio_int,
		       tegra186_asrc_put_ratio_int),

	/* Controls for fractional part of ratio */
	SOC_SINGLE_EXT_FRAC("Ratio1 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 0),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	SOC_SINGLE_EXT_FRAC("Ratio2 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 1),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	SOC_SINGLE_EXT_FRAC("Ratio3 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 2),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	SOC_SINGLE_EXT_FRAC("Ratio4 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 3),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	SOC_SINGLE_EXT_FRAC("Ratio5 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 4),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	SOC_SINGLE_EXT_FRAC("Ratio6 Fractional Part",
			    ASRC_STREAM_REG(TEGRA186_ASRC_RATIO_FRAC_PART, 5),
			    TEGRA186_ASRC_STREAM_RATIO_FRAC_PART_MASK,
			    tegra186_asrc_get_ratio_frac,
			    tegra186_asrc_put_ratio_frac),

	/* Source of ratio provider */
	SOC_ENUM_EXT("Ratio1 Source", src_select1,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	SOC_ENUM_EXT("Ratio2 Source", src_select2,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	SOC_ENUM_EXT("Ratio3 Source", src_select3,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	SOC_ENUM_EXT("Ratio4 Source", src_select4,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	SOC_ENUM_EXT("Ratio5 Source", src_select5,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	SOC_ENUM_EXT("Ratio6 Source", src_select6,
		     tegra186_asrc_get_ratio_source,
		     tegra186_asrc_put_ratio_source),

	/* Disable HW managed overflow/underflow issue at input and output */
	SOC_SINGLE_EXT("Stream1 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 0), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	SOC_SINGLE_EXT("Stream2 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 1), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	SOC_SINGLE_EXT("Stream3 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 2), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	SOC_SINGLE_EXT("Stream4 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 3), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	SOC_SINGLE_EXT("Stream5 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 4), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	SOC_SINGLE_EXT("Stream6 HW Component Disable",
		       ASRC_STREAM_REG(TEGRA186_ASRC_CFG, 5), 0, 1, 0,
		       tegra186_asrc_get_hwcomp_disable,
		       tegra186_asrc_put_hwcomp_disable),

	/* Input threshold for watermark fields */
	SOC_SINGLE_EXT("Stream1 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 0), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	SOC_SINGLE_EXT("Stream2 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 1), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	SOC_SINGLE_EXT("Stream3 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 2), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	SOC_SINGLE_EXT("Stream4 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 3), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	SOC_SINGLE_EXT("Stream5 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 4), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	SOC_SINGLE_EXT("Stream6 Input Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_RX_THRESHOLD, 4), 0, 3, 0,
		       tegra186_asrc_get_input_threshold,
		       tegra186_asrc_put_input_threshold),

	/* Output threshold for watermark fields */
	SOC_SINGLE_EXT("Stream1 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 0), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),

	SOC_SINGLE_EXT("Stream2 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 1), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),

	SOC_SINGLE_EXT("Stream3 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 2), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),

	SOC_SINGLE_EXT("Stream4 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 3), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),

	SOC_SINGLE_EXT("Stream5 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 4), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),

	SOC_SINGLE_EXT("Stream6 Output Threshold",
		       ASRC_STREAM_REG(TEGRA186_ASRC_TX_THRESHOLD, 5), 0, 3, 0,
		       tegra186_asrc_get_output_threshold,
		       tegra186_asrc_put_output_threshold),
};

static const struct snd_soc_component_driver tegra186_asrc_cmpnt = {
	.dapm_widgets		= tegra186_asrc_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra186_asrc_widgets),
	.dapm_routes		= tegra186_asrc_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra186_asrc_routes),
	.controls		= tegra186_asrc_controls,
	.num_controls		= ARRAY_SIZE(tegra186_asrc_controls),
};

static bool tegra186_asrc_wr_reg(struct device *dev, unsigned int reg)
{
	if (reg < TEGRA186_ASRC_STREAM_LIMIT)
		reg %= TEGRA186_ASRC_STREAM_STRIDE;

	switch (reg) {
	case TEGRA186_ASRC_CFG ... TEGRA186_ASRC_RATIO_COMP:
	case TEGRA186_ASRC_RX_CIF_CTRL:
	case TEGRA186_ASRC_TX_CIF_CTRL:
	case TEGRA186_ASRC_ENABLE:
	case TEGRA186_ASRC_SOFT_RESET:
	case TEGRA186_ASRC_GLOBAL_ENB ... TEGRA186_ASRC_RATIO_UPD_RX_CIF_CTRL:
	case TEGRA186_ASRC_GLOBAL_INT_MASK ... TEGRA186_ASRC_GLOBAL_INT_CLEAR:
	case TEGRA186_ASRC_GLOBAL_APR_CTRL ... TEGRA186_ASRC_CYA:
		return true;
	default:
		return false;
	}
}

static bool tegra186_asrc_rd_reg(struct device *dev, unsigned int reg)
{
	if (reg < TEGRA186_ASRC_STREAM_LIMIT)
		reg %= TEGRA186_ASRC_STREAM_STRIDE;

	if (tegra186_asrc_wr_reg(dev, reg))
		return true;

	switch (reg) {
	case TEGRA186_ASRC_RX_STATUS:
	case TEGRA186_ASRC_TX_STATUS:
	case TEGRA186_ASRC_STATUS ... TEGRA186_ASRC_OUTSAMPLEBUF_CFG:
	case TEGRA186_ASRC_RATIO_UPD_RX_STATUS:
	case TEGRA186_ASRC_GLOBAL_STATUS ... TEGRA186_ASRC_GLOBAL_INT_STATUS:
	case TEGRA186_ASRC_GLOBAL_TRANSFER_ERROR_LOG:
		return true;
	default:
		return false;
	}
}

static bool tegra186_asrc_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg < TEGRA186_ASRC_STREAM_LIMIT)
		reg %= TEGRA186_ASRC_STREAM_STRIDE;

	switch (reg) {
	case TEGRA186_ASRC_RX_STATUS:
	case TEGRA186_ASRC_TX_STATUS:
	case TEGRA186_ASRC_SOFT_RESET:
	case TEGRA186_ASRC_RATIO_INT_PART:
	case TEGRA186_ASRC_RATIO_FRAC_PART:
	case TEGRA186_ASRC_STATUS:
	case TEGRA186_ASRC_RATIO_LOCK_STATUS:
	case TEGRA186_ASRC_RATIO_UPD_RX_STATUS:
	case TEGRA186_ASRC_GLOBAL_SOFT_RESET:
	case TEGRA186_ASRC_GLOBAL_STATUS:
	case TEGRA186_ASRC_GLOBAL_STREAM_ENABLE_STATUS:
	case TEGRA186_ASRC_GLOBAL_INT_STATUS:
	case TEGRA186_ASRC_GLOBAL_TRANSFER_ERROR_LOG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra186_asrc_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA186_ASRC_CYA,
	.writeable_reg		= tegra186_asrc_wr_reg,
	.readable_reg		= tegra186_asrc_rd_reg,
	.volatile_reg		= tegra186_asrc_volatile_reg,
	.reg_defaults		= tegra186_asrc_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra186_asrc_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra186_asrc_of_match[] = {
	{ .compatible = "nvidia,tegra186-asrc" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra186_asrc_of_match);

static int tegra186_asrc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra186_asrc *asrc;
	void __iomem *regs;
	unsigned int i;
	int err;

	asrc = devm_kzalloc(dev, sizeof(*asrc), GFP_KERNEL);
	if (!asrc)
		return -ENOMEM;

	dev_set_drvdata(dev, asrc);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	asrc->regmap = devm_regmap_init_mmio(dev, regs,
					     &tegra186_asrc_regmap_config);
	if (IS_ERR(asrc->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(asrc->regmap);
	}

	regcache_cache_only(asrc->regmap, true);

	regmap_write(asrc->regmap, TEGRA186_ASRC_GLOBAL_CFG,
		     TEGRA186_ASRC_GLOBAL_CFG_FRAC_32BIT_PRECISION);

	/* Initialize default output srate */
	for (i = 0; i < TEGRA186_ASRC_STREAM_MAX; i++) {
		asrc->lane[i].ratio_source = TEGRA186_ASRC_RATIO_SOURCE_SW;
		asrc->lane[i].int_part = 1;
		asrc->lane[i].frac_part = 0;
		asrc->lane[i].hwcomp_disable = 0;
		asrc->lane[i].input_thresh =
			TEGRA186_ASRC_STREAM_DEFAULT_INPUT_HW_COMP_THRESH_CFG;
		asrc->lane[i].output_thresh =
			TEGRA186_ASRC_STREAM_DEFAULT_OUTPUT_HW_COMP_THRESH_CFG;
	}

	err = devm_snd_soc_register_component(dev, &tegra186_asrc_cmpnt,
					      tegra186_asrc_dais,
					      ARRAY_SIZE(tegra186_asrc_dais));
	if (err) {
		dev_err(dev, "can't register ASRC component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(dev);

	return 0;
}

static int tegra186_asrc_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra186_asrc_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra186_asrc_runtime_suspend,
			   tegra186_asrc_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver tegra186_asrc_driver = {
	.driver = {
		.name = "tegra186-asrc",
		.of_match_table = tegra186_asrc_of_match,
		.pm = &tegra186_asrc_pm_ops,
	},
	.probe = tegra186_asrc_platform_probe,
	.remove = tegra186_asrc_platform_remove,
};
module_platform_driver(tegra186_asrc_driver)

MODULE_AUTHOR("Junghyun Kim <juskim@nvidia.com>");
MODULE_DESCRIPTION("Tegra186 ASRC ASoC driver");
MODULE_LICENSE("GPL");
