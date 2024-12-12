// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek 8365 ALSA SoC Audio DAI ADDA Control
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
#include "../common/mtk-dai-adda-common.h"

static int adda_afe_on_ref_cnt;

/* DAI Drivers */

static int mt8365_dai_set_adda_out(struct mtk_base_afe *afe, unsigned int rate)
{
	unsigned int val;

	if (rate == 8000 || rate == 16000)
		val = AFE_ADDA_DL_VOICE_DATA;
	else
		val = 0;

	val |= FIELD_PREP(AFE_ADDA_DL_SAMPLING_RATE,
		mtk_adda_dl_rate_transform(afe, rate));
	val |= AFE_ADDA_DL_8X_UPSAMPLE |
	       AFE_ADDA_DL_MUTE_OFF_CH1 |
	       AFE_ADDA_DL_MUTE_OFF_CH2 |
	       AFE_ADDA_DL_DEGRADE_GAIN;

	regmap_update_bits(afe->regmap, AFE_ADDA_PREDIS_CON0, 0xffffffff, 0);
	regmap_update_bits(afe->regmap, AFE_ADDA_PREDIS_CON1, 0xffffffff, 0);
	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0, 0xffffffff, val);
	/* SA suggest apply -0.3db to audio/speech path */
	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON1,
			   0xffffffff, 0xf74f0000);
	/* SA suggest use default value for sdm */
	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SDM_DCCOMP_CON,
			   0xffffffff, 0x0700701e);

	return 0;
}

static int mt8365_dai_set_adda_in(struct mtk_base_afe *afe, unsigned int rate)
{
	unsigned int val;

	val = FIELD_PREP(AFE_ADDA_UL_SAMPLING_RATE,
			 mtk_adda_ul_rate_transform(afe, rate));
	regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
			   AFE_ADDA_UL_SAMPLING_RATE, val);
	/* Using Internal ADC */
	regmap_update_bits(afe->regmap, AFE_ADDA_TOP_CON0, 0x1, 0x0);

	return 0;
}

int mt8365_dai_enable_adda_on(struct mtk_base_afe *afe)
{
	unsigned long flags;
	struct mt8365_afe_private *afe_priv = afe->platform_priv;

	spin_lock_irqsave(&afe_priv->afe_ctrl_lock, flags);

	adda_afe_on_ref_cnt++;
	if (adda_afe_on_ref_cnt == 1)
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0,
				   AFE_ADDA_UL_DL_ADDA_AFE_ON,
				   AFE_ADDA_UL_DL_ADDA_AFE_ON);

	spin_unlock_irqrestore(&afe_priv->afe_ctrl_lock, flags);

	return 0;
}

int mt8365_dai_disable_adda_on(struct mtk_base_afe *afe)
{
	unsigned long flags;
	struct mt8365_afe_private *afe_priv = afe->platform_priv;

	spin_lock_irqsave(&afe_priv->afe_ctrl_lock, flags);

	adda_afe_on_ref_cnt--;
	if (adda_afe_on_ref_cnt == 0)
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0,
				   AFE_ADDA_UL_DL_ADDA_AFE_ON,
				   ~AFE_ADDA_UL_DL_ADDA_AFE_ON);
	else if (adda_afe_on_ref_cnt < 0) {
		adda_afe_on_ref_cnt = 0;
		dev_warn(afe->dev, "Abnormal adda_on ref count. Force it to 0\n");
	}

	spin_unlock_irqrestore(&afe_priv->afe_ctrl_lock, flags);

	return 0;
}

static void mt8365_dai_set_adda_out_enable(struct mtk_base_afe *afe,
					   bool enable)
{
	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0, 0x1, enable);

	if (enable)
		mt8365_dai_enable_adda_on(afe);
	else
		mt8365_dai_disable_adda_on(afe);
}

static void mt8365_dai_set_adda_in_enable(struct mtk_base_afe *afe, bool enable)
{
	if (enable) {
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0, 0x1, 0x1);
		mt8365_dai_enable_adda_on(afe);
		/* enable aud_pad_top fifo */
		regmap_update_bits(afe->regmap, AFE_AUD_PAD_TOP,
				   0xffffffff, 0x31);
	} else {
		/* disable aud_pad_top fifo */
		regmap_update_bits(afe->regmap, AFE_AUD_PAD_TOP,
				   0xffffffff, 0x30);
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0, 0x1, 0x0);
		/* de suggest disable ADDA_UL_SRC at least wait 125us */
		usleep_range(150, 300);
		mt8365_dai_disable_adda_on(afe);
	}
}

