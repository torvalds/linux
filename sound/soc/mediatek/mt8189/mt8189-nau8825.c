// SPDX-License-Identifier: GPL-2.0
/*
 *  mt8189-nau8825.c  --  mt8189 nau8825 ALSA SoC machine driver
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>

#include "mt8189-afe-common.h"

#include "../common/mtk-soc-card.h"
#include "../common/mtk-soundcard-driver.h"
#include "../common/mtk-afe-platform-driver.h"

#include "../../codecs/cs35l41.h"
#include "../../codecs/nau8825.h"
#include "../../codecs/rt5682s.h"
#include "../../codecs/rt5682.h"

#define NAU8825_HS_PRESENT	BIT(0)
#define RT5682S_HS_PRESENT	BIT(1)
#define RT5650_HS_PRESENT	BIT(2)
#define RT5682I_HS_PRESENT	BIT(3)
#define ES8326_HS_PRESENT	BIT(4)

/*
 * Nau88l25
 */
#define NAU8825_CODEC_DAI  "nau8825-hifi"

/*
 * Rt5682s
 */
#define RT5682S_CODEC_DAI     "rt5682s-aif1"

/*
 * Rt5650
 */
#define RT5650_CODEC_DAI     "rt5645-aif1"

/*
 * Rt5682i
 */
#define RT5682I_CODEC_DAI     "rt5682-aif1"

/*
 * Cs35l41
 */
#define CS35L41_CODEC_DAI     "cs35l41-pcm"
#define CS35L41_DEV0_NAME     "cs35l41.7-0040"
#define CS35L41_DEV1_NAME     "cs35l41.7-0042"

/*
 * ES8326
 */
#define ES8326_CODEC_DAI  "ES8326 HiFi"

enum mt8189_jacks {
	MT8189_JACK_HEADSET,
	MT8189_JACK_DP,
	MT8189_JACK_HDMI,
	MT8189_JACK_MAX,
};

static struct snd_soc_jack_pin mt8189_dp_jack_pins[] = {
	{
		.pin = "DP",
		.mask = SND_JACK_LINEOUT,
	},
};

static struct snd_soc_jack_pin mt8189_hdmi_jack_pins[] = {
	{
		.pin = "HDMI",
		.mask = SND_JACK_LINEOUT,
	},
};

static struct snd_soc_jack_pin mt8189_headset_jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new mt8189_dumb_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_widget mt8189_dumb_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_widget mt8189_headset_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_kcontrol_new mt8189_headset_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget mt8189_nau8825_card_widgets[] = {
	SND_SOC_DAPM_SINK("DP"),
};

static int mt8189_common_i2s_startup(struct snd_pcm_substream *substream)
{
	static const unsigned int rates[] = {
		48000,
	};
	static const struct snd_pcm_hw_constraint_list constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list  = rates,
	};

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &constraints_rates);
}

