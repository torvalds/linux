// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek 8365 ALSA SoC Audio DAI PCM Control
 *
 * Copyright (c) 2024 MediaTek Inc.
 * Authors: Jia Zeng <jia.zeng@mediatek.com>
 *          Alexandre Mergnat <amergnat@baylibre.com>
 */

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8365-afe-clk.h"
#include "mt8365-afe-common.h"

struct mt8365_pcm_intf_data {
	bool slave_mode;
	bool lrck_inv;
	bool bck_inv;
	unsigned int format;
};

/* DAI Drivers */

static void mt8365_dai_enable_pcm1(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, PCM_INTF_CON1,
			   PCM_INTF_CON1_EN, PCM_INTF_CON1_EN);
}

static void mt8365_dai_disable_pcm1(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, PCM_INTF_CON1,
			   PCM_INTF_CON1_EN, 0x0);
}

static int mt8365_dai_configure_pcm1(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_pcm_intf_data *pcm_priv = afe_priv->dai_priv[MT8365_AFE_IO_PCM1];
	bool slave_mode = pcm_priv->slave_mode;
	bool lrck_inv = pcm_priv->lrck_inv;
	bool bck_inv = pcm_priv->bck_inv;
	unsigned int fmt = pcm_priv->format;
	unsigned int bit_width = dai->sample_bits;
	unsigned int val = 0;

	if (!slave_mode) {
		val |= PCM_INTF_CON1_MASTER_MODE |
		       PCM_INTF_CON1_BYPASS_ASRC;

		if (lrck_inv)
			val |= PCM_INTF_CON1_SYNC_OUT_INV;
		if (bck_inv)
			val |= PCM_INTF_CON1_BCLK_OUT_INV;
	} else {
		val |= PCM_INTF_CON1_SLAVE_MODE;

		if (lrck_inv)
			val |= PCM_INTF_CON1_SYNC_IN_INV;
		if (bck_inv)
			val |= PCM_INTF_CON1_BCLK_IN_INV;

		/* TODO: add asrc setting */
	}

	val |= FIELD_PREP(PCM_INTF_CON1_FORMAT_MASK, fmt);

	if (fmt == MT8365_PCM_FORMAT_PCMA ||
	    fmt == MT8365_PCM_FORMAT_PCMB)
		val |= PCM_INTF_CON1_SYNC_LEN(1);
	else
		val |= PCM_INTF_CON1_SYNC_LEN(bit_width);

	switch (substream->runtime->rate) {
	case 48000:
		val |= PCM_INTF_CON1_FS_48K;
		break;
	case 32000:
		val |= PCM_INTF_CON1_FS_32K;
		break;
	case 16000:
		val |= PCM_INTF_CON1_FS_16K;
		break;
	case 8000:
		val |= PCM_INTF_CON1_FS_8K;
		break;
	default:
		return -EINVAL;
	}

	if (bit_width > 16)
		val |= PCM_INTF_CON1_24BIT | PCM_INTF_CON1_64BCK;
	else
		val |= PCM_INTF_CON1_16BIT | PCM_INTF_CON1_32BCK;

	val |= PCM_INTF_CON1_EXT_MODEM;

	regmap_update_bits(afe->regmap, PCM_INTF_CON1,
			   PCM_INTF_CON1_CONFIG_MASK, val);

	return 0;
}

static int mt8365_dai_pcm1_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	if (snd_soc_dai_active(dai))
		return 0;

	mt8365_afe_enable_main_clk(afe);

	return 0;
}

static void mt8365_dai_pcm1_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	if (snd_soc_dai_active(dai))
		return;

	mt8365_dai_disable_pcm1(afe);
	mt8365_afe_disable_main_clk(afe);
}

static int mt8365_dai_pcm1_prepare(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int ret;

	if ((snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_PLAYBACK) +
	    snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_CAPTURE)) > 1) {
		dev_info(afe->dev, "%s '%s' active(%u-%u) already\n",
			 __func__, snd_pcm_stream_str(substream),
			 snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_PLAYBACK),
			 snd_soc_dai_stream_active(dai, SNDRV_PCM_STREAM_CAPTURE));
		return 0;
	}

	ret = mt8365_dai_configure_pcm1(substream, dai);
	if (ret)
		return ret;

	mt8365_dai_enable_pcm1(afe);

	return 0;
}

static int mt8365_dai_pcm1_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_pcm_intf_data *pcm_priv = afe_priv->dai_priv[MT8365_AFE_IO_PCM1];

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		pcm_priv->format = MT8365_PCM_FORMAT_I2S;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		pcm_priv->bck_inv = false;
		pcm_priv->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		pcm_priv->bck_inv = false;
		pcm_priv->lrck_inv = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		pcm_priv->bck_inv = true;
		pcm_priv->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		pcm_priv->bck_inv = true;
		pcm_priv->lrck_inv = true;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		pcm_priv->slave_mode = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		pcm_priv->slave_mode = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops mt8365_dai_pcm1_ops = {
	.startup	= mt8365_dai_pcm1_startup,
	.shutdown	= mt8365_dai_pcm1_shutdown,
	.prepare	= mt8365_dai_pcm1_prepare,
	.set_fmt	= mt8365_dai_pcm1_set_fmt,
};

static struct snd_soc_dai_driver mtk_dai_pcm_driver[] = {
	{
		.name = "PCM1",
		.id = MT8365_AFE_IO_PCM1,
		.playback = {
			.stream_name = "PCM1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "PCM1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_dai_pcm1_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	}
};

/* DAI widget */

static const struct snd_soc_dapm_widget mtk_dai_pcm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("PCM1 Out"),
	SND_SOC_DAPM_INPUT("PCM1 In"),
};

/* DAI route */

static const struct snd_soc_dapm_route mtk_dai_pcm_routes[] = {
	{"PCM1 Playback", NULL, "O07"},
	{"PCM1 Playback", NULL, "O08"},
	{"PCM1 Out", NULL, "PCM1 Playback"},

	{"I09", NULL, "PCM1 Capture"},
	{"I22", NULL, "PCM1 Capture"},
	{"PCM1 Capture", NULL, "PCM1 In"},
};

static int init_pcmif_priv_data(struct mtk_base_afe *afe)
{
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_pcm_intf_data *pcmif_priv;

	pcmif_priv = devm_kzalloc(afe->dev, sizeof(struct mt8365_pcm_intf_data),
				  GFP_KERNEL);
	if (!pcmif_priv)
		return -ENOMEM;

	afe_priv->dai_priv[MT8365_AFE_IO_PCM1] = pcmif_priv;
	return 0;
}

int mt8365_dai_pcm_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);
	dai->dai_drivers = mtk_dai_pcm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_pcm_driver);
	dai->dapm_widgets = mtk_dai_pcm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_pcm_widgets);
	dai->dapm_routes = mtk_dai_pcm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_pcm_routes);
	return init_pcmif_priv_data(afe);
}