static int mt8365_dai_int_adda_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	unsigned int stream = substream->stream;

	mt8365_afe_enable_main_clk(afe);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mt8365_afe_enable_top_cg(afe, MT8365_TOP_CG_DAC);
		mt8365_afe_enable_top_cg(afe, MT8365_TOP_CG_DAC_PREDIS);
	} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		mt8365_afe_enable_top_cg(afe, MT8365_TOP_CG_ADC);
	}

	return 0;
}

static void mt8365_dai_int_adda_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_be_dai_data *be =
		&afe_priv->be_data[dai->id - MT8365_AFE_BACKEND_BASE];
	unsigned int stream = substream->stream;

	if (be->prepared[stream]) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			mt8365_dai_set_adda_out_enable(afe, false);
			mt8365_afe_set_i2s_out_enable(afe, false);
		} else {
			mt8365_dai_set_adda_in_enable(afe, false);
		}
		be->prepared[stream] = false;
	}

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mt8365_afe_disable_top_cg(afe, MT8365_TOP_CG_DAC_PREDIS);
		mt8365_afe_disable_top_cg(afe, MT8365_TOP_CG_DAC);
	} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		mt8365_afe_disable_top_cg(afe, MT8365_TOP_CG_ADC);
	}

	mt8365_afe_disable_main_clk(afe);
}

static int mt8365_dai_int_adda_prepare(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_be_dai_data *be =
		&afe_priv->be_data[dai->id - MT8365_AFE_BACKEND_BASE];
	unsigned int rate = substream->runtime->rate;
	int bit_width = snd_pcm_format_width(substream->runtime->format);
	int ret;

	dev_info(afe->dev, "%s '%s' rate = %u\n", __func__,
		 snd_pcm_stream_str(substream), rate);

	if (be->prepared[substream->stream]) {
		dev_info(afe->dev, "%s '%s' prepared already\n",
			 __func__, snd_pcm_stream_str(substream));
		return 0;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = mt8365_dai_set_adda_out(afe, rate);
		if (ret)
			return ret;

		ret = mt8365_afe_set_i2s_out(afe, rate, bit_width);
		if (ret)
			return ret;

		mt8365_dai_set_adda_out_enable(afe, true);
		mt8365_afe_set_i2s_out_enable(afe, true);
	} else {
		ret = mt8365_dai_set_adda_in(afe, rate);
		if (ret)
			return ret;

		mt8365_dai_set_adda_in_enable(afe, true);
	}
	be->prepared[substream->stream] = true;
	return 0;
}

static const struct snd_soc_dai_ops mt8365_afe_int_adda_ops = {
	.startup	= mt8365_dai_int_adda_startup,
	.shutdown	= mt8365_dai_int_adda_shutdown,
	.prepare	= mt8365_dai_int_adda_prepare,
};

static struct snd_soc_dai_driver mtk_dai_adda_driver[] = {
	{
		.name = "INT ADDA",
		.id = MT8365_AFE_IO_INT_ADDA,
		.playback = {
			.stream_name = "INT ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "INT ADDA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_int_adda_ops,
	}
};

/* DAI Controls */

static const struct snd_kcontrol_new mtk_adda_dl_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1 Switch", AFE_CONN3,
				    10, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2 Switch", AFE_CONN4,
				    11, 1, 0),
};

static const struct snd_kcontrol_new int_adda_o03_o04_enable_ctl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* DAI widget */

static const struct snd_soc_dapm_widget mtk_dai_adda_widgets[] = {
	SND_SOC_DAPM_SWITCH("INT ADDA O03_O04", SND_SOC_NOPM, 0, 0,
			    &int_adda_o03_o04_enable_ctl),
	/* inter-connections */
	SND_SOC_DAPM_MIXER("ADDA_DL_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch1_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch1_mix)),
	SND_SOC_DAPM_MIXER("ADDA_DL_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch2_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch2_mix)),
};

/* DAI route */

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	{"INT ADDA O03_O04", "Switch", "O03"},
	{"INT ADDA O03_O04", "Switch", "O04"},
	{"INT ADDA Playback", NULL, "INT ADDA O03_O04"},
	{"INT ADDA Playback", NULL, "ADDA_DL_CH1"},
	{"INT ADDA Playback", NULL, "ADDA_DL_CH2"},
	{"AIN Mux", "INT ADC", "INT ADDA Capture"},
	{"ADDA_DL_CH1", "GAIN1_OUT_CH1", "Hostless FM DL"},
	{"ADDA_DL_CH2", "GAIN1_OUT_CH2", "Hostless FM DL"},
};

int mt8365_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;
	list_add(&dai->list, &afe->sub_dais);
	dai->dai_drivers = mtk_dai_adda_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_adda_driver);
	dai->dapm_widgets = mtk_dai_adda_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_adda_widgets);
	dai->dapm_routes = mtk_dai_adda_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_adda_routes);
	return 0;
}