static int mt8189_common_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(cpu_dai,
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8189_common_i2s_ops = {
	.startup = mt8189_common_i2s_startup,
	.hw_params = mt8189_common_i2s_hw_params,
};

static int mt8189_dptx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 256;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(dai, 0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8189_dptx_ops = {
	.hw_params = mt8189_dptx_hw_params,
};

static int mt8189_dptx_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	dev_dbg(rtd->dev, "%s(), fix format to 32bit\n", __func__);

	/* fix BE i2s format to 32bit, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S32_LE);

	return 0;
}

static const struct snd_soc_ops mt8189_pcm_ops = {
	.startup = mt8189_common_i2s_startup,
};

static int mt8189_nau8825_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int bit_width = params_width(params);
	int clk_freq, ret;

	clk_freq = rate * 2 * bit_width;
	dev_dbg(codec_dai->dev, "clk_freq %d, rate: %d, bit_width: %d\n",
		clk_freq, rate, bit_width);

	/* Configure clock for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8825_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK clock %d\n", ret);
		return ret;
	}

	/* Configure pll for codec */
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, clk_freq,
				  rate * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops mt8189_nau8825_ops = {
	.startup = mt8189_common_i2s_startup,
	.hw_params = mt8189_nau8825_hw_params,
};

static int mt8189_rtxxxx_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
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

	ret = snd_soc_dai_set_pll(codec_dai, 0, 1, rate * 32, rate * 512);
	if (ret) {
		dev_err(card->dev, "failed to set pll\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 1, rate * 512, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "failed to set sysclk\n");
		return ret;
	}

	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 512,
				      SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8189_rtxxxx_i2s_ops = {
	.startup = mt8189_common_i2s_startup,
	.hw_params = mt8189_rtxxxx_i2s_hw_params,
};

static int mt8189_cs35l41_i2s_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs = rate * 128;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	int clk_freq = rate * 32;
	int rx_slot[] = {0, 1};
	int i, ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_component_set_sysclk(codec_dai->component,
						   CS35L41_CLKID_SCLK, 0,
						   clk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(codec_dai->dev, "set component sysclk fail: %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, CS35L41_CLKID_SCLK,
					     clk_freq, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(codec_dai->dev, "set sysclk fail: %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dai_set_channel_map(codec_dai, 0, NULL,
						  1, &rx_slot[i]);
		if (ret < 0) {
			dev_err(codec_dai->dev, "set channel map fail: %d\n",
				ret);
			return ret;
		}
	}

	return snd_soc_dai_set_sysclk(cpu_dai,
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8189_cs35l41_i2s_ops = {
	.startup = mt8189_common_i2s_startup,
	.hw_params = mt8189_cs35l41_i2s_hw_params,
};

static int mt8189_es8326_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	int ret;

	/* Configure MCLK for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, rate * 256, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set MCLK %d\n", ret);
		return ret;
	}

	/* Configure MCLK for cpu */
	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 256, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8189_es8326_ops = {
	.startup = mt8189_common_i2s_startup,
	.hw_params = mt8189_es8326_hw_params,
};

static int mt8189_dumb_amp_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, mt8189_dumb_spk_widgets,
					ARRAY_SIZE(mt8189_dumb_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb Speaker dapm, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8189_dumb_spk_controls,
					ARRAY_SIZE(mt8189_dumb_spk_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb card controls, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt8189_dptx_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8189_JACK_DP];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret;

	ret = snd_soc_card_jack_new_pins(rtd->card, "DP Jack", SND_JACK_LINEOUT,
					 jack, mt8189_dp_jack_pins,
					 ARRAY_SIZE(mt8189_dp_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "%s, new jack failed: %d\n", __func__, ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "%s, set jack failed on %s (ret=%d)\n",
			__func__, component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8189_hdmi_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8189_JACK_HDMI];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret;

	ret = snd_soc_card_jack_new_pins(rtd->card, "HDMI Jack", SND_JACK_LINEOUT,
					 jack, mt8189_hdmi_jack_pins,
					 ARRAY_SIZE(mt8189_hdmi_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "%s, new jack failed: %d\n", __func__, ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "%s, set jack failed on %s (ret=%d)\n",
			__func__, component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8189_headset_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8189_JACK_HEADSET];
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	int ret;
	int type;

	ret = snd_soc_dapm_new_controls(dapm, mt8189_headset_widgets,
					ARRAY_SIZE(mt8189_headset_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card widget, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8189_headset_controls,
					ARRAY_SIZE(mt8189_headset_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 jack,
					 mt8189_headset_jack_pins,
					 ARRAY_SIZE(mt8189_headset_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	if (card_data->flags & ES8326_HS_PRESENT) {
		snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);
	} else {
		snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
	}

	type = SND_JACK_HEADSET | SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2 | SND_JACK_BTN_3;
	ret = snd_soc_component_set_jack(component, jack, (void *)&type);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return 0;
};

static void mt8189_headset_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

/* FE */
SND_SOC_DAILINK_DEFS(playback0,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback1,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
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
SND_SOC_DAILINK_DEFS(playback23,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL23")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback24,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL24")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback25,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL25")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback_24ch,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL_24CH")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture0,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL0")),
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
SND_SOC_DAILINK_DEFS(capture8,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture9,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL9")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture10,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL10")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture24,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL24")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture25,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL25")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture_cm0,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_CM0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture_cm1,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_CM1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture_etdm_in0,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_ETDM_IN0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(capture_etdm_in1,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_ETDM_IN1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback_hdmi,
		     DAILINK_COMP_ARRAY(COMP_CPU("HDMI")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
/* BE */
SND_SOC_DAILINK_DEFS(ap_dmic,
		     DAILINK_COMP_ARRAY(COMP_CPU("AP_DMIC")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(ap_dmic_ch34,
		     DAILINK_COMP_ARRAY(COMP_CPU("AP_DMIC_CH34")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sin0,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SIN0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sin1,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SIN1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sout0,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SOUT0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(i2sout1,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2SOUT1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(pcm0,
		     DAILINK_COMP_ARRAY(COMP_CPU("PCM 0")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(tdm_dptx,
		     DAILINK_COMP_ARRAY(COMP_CPU("TDM_DPTX")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mt8189_nau8825_dai_links[] = {
	/* Front End DAI links */
	{
		.name = "DL0_FE",
		.stream_name = "DL0 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(playback0),
	},
	{
		.name = "DL1_FE",
		.stream_name = "DL1 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(playback1),
	},
	{
		.name = "UL0_FE",
		.stream_name = "UL0 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(capture0),
	},
	{
		.name = "UL1_FE",
		.stream_name = "UL1 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(capture1),
	},
	{
		.name = "UL2_FE",
		.stream_name = "UL2 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(capture2),
	},
	{
		.name = "HDMI_FE",
		.stream_name = "HDMI Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback_hdmi),
	},
	{
		.name = "DL2_FE",
		.stream_name = "DL2 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
				SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback2),
	},
	{
		.name = "DL3_FE",
		.stream_name = "DL3 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback3),
	},
	{
		.name = "DL4_FE",
		.stream_name = "DL4 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback4),
	},
	{
		.name = "DL5_FE",
		.stream_name = "DL5 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback5),
	},
	{
		.name = "DL6_FE",
		.stream_name = "DL6 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback6),
	},
	{
		.name = "DL7_FE",
		.stream_name = "DL7 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback7),
	},
	{
		.name = "DL8 FE",
		.stream_name = "DL8 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback8),
	},
	{
		.name = "DL23 FE",
		.stream_name = "DL23 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback23),
	},
	{
		.name = "DL24 FE",
		.stream_name = "DL24 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback24),
	},
	{
		.name = "DL25 FE",
		.stream_name = "DL25 Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback25),
	},
	{
		.name = "DL_24CH_FE",
		.stream_name = "DL_24CH Playback",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback_24ch),
	},
	{
		.name = "UL9_FE",
		.stream_name = "UL9 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture9),
	},
	{
		.name = "UL3_FE",
		.stream_name = "UL3 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture3),
	},
	{
		.name = "UL7_FE",
		.stream_name = "UL7 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture7),
	},
	{
		.name = "UL4_FE",
		.stream_name = "UL4 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture4),
	},
	{
		.name = "UL5_FE",
		.stream_name = "UL5 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture5),
	},
	{
		.name = "UL_CM0_FE",
		.stream_name = "UL_CM0 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture_cm0),
	},
	{
		.name = "UL_CM1_FE",
		.stream_name = "UL_CM1 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture_cm1),
	},
	{
		.name = "UL10_FE",
		.stream_name = "UL10 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture10),
	},
	{
		.name = "UL6_FE",
		.stream_name = "UL6 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture6),
	},
	{
		.name = "UL25_FE",
		.stream_name = "UL25 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture25),
	},
	{
		.name = "UL8_FE",
		.stream_name = "UL8 Capture_Mono_1",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture8),
	},
	{
		.name = "UL24_FE",
		.stream_name = "UL24 Capture_Mono_2",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture24),
	},
	{
		.name = "UL_ETDM_In0_FE",
		.stream_name = "UL_ETDM_In0 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture_etdm_in0),
	},
	{
		.name = "UL_ETDM_In1_FE",
		.stream_name = "UL_ETDM_In1 Capture",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture_etdm_in1),
	},
	/* Back End DAI links */
	{
		.name = "I2SIN0_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8189_common_i2s_ops,
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2sin0),
	},
	{
		.name = "I2SIN1_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8189_common_i2s_ops,
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2sin1),
	},
	{
		.name = "I2SOUT0_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8189_common_i2s_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2sout0),
	},
	{
		.name = "I2SOUT1_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8189_common_i2s_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(i2sout1),
	},
	{
		.name = "AP_DMIC_BE",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(ap_dmic),
	},
	{
		.name = "AP_DMIC_CH34_BE",
		.no_pcm = 1,
		.capture_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(ap_dmic_ch34),
	},
	{
		.name = "TDM_DPTX_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.ops = &mt8189_dptx_ops,
		.be_hw_params_fixup = mt8189_dptx_hw_params_fixup,
		.no_pcm = 1,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(tdm_dptx),
	},
	{
		.name = "PCM_0_BE",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC
			| SND_SOC_DAIFMT_GATED,
		.no_pcm = 1,
		.ops = &mt8189_pcm_ops,
		.playback_only = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pcm0),
	},
};

static struct snd_soc_codec_conf mt8189_cs35l41_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(CS35L41_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static int mt8189_nau8825_soc_card_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct snd_soc_card *card = soc_card_data->card_data->card;
	struct snd_soc_dai_link *dai_link;
	bool init_nau8825 = false;
	bool init_rt5682s = false;
	bool init_rt5650 = false;
	bool init_rt5682i = false;
	bool init_es8326 = false;
	bool init_dumb = false;
	int i;

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "TDM_DPTX_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8189_dptx_codec_init;
		} else if (strcmp(dai_link->name, "PCM_0_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8189_hdmi_codec_init;
		} else if (strcmp(dai_link->name, "I2SOUT0_BE") == 0 ||
			   strcmp(dai_link->name, "I2SIN0_BE") == 0) {
			if (!strcmp(dai_link->codecs->dai_name, NAU8825_CODEC_DAI)) {
				dai_link->ops = &mt8189_nau8825_ops;
				if (!init_nau8825) {
					dai_link->init = mt8189_headset_codec_init;
					dai_link->exit = mt8189_headset_codec_exit;
					init_nau8825 = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5682S_CODEC_DAI)) {
				dai_link->ops = &mt8189_rtxxxx_i2s_ops;
				if (!init_rt5682s) {
					dai_link->init = mt8189_headset_codec_init;
					dai_link->exit = mt8189_headset_codec_exit;
					init_rt5682s = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5650_CODEC_DAI)) {
				dai_link->ops = &mt8189_rtxxxx_i2s_ops;
				if (!init_rt5650) {
					dai_link->init = mt8189_headset_codec_init;
					dai_link->exit = mt8189_headset_codec_exit;
					init_rt5650 = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5682I_CODEC_DAI)) {
				dai_link->ops = &mt8189_rtxxxx_i2s_ops;
				if (!init_rt5682i) {
					dai_link->init = mt8189_headset_codec_init;
					dai_link->exit = mt8189_headset_codec_exit;
					init_rt5682i = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, ES8326_CODEC_DAI)) {
				dai_link->ops = &mt8189_es8326_ops;
				if (!init_es8326) {
					dai_link->init = mt8189_headset_codec_init;
					dai_link->exit = mt8189_headset_codec_exit;
					init_es8326 = true;
				}
			} else {
				if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai")) {
					if (!init_dumb) {
						dai_link->init = mt8189_dumb_amp_init;
						init_dumb = true;
					}
				}
			}
		} else if (strcmp(dai_link->name, "I2SOUT1_BE") == 0) {
			if (!strcmp(dai_link->codecs->dai_name, CS35L41_CODEC_DAI)) {
				dai_link->ops = &mt8189_cs35l41_i2s_ops;
				card->num_configs = ARRAY_SIZE(mt8189_cs35l41_codec_conf);
				card->codec_conf = mt8189_cs35l41_codec_conf;
			}
		}
	}

	return 0;
}

static struct snd_soc_card mt8189_nau8825_soc_card = {
	.owner = THIS_MODULE,
	.dai_link = mt8189_nau8825_dai_links,
	.num_links = ARRAY_SIZE(mt8189_nau8825_dai_links),
	.dapm_widgets = mt8189_nau8825_card_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8189_nau8825_card_widgets),
};

static const struct mtk_soundcard_pdata mt8189_nau8825_card = {
	.card_name = "mt8189_nau8825",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8189_nau8825_soc_card,
		.num_jacks = MT8189_JACK_MAX,
		.flags = NAU8825_HS_PRESENT
	},
	.sof_priv = NULL,
	.soc_probe = mt8189_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8189_rt5650_card = {
	.card_name = "mt8189_rt5650",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8189_nau8825_soc_card,
		.num_jacks = MT8189_JACK_MAX,
		.flags = RT5650_HS_PRESENT
	},
	.sof_priv = NULL,
	.soc_probe = mt8189_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8189_rt5682s_card = {
	.card_name = "mt8189_rt5682s",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8189_nau8825_soc_card,
		.num_jacks = MT8189_JACK_MAX,
		.flags = RT5682S_HS_PRESENT
	},
	.sof_priv = NULL,
	.soc_probe = mt8189_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8189_rt5682i_card = {
	.card_name = "mt8189_rt5682i",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8189_nau8825_soc_card,
		.num_jacks = MT8189_JACK_MAX,
		.flags = RT5682I_HS_PRESENT
	},
	.sof_priv = NULL,
	.soc_probe = mt8189_nau8825_soc_card_probe,
};

static const struct mtk_soundcard_pdata mt8188_es8326_card = {
	.card_name = "mt8188_es8326",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8189_nau8825_soc_card,
		.num_jacks = MT8189_JACK_MAX,
		.flags = ES8326_HS_PRESENT
	},
	.sof_priv = NULL,
	.soc_probe = mt8189_nau8825_soc_card_probe,
};

static const struct of_device_id mt8189_nau8825_dt_match[] = {
	{.compatible = "mediatek,mt8189-nau8825", .data = &mt8189_nau8825_card,},
	{.compatible = "mediatek,mt8189-rt5650", .data = &mt8189_rt5650_card,},
	{.compatible = "mediatek,mt8189-rt5682s", .data = &mt8189_rt5682s_card,},
	{.compatible = "mediatek,mt8189-rt5682i", .data = &mt8189_rt5682i_card,},
	{.compatible = "mediatek,mt8189-es8326", .data = &mt8188_es8326_card,},
	{}
};
MODULE_DEVICE_TABLE(of, mt8189_nau8825_dt_match);

static struct platform_driver mt8189_nau8825_driver = {
	.driver = {
		.name = "mt8189-nau8825",
		.of_match_table = mt8189_nau8825_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};
module_platform_driver(mt8189_nau8825_driver);

/* Module information */
MODULE_DESCRIPTION("MT8189 NAU8825 ALSA SoC machine driver");
MODULE_AUTHOR("Darren Ye <darren.ye@mediatek.com>");
MODULE_AUTHOR("Cyril Chao <cyril.chao@mediatek.com>");
MODULE_LICENSE("GPL");
