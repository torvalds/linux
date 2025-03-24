// SPDX-License-Identifier: GPL-2.0
//
// mt8186-mt6366.c
//	--  MT8186-MT6366 ALSA SoC machine driver
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>
//
// Copyright (c) 2024 Collabora Ltd.
//                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
//

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/rt5682.h>
#include <sound/soc.h>

#include "../../codecs/da7219.h"
#include "../../codecs/mt6358.h"
#include "../../codecs/rt5682.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-dsp-sof-common.h"
#include "../common/mtk-soc-card.h"
#include "../common/mtk-soundcard-driver.h"
#include "mt8186-afe-common.h"
#include "mt8186-afe-clk.h"
#include "mt8186-afe-gpio.h"
#include "mt8186-mt6366-common.h"

#define RT1019_CODEC_DAI	"HiFi"
#define RT1019_DEV0_NAME	"rt1019p"

#define RT5682S_CODEC_DAI	"rt5682s-aif1"
#define RT5682S_DEV0_NAME	"rt5682s.5-001a"

#define DA7219_CODEC_DAI	"da7219-hifi"
#define DA7219_DEV_NAME		"da7219.5-001a"

#define SOF_DMA_DL1 "SOF_DMA_DL1"
#define SOF_DMA_DL2 "SOF_DMA_DL2"
#define SOF_DMA_UL1 "SOF_DMA_UL1"
#define SOF_DMA_UL2 "SOF_DMA_UL2"

#define DA7219_CODEC_PRESENT	BIT(0)

struct mt8186_mt6366_rt1019_rt5682s_priv {
	struct gpio_desc *dmic_sel;
	int dmic_switch;
};

enum mt8186_jacks {
	MT8186_JACK_HEADSET,
	MT8186_JACK_HDMI,
	MT8186_JACK_MAX,
};

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin mt8186_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static struct snd_soc_codec_conf mt8186_mt6366_rt1019_rt5682s_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("mt6358-sound"),
		.name_prefix = "Mt6366",
	},
	{
		.dlc = COMP_CODEC_CONF("bt-sco"),
		.name_prefix = "Mt8186 bt",
	},
	{
		.dlc = COMP_CODEC_CONF("hdmi-audio-codec"),
		.name_prefix = "Mt8186 hdmi",
	},
};

static int dmic_get(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct mtk_soc_card_data *soc_card_data =
		snd_soc_card_get_drvdata(dapm->card);
	struct mt8186_mt6366_rt1019_rt5682s_priv *priv = soc_card_data->mach_priv;

	ucontrol->value.integer.value[0] = priv->dmic_switch;
	return 0;
}

static int dmic_set(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct mtk_soc_card_data *soc_card_data =
		snd_soc_card_get_drvdata(dapm->card);
	struct mt8186_mt6366_rt1019_rt5682s_priv *priv = soc_card_data->mach_priv;

	priv->dmic_switch = ucontrol->value.integer.value[0];
	if (priv->dmic_sel) {
		gpiod_set_value(priv->dmic_sel, priv->dmic_switch);
		dev_dbg(dapm->card->dev, "dmic_set_value %d\n",
			 priv->dmic_switch);
	}
	return 0;
}

static const char * const dmic_mux_text[] = {
	"Front Mic",
	"Rear Mic",
};

static SOC_ENUM_SINGLE_DECL(mt8186_dmic_enum,
			    SND_SOC_NOPM, 0, dmic_mux_text);

static const struct snd_kcontrol_new mt8186_dmic_mux_control =
	SOC_DAPM_ENUM_EXT("DMIC Select Mux", mt8186_dmic_enum,
			  dmic_get, dmic_set);

static const struct snd_soc_dapm_widget dmic_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
	SND_SOC_DAPM_MUX("Dmic Mux", SND_SOC_NOPM, 0, 0, &mt8186_dmic_mux_control),
};

static const struct snd_soc_dapm_route dmic_map[] = {
	/* digital mics */
	{"Dmic Mux", "Front Mic", "DMIC"},
	{"Dmic Mux", "Rear Mic", "DMIC"},
};

static int primary_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mt8186_mt6366_rt1019_rt5682s_priv *priv = soc_card_data->mach_priv;
	int ret;

	ret = mt8186_mt6366_init(rtd);

	if (ret) {
		dev_err(card->dev, "mt8186_mt6366_init failed: %d\n", ret);
		return ret;
	}

	if (!priv->dmic_sel) {
		dev_dbg(card->dev, "dmic_sel is null\n");
		return 0;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, dmic_widgets,
					ARRAY_SIZE(dmic_widgets));
	if (ret) {
		dev_err(card->dev, "DMic widget addition failed: %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, dmic_map,
				      ARRAY_SIZE(dmic_map));

	if (ret)
		dev_err(card->dev, "DMic map addition failed: %d\n", ret);

	return ret;
}

static int mt8186_headset_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt_afe);
	struct mtk_soc_card_data *soc_card_data =
		snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8186_JACK_HEADSET];
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	const int hs_keys_rt5682[] = {
		KEY_PLAYPAUSE, KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_VOICECOMMAND
	};
	const int hs_keys_da7219[] = {
		KEY_PLAYPAUSE, KEY_VOICECOMMAND, KEY_VOLUMEUP, KEY_VOLUMEDOWN
	};
	const int *hs_keys;
	int ret;
	int type;

	ret = mt8186_dai_i2s_set_share(afe, "I2S1", "I2S0");
	if (ret) {
		dev_err(rtd->dev, "Failed to set up shared clocks\n");
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3,
				    jack, mt8186_jack_pins,
				    ARRAY_SIZE(mt8186_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	if (soc_card_data->card_data->flags & DA7219_CODEC_PRESENT)
		hs_keys = hs_keys_da7219;
	else
		hs_keys = hs_keys_rt5682;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, hs_keys[0]);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, hs_keys[1]);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, hs_keys[2]);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, hs_keys[3]);

	type = SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2 | SND_JACK_BTN_3;
	return snd_soc_component_set_jack(cmpnt_codec, jack, (void *)&type);
}

static int mt8186_da7219_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 256;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	unsigned int freq;
	int ret, j;

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_fs, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->dev, "failed to set cpu dai sysclk: %d\n", ret);
		return ret;
	}

	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		if (strcmp(codec_dai->component->name, DA7219_DEV_NAME))
			continue;

		ret = snd_soc_dai_set_sysclk(codec_dai, DA7219_CLKSRC_MCLK,
					     mclk_fs, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(rtd->dev, "failed to set sysclk: %d\n", ret);
			return ret;
		}

		if ((rate % 8000) == 0)
			freq = DA7219_PLL_FREQ_OUT_98304;
		else
			freq = DA7219_PLL_FREQ_OUT_90316;

		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_PLL_SRM,
					  0, freq);
		if (ret) {
			dev_err(rtd->dev, "failed to start PLL: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int mt8186_da7219_i2s_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int j, ret;

	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		if (strcmp(codec_dai->component->name, DA7219_DEV_NAME))
			continue;

		ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
		if (ret < 0) {
			dev_err(rtd->dev, "failed to stop PLL: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops mt8186_da7219_i2s_ops = {
	.hw_params = mt8186_da7219_i2s_hw_params,
	.hw_free = mt8186_da7219_i2s_hw_free,
};

static int mt8186_rt5682s_i2s_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	int bitwidth;
	int ret;

	bitwidth = snd_pcm_format_width(params_format(params));
	if (bitwidth < 0) {
		dev_err(card->dev, "invalid bit width: %d\n", bitwidth);
		return bitwidth;
	}

	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x00, 0x0, 0x2, bitwidth);
	if (ret) {
		dev_err(card->dev, "failed to set tdm slot\n");
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL1,
				  RT5682_PLL1_S_BCLK1,
				  params_rate(params) * 64,
				  params_rate(params) * 512);
	if (ret) {
		dev_err(card->dev, "failed to set pll\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai,
				     RT5682_SCLK_S_PLL1,
				     params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "failed to set sysclk\n");
		return ret;
	}

	return snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8186_rt5682s_i2s_ops = {
	.hw_params = mt8186_rt5682s_i2s_hw_params,
};

static int mt8186_mt6366_rt1019_rt5682s_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt_afe);
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	struct mtk_soc_card_data *soc_card_data =
		snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8186_JACK_HDMI];
	int ret;

	ret = mt8186_dai_i2s_set_share(afe, "I2S2", "I2S3");
	if (ret) {
		dev_err(rtd->dev, "Failed to set up shared clocks\n");
		return ret;
	}

	ret = snd_soc_card_jack_new(rtd->card, "HDMI Jack", SND_JACK_LINEOUT, jack);
	if (ret) {
		dev_err(rtd->dev, "HDMI Jack creation failed: %d\n", ret);
		return ret;
	}

	return snd_soc_component_set_jack(cmpnt_codec, jack, NULL);
}

static int mt8186_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params,
				  snd_pcm_format_t fmt)
{
	struct snd_interval *channels = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_CHANNELS);

	dev_dbg(rtd->dev, "%s(), fix format to %d\n", __func__, fmt);

	/* fix BE i2s channel to 2 channel */
	channels->min = 2;
	channels->max = 2;

	/* clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, fmt);

	return 0;
}

static int mt8186_i2s_hw_params_24le_fixup(struct snd_soc_pcm_runtime *rtd,
					   struct snd_pcm_hw_params *params)
{
	return mt8186_hw_params_fixup(rtd, params, SNDRV_PCM_FORMAT_S24_LE);
}

static int mt8186_i2s_hw_params_32le_fixup(struct snd_soc_pcm_runtime *rtd,
					   struct snd_pcm_hw_params *params)
{
	return mt8186_hw_params_fixup(rtd, params, SNDRV_PCM_FORMAT_S32_LE);
}

/* fixup the BE DAI link to match any values from topology */
static int mt8186_sof_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	ret = mtk_sof_dai_link_fixup(rtd, params);

	if (!strcmp(rtd->dai_link->name, "I2S0") ||
	    !strcmp(rtd->dai_link->name, "I2S1") ||
	    !strcmp(rtd->dai_link->name, "I2S2")) {
		if (soc_card_data->card_data->flags & DA7219_CODEC_PRESENT)
			mt8186_i2s_hw_params_32le_fixup(rtd, params);
		else
			mt8186_i2s_hw_params_24le_fixup(rtd, params);
	} else if (!strcmp(rtd->dai_link->name, "I2S3")) {
		if (soc_card_data->card_data->flags & DA7219_CODEC_PRESENT)
			mt8186_i2s_hw_params_24le_fixup(rtd, params);
		else
			mt8186_i2s_hw_params_32le_fixup(rtd, params);
	}

	return ret;
}

/* FE */
SND_SOC_DAILINK_DEFS(playback1,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback12,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL12")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback2,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback3,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback4,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback5,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL5")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback6,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback7,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL7")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback8,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture1,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture2,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture3,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture4,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture5,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL5")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture6,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture7,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL7")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* hostless */
SND_SOC_DAILINK_DEFS(hostless_lpbk,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless LPBK DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_fm,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless FM DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_src1,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_SRC_1_DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_src_bargein,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_SRC_Bargein_DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* BE */
SND_SOC_DAILINK_DEFS(adda,
		     DAILINK_COMP_ARRAY(COMP_CPU("ADDA")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6358-sound",
						   "mt6358-snd-codec-aif1"),
					COMP_CODEC("dmic-codec",
						   "dmic-hifi")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2s0,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S0")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2s1,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S1")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2s2,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2s3,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S3")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hw_gain1,
		     DAILINK_COMP_ARRAY(COMP_CPU("HW Gain 1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hw_gain2,
		     DAILINK_COMP_ARRAY(COMP_CPU("HW Gain 2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hw_src1,
		     DAILINK_COMP_ARRAY(COMP_CPU("HW_SRC_1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hw_src2,
		     DAILINK_COMP_ARRAY(COMP_CPU("HW_SRC_2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(connsys_i2s,
		     DAILINK_COMP_ARRAY(COMP_CPU("CONNSYS_I2S")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(pcm1,
		     DAILINK_COMP_ARRAY(COMP_CPU("PCM 1")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("bt-sco", "bt-sco-pcm-wb")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(tdm_in,
		     DAILINK_COMP_ARRAY(COMP_CPU("TDM IN")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* hostless */
SND_SOC_DAILINK_DEFS(hostless_ul1,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_UL1 DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_ul2,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_UL2 DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_ul3,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_UL3 DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_ul5,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_UL5 DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_ul6,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless_UL6 DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_hw_gain_aaudio,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless HW Gain AAudio DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(hostless_src_aaudio,
		     DAILINK_COMP_ARRAY(COMP_CPU("Hostless SRC AAudio DAI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(AFE_SOF_DL1,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_DL2,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_UL1,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_UL2,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

static const struct sof_conn_stream g_sof_conn_streams[] = {
	{ "I2S1", "AFE_SOF_DL1", SOF_DMA_DL1, SNDRV_PCM_STREAM_PLAYBACK},
	{ "I2S3", "AFE_SOF_DL2", SOF_DMA_DL2, SNDRV_PCM_STREAM_PLAYBACK},
	{ "Primary Codec", "AFE_SOF_UL1", SOF_DMA_UL1, SNDRV_PCM_STREAM_CAPTURE},
	{ "I2S0", "AFE_SOF_UL2", SOF_DMA_UL2, SNDRV_PCM_STREAM_CAPTURE},
};

static struct snd_soc_dai_link mt8186_mt6366_rt1019_rt5682s_dai_links[] = {
	/* Front End DAI links */
	{
		.name = "Playback_1",
		.stream_name = "Playback_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(playback1),
	},
	{
		.name = "Playback_12",
		.stream_name = "Playback_12",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback12),
	},
	{
		.name = "Playback_2",
		.stream_name = "Playback_2",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(playback2),
	},
	{
		.name = "Playback_3",
		.stream_name = "Playback_3",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(playback3),
	},
	{
		.name = "Playback_4",
		.stream_name = "Playback_4",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback4),
	},
	{
		.name = "Playback_5",
		.stream_name = "Playback_5",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback5),
	},
	{
		.name = "Playback_6",
		.stream_name = "Playback_6",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback6),
	},
	{
		.name = "Playback_7",
		.stream_name = "Playback_7",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback7),
	},
	{
		.name = "Playback_8",
		.stream_name = "Playback_8",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback8),
	},
	{
		.name = "Capture_1",
		.stream_name = "Capture_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture1),
	},
	{
		.name = "Capture_2",
		.stream_name = "Capture_2",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(capture2),
	},
	{
		.name = "Capture_3",
		.stream_name = "Capture_3",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture3),
	},
	{
		.name = "Capture_4",
		.stream_name = "Capture_4",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(capture4),
	},
	{
		.name = "Capture_5",
		.stream_name = "Capture_5",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture5),
	},
	{
		.name = "Capture_6",
		.stream_name = "Capture_6",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(capture6),
	},
	{
		.name = "Capture_7",
		.stream_name = "Capture_7",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture7),
	},
	{
		.name = "Hostless_LPBK",
		.stream_name = "Hostless_LPBK",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_lpbk),
	},
	{
		.name = "Hostless_FM",
		.stream_name = "Hostless_FM",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_fm),
	},
	{
		.name = "Hostless_SRC_1",
		.stream_name = "Hostless_SRC_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_src1),
	},
	{
		.name = "Hostless_SRC_Bargein",
		.stream_name = "Hostless_SRC_Bargein",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_src_bargein),
	},
	{
		.name = "Hostless_HW_Gain_AAudio",
		.stream_name = "Hostless_HW_Gain_AAudio",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_hw_gain_aaudio),
	},
	{
		.name = "Hostless_SRC_AAudio",
		.stream_name = "Hostless_SRC_AAudio",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_src_aaudio),
	},
	/* Back End DAI links */
	{
		.name = "Primary Codec",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.init = primary_codec_init,
		SND_SOC_DAILINK_REG(adda),
	},
	{
		.name = "I2S3",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_IB_IF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.playback_only = 1,
		.ignore_suspend = 1,
		.init = mt8186_mt6366_rt1019_rt5682s_hdmi_init,
		SND_SOC_DAILINK_REG(i2s3),
	},
	{
		.name = "I2S0",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &mt8186_rt5682s_i2s_ops,
		SND_SOC_DAILINK_REG(i2s0),
	},
	{
		.name = "I2S1",
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		.init = mt8186_headset_codec_init,
		SND_SOC_DAILINK_REG(i2s1),
	},
	{
		.name = "I2S2",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2s2),
	},
	{
		.name = "HW Gain 1",
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hw_gain1),
	},
	{
		.name = "HW Gain 2",
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hw_gain2),
	},
	{
		.name = "HW_SRC_1",
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hw_src1),
	},
	{
		.name = "HW_SRC_2",
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hw_src2),
	},
	{
		.name = "CONNSYS_I2S",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(connsys_i2s),
	},
	{
		.name = "PCM 1",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_IF,
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pcm1),
	},
	{
		.name = "TDM IN",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(tdm_in),
	},
	/* dummy BE for ul memif to record from dl memif */
	{
		.name = "Hostless_UL1",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_ul1),
	},
	{
		.name = "Hostless_UL2",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_ul2),
	},
	{
		.name = "Hostless_UL3",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_ul3),
	},
	{
		.name = "Hostless_UL5",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_ul5),
	},
	{
		.name = "Hostless_UL6",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hostless_ul6),
	},
	/* SOF BE */
	{
		.name = "AFE_SOF_DL1",
		.no_pcm = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(AFE_SOF_DL1),
	},
	{
		.name = "AFE_SOF_DL2",
		.no_pcm = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(AFE_SOF_DL2),
	},
	{
		.name = "AFE_SOF_UL1",
		.no_pcm = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(AFE_SOF_UL1),
	},
	{
		.name = "AFE_SOF_UL2",
		.no_pcm = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(AFE_SOF_UL2),
	},
};

static const struct snd_soc_dapm_widget
mt8186_mt6366_da7219_max98357_widgets[] = {
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_LINE("HDMI1", NULL),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL1, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL2, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL1, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL2, SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget
mt8186_mt6366_rt1019_rt5682s_widgets[] = {
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_OUTPUT("HDMI1"),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL1, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL2, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL1, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL2, SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route
mt8186_mt6366_rt1019_rt5682s_routes[] = {
	/* SPK */
	{ "Speakers", NULL, "Speaker" },
	/* Headset */
	{ "Headphone", NULL, "HPOL" },
	{ "Headphone", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
	/* HDMI */
	{ "HDMI1", NULL, "TX" },
	/* SOF Uplink */
	{SOF_DMA_UL1, NULL, "UL1_CH1"},
	{SOF_DMA_UL1, NULL, "UL1_CH2"},
	{SOF_DMA_UL2, NULL, "UL2_CH1"},
	{SOF_DMA_UL2, NULL, "UL2_CH2"},
	/* SOF Downlink */
	{"DSP_DL1_VIRT", NULL, SOF_DMA_DL1},
	{"DSP_DL2_VIRT", NULL, SOF_DMA_DL2},
};

static const struct snd_soc_dapm_route mt8186_mt6366_rt5650_routes[] = {
	/* SPK */
	{"Speakers", NULL, "SPOL"},
	{"Speakers", NULL, "SPOR"},
	/* Headset */
	{ "Headphone", NULL, "HPOL" },
	{ "Headphone", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
	{ "IN1N", NULL, "Headset Mic"},
	/* HDMI */
	{ "HDMI1", NULL, "TX" },
	/* SOF Uplink */
	{SOF_DMA_UL1, NULL, "UL1_CH1"},
	{SOF_DMA_UL1, NULL, "UL1_CH2"},
	{SOF_DMA_UL2, NULL, "UL2_CH1"},
	{SOF_DMA_UL2, NULL, "UL2_CH2"},
	/* SOF Downlink */
	{"DSP_DL1_VIRT", NULL, SOF_DMA_DL1},
	{"DSP_DL2_VIRT", NULL, SOF_DMA_DL2},
};

static const struct snd_kcontrol_new mt8186_mt6366_da7219_max98357_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Line Out"),
	SOC_DAPM_PIN_SWITCH("HDMI1"),
};

static const struct snd_kcontrol_new
mt8186_mt6366_rt1019_rt5682s_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("HDMI1"),
};

static struct snd_soc_card mt8186_mt6366_da7219_max98357_soc_card = {
	.name = "mt8186_da7219_max98357",
	.owner = THIS_MODULE,
	.dai_link = mt8186_mt6366_rt1019_rt5682s_dai_links,
	.num_links = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_dai_links),
	.controls = mt8186_mt6366_da7219_max98357_controls,
	.num_controls = ARRAY_SIZE(mt8186_mt6366_da7219_max98357_controls),
	.dapm_widgets = mt8186_mt6366_da7219_max98357_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8186_mt6366_da7219_max98357_widgets),
	.dapm_routes = mt8186_mt6366_rt1019_rt5682s_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_routes),
	.codec_conf = mt8186_mt6366_rt1019_rt5682s_codec_conf,
	.num_configs = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_codec_conf),
};

static struct snd_soc_card mt8186_mt6366_rt1019_rt5682s_soc_card = {
	.name = "mt8186_rt1019_rt5682s",
	.owner = THIS_MODULE,
	.dai_link = mt8186_mt6366_rt1019_rt5682s_dai_links,
	.num_links = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_dai_links),
	.controls = mt8186_mt6366_rt1019_rt5682s_controls,
	.num_controls = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_controls),
	.dapm_widgets = mt8186_mt6366_rt1019_rt5682s_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_widgets),
	.dapm_routes = mt8186_mt6366_rt1019_rt5682s_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_routes),
	.codec_conf = mt8186_mt6366_rt1019_rt5682s_codec_conf,
	.num_configs = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_codec_conf),
};

static struct snd_soc_card mt8186_mt6366_rt5682s_max98360_soc_card = {
	.name = "mt8186_rt5682s_max98360",
	.owner = THIS_MODULE,
	.dai_link = mt8186_mt6366_rt1019_rt5682s_dai_links,
	.num_links = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_dai_links),
	.controls = mt8186_mt6366_rt1019_rt5682s_controls,
	.num_controls = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_controls),
	.dapm_widgets = mt8186_mt6366_rt1019_rt5682s_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_widgets),
	.dapm_routes = mt8186_mt6366_rt1019_rt5682s_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_routes),
	.codec_conf = mt8186_mt6366_rt1019_rt5682s_codec_conf,
	.num_configs = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_codec_conf),
};

static struct snd_soc_card mt8186_mt6366_rt5650_soc_card = {
	.name = "mt8186_rt5650",
	.owner = THIS_MODULE,
	.dai_link = mt8186_mt6366_rt1019_rt5682s_dai_links,
	.num_links = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_dai_links),
	.controls = mt8186_mt6366_rt1019_rt5682s_controls,
	.num_controls = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_controls),
	.dapm_widgets = mt8186_mt6366_rt1019_rt5682s_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_widgets),
	.dapm_routes = mt8186_mt6366_rt5650_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8186_mt6366_rt5650_routes),
	.codec_conf = mt8186_mt6366_rt1019_rt5682s_codec_conf,
	.num_configs = ARRAY_SIZE(mt8186_mt6366_rt1019_rt5682s_codec_conf),
};

static int mt8186_mt6366_legacy_probe(struct mtk_soc_card_data *soc_card_data)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *dai_link;
	struct device_node *headset_codec, *playback_codec;
	int ret, i;

	playback_codec = of_get_child_by_name(dev->of_node, "playback-codecs");
	if (!playback_codec)
		return dev_err_probe(dev, -EINVAL,
				     "Property 'playback-codecs' missing or invalid\n");

	headset_codec = of_get_child_by_name(dev->of_node, "headset-codec");
	if (!headset_codec) {
		of_node_put(playback_codec);
		return dev_err_probe(dev, -EINVAL,
				     "Property 'headset-codec' missing or invalid\n");
	}

	for_each_card_prelinks(card, i, dai_link) {
		ret = mt8186_mt6366_card_set_be_link(card, dai_link, playback_codec, "I2S3");
		if (ret) {
			dev_err_probe(dev, ret, "%s set playback_codec fail\n",
				      dai_link->name);
			break;
		}

		ret = mt8186_mt6366_card_set_be_link(card, dai_link, headset_codec, "I2S0");
		if (ret) {
			dev_err_probe(dev, ret, "%s set headset_codec fail\n",
				      dai_link->name);
			break;
		}

		ret = mt8186_mt6366_card_set_be_link(card, dai_link, headset_codec, "I2S1");
		if (ret) {
			dev_err_probe(dev, ret, "%s set headset_codec fail\n",
				      dai_link->name);
			break;
		}
	}
	of_node_put(headset_codec);
	of_node_put(playback_codec);

	return ret;
}

static int mt8186_mt6366_soc_card_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct snd_soc_dai_link *dai_link;
	struct mt8186_mt6366_rt1019_rt5682s_priv *mach_priv;
	int i, ret;

	mach_priv = devm_kzalloc(card->dev, sizeof(*mach_priv), GFP_KERNEL);
	if (!mach_priv)
		return -ENOMEM;

	soc_card_data->mach_priv = mach_priv;

	mach_priv->dmic_sel = devm_gpiod_get_optional(card->dev,
						      "dmic", GPIOD_OUT_LOW);
	if (IS_ERR(mach_priv->dmic_sel))
		return dev_err_probe(card->dev, PTR_ERR(mach_priv->dmic_sel),
				     "DMIC gpio failed\n");

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "I2S0") == 0 ||
		    strcmp(dai_link->name, "I2S1") == 0 ||
		    strcmp(dai_link->name, "I2S2") == 0) {
			if (card_data->flags & DA7219_CODEC_PRESENT) {
				dai_link->be_hw_params_fixup = mt8186_i2s_hw_params_32le_fixup;
				dai_link->ops = &mt8186_da7219_i2s_ops;
			} else {
				dai_link->be_hw_params_fixup = mt8186_i2s_hw_params_24le_fixup;
				dai_link->ops = &mt8186_rt5682s_i2s_ops;
			}
		} else if (strcmp(dai_link->name, "I2S3") == 0) {
			if (card_data->flags & DA7219_CODEC_PRESENT)
				dai_link->be_hw_params_fixup = mt8186_i2s_hw_params_24le_fixup;
			else
				dai_link->be_hw_params_fixup = mt8186_i2s_hw_params_32le_fixup;
		}
	}

	if (legacy) {
		ret = mt8186_mt6366_legacy_probe(soc_card_data);
		if (ret)
			return ret;
	}

	ret = mt8186_afe_gpio_init(card->dev);
	if (ret)
		return dev_err_probe(card->dev, ret, "init AFE gpio error\n");

	return 0;
}

static const unsigned int mt8186_pcm_playback_channels[] = { 2 };
static const unsigned int mt8186_pcm_capture_channels[] = { 1, 2 };
static const unsigned int mt8186_pcm_rates[] = { 48000 };

static const struct snd_pcm_hw_constraint_list mt8186_rate_constraint = {
	.list = mt8186_pcm_rates,
	.count = ARRAY_SIZE(mt8186_pcm_rates)
};

static const struct mtk_pcm_constraints_data mt8186_pcm_constraints[MTK_CONSTRAINT_CAPTURE + 1] = {
	[MTK_CONSTRAINT_PLAYBACK] = {
		.channels = &(const struct snd_pcm_hw_constraint_list) {
			.list = mt8186_pcm_playback_channels,
			.count = ARRAY_SIZE(mt8186_pcm_playback_channels)
		},
		.rates = &mt8186_rate_constraint,
	},
	[MTK_CONSTRAINT_CAPTURE] = {
		.channels = &(const struct snd_pcm_hw_constraint_list) {
			.list = mt8186_pcm_capture_channels,
			.count = ARRAY_SIZE(mt8186_pcm_capture_channels)
		},
		.rates = &mt8186_rate_constraint,
	}
};

static const struct mtk_sof_priv mt8186_sof_priv = {
	.conn_streams = g_sof_conn_streams,
	.num_streams = ARRAY_SIZE(g_sof_conn_streams),
	.sof_dai_link_fixup = mt8186_sof_dai_link_fixup
};

static const struct mtk_soundcard_pdata mt8186_mt6366_da7219_max98357_pdata = {
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8186_mt6366_da7219_max98357_soc_card,
		.num_jacks = MT8186_JACK_MAX,
		.pcm_constraints = mt8186_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8186_pcm_constraints),
		.flags = DA7219_CODEC_PRESENT,
	},
	.sof_priv = &mt8186_sof_priv,
	.soc_probe = mt8186_mt6366_soc_card_probe
};

static const struct mtk_soundcard_pdata mt8186_mt6366_rt1019_rt5682s_pdata = {
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8186_mt6366_rt1019_rt5682s_soc_card,
		.num_jacks = MT8186_JACK_MAX,
		.pcm_constraints = mt8186_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8186_pcm_constraints),
	},
	.sof_priv = &mt8186_sof_priv,
	.soc_probe = mt8186_mt6366_soc_card_probe
};

static const struct mtk_soundcard_pdata mt8186_mt6366_rt5682s_max98360_pdata = {
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8186_mt6366_rt5682s_max98360_soc_card,
		.num_jacks = MT8186_JACK_MAX,
		.pcm_constraints = mt8186_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8186_pcm_constraints),
	},
	.sof_priv = &mt8186_sof_priv,
	.soc_probe = mt8186_mt6366_soc_card_probe
};

static const struct mtk_soundcard_pdata mt8186_mt6366_rt5650_pdata = {
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8186_mt6366_rt5650_soc_card,
		.num_jacks = MT8186_JACK_MAX,
		.pcm_constraints = mt8186_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8186_pcm_constraints),
	},
	.sof_priv = &mt8186_sof_priv,
	.soc_probe = mt8186_mt6366_soc_card_probe
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id mt8186_mt6366_dt_match[] = {
	{
		.compatible = "mediatek,mt8186-mt6366-rt1019-rt5682s-sound",
		.data = &mt8186_mt6366_rt1019_rt5682s_pdata,
	},
	{
		.compatible = "mediatek,mt8186-mt6366-rt5682s-max98360-sound",
		.data = &mt8186_mt6366_rt5682s_max98360_pdata,
	},
	{
		.compatible = "mediatek,mt8186-mt6366-rt5650-sound",
		.data = &mt8186_mt6366_rt5650_pdata,
	},
	{
		.compatible = "mediatek,mt8186-mt6366-da7219-max98357-sound",
		.data = &mt8186_mt6366_da7219_max98357_pdata,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt8186_mt6366_dt_match);
#endif

static struct platform_driver mt8186_mt6366_driver = {
	.driver = {
		.name = "mt8186_mt6366",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = mt8186_mt6366_dt_match,
#endif
		.pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};

module_platform_driver(mt8186_mt6366_driver);

/* Module information */
MODULE_DESCRIPTION("MT8186-MT6366 ALSA SoC machine driver");
MODULE_AUTHOR("Jiaxin Yu <jiaxin.yu@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt8186_mt6366 soc card");
