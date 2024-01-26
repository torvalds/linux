// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021, 2023 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//	    Vijendar Mukunda <Vijendar.Mukunda@amd.com>
//

/*
 * Machine Driver Interface for ACP HW block
 */

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>
#include <linux/input.h>
#include <linux/module.h>

#include "../../codecs/rt5682.h"
#include "../../codecs/rt1019.h"
#include "../../codecs/rt5682s.h"
#include "../../codecs/nau8825.h"
#include "../../codecs/nau8821.h"
#include "acp-mach.h"

#define PCO_PLAT_CLK 48000000
#define RT5682_PLL_FREQ (48000 * 512)
#define DUAL_CHANNEL	2
#define FOUR_CHANNEL	4
#define NAU8821_CODEC_DAI	"nau8821-hifi"
#define NAU8821_BCLK		1536000
#define NAU8821_FREQ_OUT	12288000
#define MAX98388_CODEC_DAI	"max98388-aif1"

#define TDM_MODE_ENABLE 1

const struct dmi_system_id acp_quirk_table[] = {
	{
		/* Google skyrim proto-0 */
		.matches = {
			DMI_EXACT_MATCH(DMI_PRODUCT_FAMILY, "Google_Skyrim"),
		},
		.driver_data = (void *)TDM_MODE_ENABLE,
	},
	{}
};
EXPORT_SYMBOL_GPL(acp_quirk_table);

static const unsigned int channels[] = {
	DUAL_CHANNEL,
};

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int acp_clk_enable(struct acp_card_drvdata *drvdata,
			  unsigned int srate, unsigned int bclk_ratio)
{
	clk_set_rate(drvdata->wclk, srate);
	clk_set_rate(drvdata->bclk, srate * bclk_ratio);

	return clk_prepare_enable(drvdata->wclk);
}

/* Declare RT5682 codec components */
SND_SOC_DAILINK_DEF(rt5682,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00", "rt5682-aif1")));

static struct snd_soc_jack rt5682_jack;
static struct snd_soc_jack_pin rt5682_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new rt5682_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget rt5682_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route rt5682_map[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
};

/* Define card ops for RT5682 CODEC */
static int acp_card_rt5682_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != RT5682)
		return -EINVAL;

	drvdata->wclk = clk_get(component->dev, "rt5682-dai-wclk");
	drvdata->bclk = clk_get(component->dev, "rt5682-dai-bclk");

	ret = snd_soc_dapm_new_controls(&card->dapm, rt5682_widgets,
					ARRAY_SIZE(rt5682_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, rt5682_controls,
					ARRAY_SIZE(rt5682_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &rt5682_jack,
					 rt5682_jack_pins,
					 ARRAY_SIZE(rt5682_jack_pins));
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(rt5682_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(rt5682_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(rt5682_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(rt5682_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &rt5682_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, rt5682_map, ARRAY_SIZE(rt5682_map));
}

static int acp_card_hs_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret;
	unsigned int fmt;

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				      &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &constraints_rates);

	return ret;
}

static void acp_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;

	if (!drvdata->soc_mclk)
		clk_disable_unprepare(drvdata->wclk);
}

static int acp_card_rt5682_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;
	unsigned int fmt, srate, ch, format;

	srate = params_rate(params);
	ch = params_channels(params);
	format = params_physical_width(params);

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		/**
		 * As codec supports slot 0 and slot 1 for playback and capture.
		 */
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 8, 16);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "set TDM slot err: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 0x3, 8, 16);
		if (ret < 0) {
			dev_warn(rtd->dev, "set TDM slot err:%d\n", ret);
			return ret;
		}
	}

	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL2, RT5682_PLL2_S_MCLK,
				  PCO_PLAT_CLK, RT5682_PLL_FREQ);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set codec PLL: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL2,
				     RT5682_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set codec SYSCLK: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		ret = snd_soc_dai_set_pll(codec_dai, RT5682S_PLL1, RT5682S_PLL_S_BCLK1,
					  6144000, 49152000);
		if (ret < 0) {
			dev_err(rtd->dev, "Failed to set codec PLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, RT5682S_SCLK_S_PLL1,
					     49152000, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(rtd->dev, "Failed to set codec SYSCLK: %d\n", ret);
			return ret;
		}
	}

	/* Set tdm/i2s1 master bclk ratio */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, ch * format);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set rt5682 tdm bclk ratio: %d\n", ret);
		return ret;
	}

	if (!drvdata->soc_mclk) {
		ret = acp_clk_enable(drvdata, srate, ch * format);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to enable HS clk: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops acp_card_rt5682_ops = {
	.startup = acp_card_hs_startup,
	.shutdown = acp_card_shutdown,
	.hw_params = acp_card_rt5682_hw_params,
};

/* Define RT5682S CODEC component*/
SND_SOC_DAILINK_DEF(rt5682s,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-RTL5682:00", "rt5682s-aif1")));

static struct snd_soc_jack rt5682s_jack;
static struct snd_soc_jack_pin rt5682s_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new rt5682s_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget rt5682s_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route rt5682s_map[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
};

static int acp_card_rt5682s_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != RT5682S)
		return -EINVAL;

	if (!drvdata->soc_mclk) {
		drvdata->wclk = clk_get(component->dev, "rt5682-dai-wclk");
		drvdata->bclk = clk_get(component->dev, "rt5682-dai-bclk");
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, rt5682s_widgets,
					ARRAY_SIZE(rt5682s_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, rt5682s_controls,
					ARRAY_SIZE(rt5682s_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &rt5682s_jack,
					 rt5682s_jack_pins,
					 ARRAY_SIZE(rt5682s_jack_pins));
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(rt5682s_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(rt5682s_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(rt5682s_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(rt5682s_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &rt5682s_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, rt5682s_map, ARRAY_SIZE(rt5682s_map));
}

static int acp_card_rt5682s_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;
	unsigned int fmt, srate, ch, format;

	srate = params_rate(params);
	ch = params_channels(params);
	format = params_physical_width(params);

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		/**
		 * As codec supports slot 0 and slot 1 for playback and capture.
		 */
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 8, 16);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "set TDM slot err: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 0x3, 8, 16);
		if (ret < 0) {
			dev_warn(rtd->dev, "set TDM slot err:%d\n", ret);
			return ret;
		}
	}

	ret = snd_soc_dai_set_pll(codec_dai, RT5682S_PLL2, RT5682S_PLL_S_MCLK,
				  PCO_PLAT_CLK, RT5682_PLL_FREQ);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set codec PLL: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682S_SCLK_S_PLL2,
				     RT5682_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set codec SYSCLK: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		ret = snd_soc_dai_set_pll(codec_dai, RT5682S_PLL1, RT5682S_PLL_S_BCLK1,
					  6144000, 49152000);
		if (ret < 0) {
			dev_err(rtd->dev, "Failed to set codec PLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, RT5682S_SCLK_S_PLL1,
					     49152000, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(rtd->dev, "Failed to set codec SYSCLK: %d\n", ret);
			return ret;
		}
	}

	/* Set tdm/i2s1 master bclk ratio */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, ch * format);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set rt5682 tdm bclk ratio: %d\n", ret);
		return ret;
	}

	clk_set_rate(drvdata->wclk, srate);
	clk_set_rate(drvdata->bclk, srate * ch * format);

	return 0;
}

static const struct snd_soc_ops acp_card_rt5682s_ops = {
	.startup = acp_card_hs_startup,
	.hw_params = acp_card_rt5682s_hw_params,
};

static const unsigned int dmic_channels[] = {
	DUAL_CHANNEL, FOUR_CHANNEL,
};

static const struct snd_pcm_hw_constraint_list dmic_constraints_channels = {
	.count = ARRAY_SIZE(dmic_channels),
	.list = dmic_channels,
	.mask = 0,
};

static int acp_card_dmic_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &dmic_constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);

	return 0;
}

static const struct snd_soc_ops acp_card_dmic_ops = {
	.startup = acp_card_dmic_startup,
};

/* Declare RT1019 codec components */
SND_SOC_DAILINK_DEF(rt1019,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC1019:00", "rt1019-aif"),
			  COMP_CODEC("i2c-10EC1019:01", "rt1019-aif")));

static const struct snd_kcontrol_new rt1019_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget rt1019_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route rt1019_map_lr[] = {
	{ "Left Spk", NULL, "Left SPO" },
	{ "Right Spk", NULL, "Right SPO" },
};

static struct snd_soc_codec_conf rt1019_conf[] = {
	{
		 .dlc = COMP_CODEC_CONF("i2c-10EC1019:01"),
		 .name_prefix = "Left",
	},
	{
		 .dlc = COMP_CODEC_CONF("i2c-10EC1019:00"),
		 .name_prefix = "Right",
	},
};

static int acp_card_rt1019_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	int ret;

	if (drvdata->amp_codec_id != RT1019)
		return -EINVAL;

	ret = snd_soc_dapm_new_controls(&card->dapm, rt1019_widgets,
					ARRAY_SIZE(rt1019_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, rt1019_controls,
					ARRAY_SIZE(rt1019_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, rt1019_map_lr,
				       ARRAY_SIZE(rt1019_map_lr));
}

static int acp_card_rt1019_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int i, ret = 0;
	unsigned int fmt, srate, ch, format;

	srate = params_rate(params);
	ch = params_channels(params);
	format = params_physical_width(params);

	if (drvdata->amp_codec_id != RT1019)
		return -EINVAL;

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		/**
		 * As codec supports slot 2 and slot 3 for playback.
		 */
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0xC, 0, 8, 16);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "set TDM slot err: %d\n", ret);
			return ret;
		}
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (strcmp(codec_dai->name, "rt1019-aif"))
			continue;

		if (drvdata->tdm_mode)
			ret = snd_soc_dai_set_pll(codec_dai, 0, RT1019_PLL_S_BCLK,
						  TDM_CHANNELS * format * srate, 256 * srate);
		else
			ret = snd_soc_dai_set_pll(codec_dai, 0, RT1019_PLL_S_BCLK,
						  ch * format * srate, 256 * srate);

		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1019_SCLK_S_PLL,
					     256 * srate, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;

		if (drvdata->tdm_mode) {
			ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A
							| SND_SOC_DAIFMT_NB_NF);
			if (ret < 0) {
				dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
				return ret;
			}

			/**
			 * As codec supports slot 2 for left channel playback.
			 */
			if (!strcmp(codec_dai->component->name, "i2c-10EC1019:00")) {
				ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x4, 0x4, 8, 16);
				if (ret < 0)
					break;
			}

			/**
			 * As codec supports slot 3 for right channel playback.
			 */
			if (!strcmp(codec_dai->component->name, "i2c-10EC1019:01")) {
				ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x8, 0x8, 8, 16);
				if (ret < 0)
					break;
			}
		}
	}

	if (!drvdata->soc_mclk) {
		ret = acp_clk_enable(drvdata, srate, ch * format);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to enable AMP clk: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int acp_card_amp_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				      &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &constraints_rates);

	return 0;
}

static const struct snd_soc_ops acp_card_rt1019_ops = {
	.startup = acp_card_amp_startup,
	.shutdown = acp_card_shutdown,
	.hw_params = acp_card_rt1019_hw_params,
};

/* Declare Maxim codec components */
SND_SOC_DAILINK_DEF(max98360a,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98360A:00", "HiFi")));

static const struct snd_kcontrol_new max98360a_controls[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget max98360a_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_route max98360a_map[] = {
	{"Spk", NULL, "Speaker"},
};

static int acp_card_maxim_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	int ret;

	if (drvdata->amp_codec_id != MAX98360A)
		return -EINVAL;

	ret = snd_soc_dapm_new_controls(&card->dapm, max98360a_widgets,
					ARRAY_SIZE(max98360a_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, max98360a_controls,
					ARRAY_SIZE(max98360a_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, max98360a_map,
				       ARRAY_SIZE(max98360a_map));
}

static int acp_card_maxim_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	unsigned int fmt, srate, ch, format;
	int ret;

	srate = params_rate(params);
	ch = params_channels(params);
	format = params_physical_width(params);

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		/**
		 * As codec supports slot 2 and slot 3 for playback.
		 */
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0xC, 0, 8, 16);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "set TDM slot err: %d\n", ret);
			return ret;
		}
	}

	if (!drvdata->soc_mclk) {
		ret = acp_clk_enable(drvdata, srate, ch * format);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to enable AMP clk: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

static const struct snd_soc_ops acp_card_maxim_ops = {
	.startup = acp_card_amp_startup,
	.shutdown = acp_card_shutdown,
	.hw_params = acp_card_maxim_hw_params,
};

SND_SOC_DAILINK_DEF(max98388,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ADS8388:00", "max98388-aif1"),
				       COMP_CODEC("i2c-ADS8388:01", "max98388-aif1")));

static const struct snd_kcontrol_new max98388_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget max98388_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route max98388_map[] = {
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

static struct snd_soc_codec_conf max98388_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("i2c-ADS8388:00"),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-ADS8388:01"),
		.name_prefix = "Right",
	},
};

static const unsigned int max98388_format[] = {16};

static struct snd_pcm_hw_constraint_list constraints_sample_bits_max = {
	.list = max98388_format,
	.count = ARRAY_SIZE(max98388_format),
};

static int acp_card_max98388_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
				   &constraints_sample_bits_max);

	return 0;
}

static int acp_card_max98388_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	int ret;

	if (drvdata->amp_codec_id != MAX98388)
		return -EINVAL;

	ret = snd_soc_dapm_new_controls(&card->dapm, max98388_widgets,
					ARRAY_SIZE(max98388_widgets));

	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, max98388_controls,
					ARRAY_SIZE(max98388_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, max98388_map,
				       ARRAY_SIZE(max98388_map));
}

static int acp_max98388_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai =
			snd_soc_card_get_codec_dai(card,
						   MAX98388_CODEC_DAI);
	int ret;

	ret = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF);
	if (ret < 0)
		return ret;

	return ret;
}

static const struct snd_soc_ops acp_max98388_ops = {
	.startup = acp_card_max98388_startup,
	.hw_params = acp_max98388_hw_params,
};

/* Declare nau8825 codec components */
SND_SOC_DAILINK_DEF(nau8825,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10508825:00", "nau8825-hifi")));

static struct snd_soc_jack nau8825_jack;
static struct snd_soc_jack_pin nau8825_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new nau8825_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget nau8825_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route nau8825_map[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
};

static int acp_card_nau8825_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != NAU8825)
		return -EINVAL;

	ret = snd_soc_dapm_new_controls(&card->dapm, nau8825_widgets,
					ARRAY_SIZE(nau8825_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, nau8825_controls,
					ARRAY_SIZE(nau8825_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &nau8825_jack,
					 nau8825_jack_pins,
					 ARRAY_SIZE(nau8825_jack_pins));
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(nau8825_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(nau8825_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(nau8825_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(nau8825_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &nau8825_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, nau8825_map, ARRAY_SIZE(nau8825_map));
}

static int acp_nau8825_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;
	unsigned int fmt;

	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8825_CLK_FLL_FS,
				     (48000 * 256), SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, params_rate(params),
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set FLL: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode)
		fmt = SND_SOC_DAIFMT_DSP_A;
	else
		fmt = SND_SOC_DAIFMT_I2S;

	if (drvdata->soc_mclk)
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(rtd->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	if (drvdata->tdm_mode) {
		/**
		 * As codec supports slot 4 and slot 5 for playback and slot 6 for capture.
		 */
		ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0x30, 0xC0, 8, 16);
		if (ret && ret != -ENOTSUPP) {
			dev_err(rtd->dev, "set TDM slot err: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x40, 0x30, 8, 16);
		if (ret < 0) {
			dev_warn(rtd->dev, "set TDM slot err:%d\n", ret);
			return ret;
		}
	}
	return ret;
}

static int acp_nau8825_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = 2;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);

	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE, &constraints_rates);
	return 0;
}

static const struct snd_soc_ops acp_card_nau8825_ops = {
	.startup =  acp_nau8825_startup,
	.hw_params = acp_nau8825_hw_params,
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	codec_dai = snd_soc_card_get_codec_dai(card, NAU8821_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_INTERNAL,
					     0, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "set sysclk err = %d\n", ret);
			return -EIO;
		}
	} else {
		ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_FLL_BLK, 0,
					     SND_SOC_CLOCK_IN);
		if (ret < 0)
			dev_err(codec_dai->dev, "can't set FS clock %d\n", ret);
		ret = snd_soc_dai_set_pll(codec_dai, 0, 0, NAU8821_BCLK,
					  NAU8821_FREQ_OUT);
		if (ret < 0)
			dev_err(codec_dai->dev, "can't set FLL: %d\n", ret);
	}
	return ret;
}

static struct snd_soc_jack nau8821_jack;
static struct snd_soc_jack_pin nau8821_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new nau8821_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget nau8821_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route nau8821_audio_route[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
	{ "MICL", NULL, "Headset Mic" },
	{ "MICR", NULL, "Headset Mic" },
	{ "DMIC", NULL, "Int Mic" },
	{ "Headphone Jack", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Int Mic", NULL, "Platform Clock" },
};

static const unsigned int nau8821_format[] = {16};

static struct snd_pcm_hw_constraint_list constraints_sample_bits = {
	.list = nau8821_format,
	.count = ARRAY_SIZE(nau8821_format),
};

static int acp_8821_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	ret = snd_soc_dapm_new_controls(&card->dapm, nau8821_widgets,
					ARRAY_SIZE(nau8821_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add widget dapm controls, ret %d\n", ret);
		// Don't need to add routes if widget addition failed
		return ret;
	}

	ret = snd_soc_add_card_controls(card, nau8821_controls,
					ARRAY_SIZE(nau8821_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &nau8821_jack,
					 nau8821_jack_pins,
					 ARRAY_SIZE(nau8821_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(nau8821_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(nau8821_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(nau8821_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(nau8821_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	nau8821_enable_jack_detect(component, &nau8821_jack);

	return snd_soc_dapm_add_routes(&rtd->card->dapm, nau8821_audio_route,
				       ARRAY_SIZE(nau8821_audio_route));
}

static int acp_8821_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
				   &constraints_sample_bits);
	return 0;
}

static int acp_nau8821_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret;
	unsigned int fmt;

	if (drvdata->soc_mclk)
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(card->dev, "can't set FS clock %d\n", ret);
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, snd_soc_params_to_bclk(params),
				  params_rate(params) * 256);
	if (ret < 0)
		dev_err(card->dev, "can't set FLL: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops acp_8821_ops = {
	.startup = acp_8821_startup,
	.hw_params = acp_nau8821_hw_params,
};

SND_SOC_DAILINK_DEF(nau8821,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-NVTN2020:00",
						  "nau8821-hifi")));

/* Declare DMIC codec components */
SND_SOC_DAILINK_DEF(dmic_codec,
		DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

/* Declare ACP CPU components */
static struct snd_soc_dai_link_component platform_component[] = {
	{
		 .name = "acp_asoc_renoir.0",
	}
};

static struct snd_soc_dai_link_component platform_rmb_component[] = {
	{
		.name = "acp_asoc_rembrandt.0",
	}
};

static struct snd_soc_dai_link_component platform_acp63_component[] = {
	{
		.name = "acp_asoc_acp63.0",
	}
};

static struct snd_soc_dai_link_component platform_acp70_component[] = {
	{
		.name = "acp_asoc_acp70.0",
	}
};

static struct snd_soc_dai_link_component sof_component[] = {
	{
		 .name = "0000:04:00.5",
	}
};

SND_SOC_DAILINK_DEF(i2s_sp,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-i2s-sp")));
SND_SOC_DAILINK_DEF(i2s_hs,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp-i2s-hs")));
SND_SOC_DAILINK_DEF(sof_sp,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-sp")));
SND_SOC_DAILINK_DEF(sof_sp_virtual,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-sp-virtual")));
SND_SOC_DAILINK_DEF(sof_hs,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-hs")));
SND_SOC_DAILINK_DEF(sof_hs_virtual,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-hs-virtual")));
SND_SOC_DAILINK_DEF(sof_bt,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-bt")));
SND_SOC_DAILINK_DEF(sof_dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-dmic")));
SND_SOC_DAILINK_DEF(pdm_dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-pdm-dmic")));

static int acp_rtk_set_bias_level(struct snd_soc_card *card,
				  struct snd_soc_dapm_context *dapm,
				  enum snd_soc_bias_level level)
{
	struct snd_soc_component *component = dapm->component;
	struct acp_card_drvdata *drvdata = card->drvdata;
	int ret = 0;

	if (!component)
		return 0;

	if (strncmp(component->name, "i2c-RTL5682", 11) &&
	    strncmp(component->name, "i2c-10EC1019", 12))
		return 0;

	/*
	 * For Realtek's codec and amplifier components,
	 * the lrck and bclk must be enabled brfore their all dapms be powered on,
	 * and must be disabled after their all dapms be powered down
	 * to avoid any pop.
	 */
	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_dapm_get_bias_level(dapm) == SND_SOC_BIAS_OFF) {

			/* Increase bclk's enable_count */
			ret = clk_prepare_enable(drvdata->bclk);
			if (ret < 0)
				dev_err(component->dev, "Failed to enable bclk %d\n", ret);
		} else {
			/*
			 * Decrease bclk's enable_count.
			 * While the enable_count is 0, the bclk would be closed.
			 */
			clk_disable_unprepare(drvdata->bclk);
		}
		break;
	default:
		break;
	}

	return ret;
}

int acp_sofdsp_dai_links_create(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *links;
	struct device *dev = card->dev;
	struct acp_card_drvdata *drv_data = card->drvdata;
	int i = 0, num_links = 0;

	if (drv_data->hs_cpu_id)
		num_links++;
	if (drv_data->bt_cpu_id)
		num_links++;
	if (drv_data->amp_cpu_id)
		num_links++;
	if (drv_data->dmic_cpu_id)
		num_links++;

	links = devm_kcalloc(dev, num_links, sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	if (drv_data->hs_cpu_id == I2S_SP) {
		links[i].name = "acp-headset-codec";
		links[i].id = HEADSET_BE_ID;
		links[i].cpus = sof_sp;
		links[i].num_cpus = ARRAY_SIZE(sof_sp);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->hs_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->hs_codec_id == RT5682) {
			links[i].codecs = rt5682;
			links[i].num_codecs = ARRAY_SIZE(rt5682);
			links[i].init = acp_card_rt5682_init;
			links[i].ops = &acp_card_rt5682_ops;
		}
		if (drv_data->hs_codec_id == RT5682S) {
			links[i].codecs = rt5682s;
			links[i].num_codecs = ARRAY_SIZE(rt5682s);
			links[i].init = acp_card_rt5682s_init;
			links[i].ops = &acp_card_rt5682s_ops;
		}
		if (drv_data->hs_codec_id == NAU8821) {
			links[i].codecs = nau8821;
			links[i].num_codecs = ARRAY_SIZE(nau8821);
			links[i].init = acp_8821_init;
			links[i].ops = &acp_8821_ops;
		}
		i++;
	}

	if (drv_data->hs_cpu_id == I2S_HS) {
		links[i].name = "acp-headset-codec";
		links[i].id = HEADSET_BE_ID;
		links[i].cpus = sof_hs;
		links[i].num_cpus = ARRAY_SIZE(sof_hs);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->hs_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->hs_codec_id == NAU8825) {
			links[i].codecs = nau8825;
			links[i].num_codecs = ARRAY_SIZE(nau8825);
			links[i].init = acp_card_nau8825_init;
			links[i].ops = &acp_card_nau8825_ops;
		}
		if (drv_data->hs_codec_id == RT5682S) {
			links[i].codecs = rt5682s;
			links[i].num_codecs = ARRAY_SIZE(rt5682s);
			links[i].init = acp_card_rt5682s_init;
			links[i].ops = &acp_card_rt5682s_ops;
		}
		i++;
	}

	if (drv_data->amp_cpu_id == I2S_SP) {
		links[i].name = "acp-amp-codec";
		links[i].id = AMP_BE_ID;
		links[i].cpus = sof_sp_virtual;
		links[i].num_cpus = ARRAY_SIZE(sof_sp_virtual);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->amp_codec_id == RT1019) {
			links[i].codecs = rt1019;
			links[i].num_codecs = ARRAY_SIZE(rt1019);
			links[i].ops = &acp_card_rt1019_ops;
			links[i].init = acp_card_rt1019_init;
			card->codec_conf = rt1019_conf;
			card->num_configs = ARRAY_SIZE(rt1019_conf);
		}
		if (drv_data->amp_codec_id == MAX98360A) {
			links[i].codecs = max98360a;
			links[i].num_codecs = ARRAY_SIZE(max98360a);
			links[i].ops = &acp_card_maxim_ops;
			links[i].init = acp_card_maxim_init;
		}
		i++;
	}

	if (drv_data->amp_cpu_id == I2S_HS) {
		links[i].name = "acp-amp-codec";
		links[i].id = AMP_BE_ID;
		links[i].cpus = sof_hs_virtual;
		links[i].num_cpus = ARRAY_SIZE(sof_hs_virtual);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->amp_codec_id == MAX98360A) {
			links[i].codecs = max98360a;
			links[i].num_codecs = ARRAY_SIZE(max98360a);
			links[i].ops = &acp_card_maxim_ops;
			links[i].init = acp_card_maxim_init;
		}
		if (drv_data->amp_codec_id == MAX98388) {
			links[i].dpcm_capture = 1;
			links[i].codecs = max98388;
			links[i].num_codecs = ARRAY_SIZE(max98388);
			links[i].ops = &acp_max98388_ops;
			links[i].init = acp_card_max98388_init;
			card->codec_conf = max98388_conf;
			card->num_configs = ARRAY_SIZE(max98388_conf);
		}
		if (drv_data->amp_codec_id == RT1019) {
			links[i].codecs = rt1019;
			links[i].num_codecs = ARRAY_SIZE(rt1019);
			links[i].ops = &acp_card_rt1019_ops;
			links[i].init = acp_card_rt1019_init;
			card->codec_conf = rt1019_conf;
			card->num_configs = ARRAY_SIZE(rt1019_conf);
		}
		i++;
	}

	if (drv_data->bt_cpu_id == I2S_BT) {
		links[i].name = "acp-bt-codec";
		links[i].id = BT_BE_ID;
		links[i].cpus = sof_bt;
		links[i].num_cpus = ARRAY_SIZE(sof_bt);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->bt_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		i++;
	}

	if (drv_data->dmic_cpu_id == DMIC) {
		links[i].name = "acp-dmic-codec";
		links[i].id = DMIC_BE_ID;
		links[i].codecs = dmic_codec;
		links[i].num_codecs = ARRAY_SIZE(dmic_codec);
		links[i].cpus = sof_dmic;
		links[i].num_cpus = ARRAY_SIZE(sof_dmic);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_capture = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
	}

	card->dai_link = links;
	card->num_links = num_links;
	card->set_bias_level = acp_rtk_set_bias_level;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_sofdsp_dai_links_create, SND_SOC_AMD_MACH);

int acp_legacy_dai_links_create(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *links;
	struct device *dev = card->dev;
	struct acp_card_drvdata *drv_data = card->drvdata;
	int i = 0, num_links = 0;
	int rc;

	if (drv_data->hs_cpu_id)
		num_links++;
	if (drv_data->amp_cpu_id)
		num_links++;
	if (drv_data->dmic_cpu_id)
		num_links++;

	links = devm_kcalloc(dev, num_links, sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	if (drv_data->hs_cpu_id == I2S_SP) {
		links[i].name = "acp-headset-codec";
		links[i].id = HEADSET_BE_ID;
		links[i].cpus = i2s_sp;
		links[i].num_cpus = ARRAY_SIZE(i2s_sp);
		links[i].platforms = platform_component;
		links[i].num_platforms = ARRAY_SIZE(platform_component);
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
		if (!drv_data->hs_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->hs_codec_id == RT5682) {
			links[i].codecs = rt5682;
			links[i].num_codecs = ARRAY_SIZE(rt5682);
			links[i].init = acp_card_rt5682_init;
			links[i].ops = &acp_card_rt5682_ops;
		}
		if (drv_data->hs_codec_id == RT5682S) {
			links[i].codecs = rt5682s;
			links[i].num_codecs = ARRAY_SIZE(rt5682s);
			links[i].init = acp_card_rt5682s_init;
			links[i].ops = &acp_card_rt5682s_ops;
		}
		if (drv_data->hs_codec_id == ES83XX) {
			rc = acp_ops_configure_link(card, &links[i]);
			if (rc != 0) {
				dev_err(dev, "Failed to configure link for ES83XX: %d\n", rc);
				return rc;
			}
		}
		i++;
	}

	if (drv_data->hs_cpu_id == I2S_HS) {
		links[i].name = "acp-headset-codec";
		links[i].id = HEADSET_BE_ID;
		links[i].cpus = i2s_hs;
		links[i].num_cpus = ARRAY_SIZE(i2s_hs);
		if (drv_data->platform == REMBRANDT) {
			links[i].platforms = platform_rmb_component;
			links[i].num_platforms = ARRAY_SIZE(platform_rmb_component);
		} else if (drv_data->platform == ACP63) {
			links[i].platforms = platform_acp63_component;
			links[i].num_platforms = ARRAY_SIZE(platform_acp63_component);
		} else {
			links[i].platforms = platform_component;
			links[i].num_platforms = ARRAY_SIZE(platform_component);
		}
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
		if (!drv_data->hs_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->hs_codec_id == NAU8825) {
			links[i].codecs = nau8825;
			links[i].num_codecs = ARRAY_SIZE(nau8825);
			links[i].init = acp_card_nau8825_init;
			links[i].ops = &acp_card_nau8825_ops;
		}
		if (drv_data->hs_codec_id == RT5682S) {
			links[i].codecs = rt5682s;
			links[i].num_codecs = ARRAY_SIZE(rt5682s);
			links[i].init = acp_card_rt5682s_init;
			links[i].ops = &acp_card_rt5682s_ops;
		}
		i++;
	}

	if (drv_data->amp_cpu_id == I2S_SP) {
		links[i].name = "acp-amp-codec";
		links[i].id = AMP_BE_ID;
		links[i].cpus = i2s_sp;
		links[i].num_cpus = ARRAY_SIZE(i2s_sp);
		links[i].platforms = platform_component;
		links[i].num_platforms = ARRAY_SIZE(platform_component);
		links[i].dpcm_playback = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->amp_codec_id == RT1019) {
			links[i].codecs = rt1019;
			links[i].num_codecs = ARRAY_SIZE(rt1019);
			links[i].ops = &acp_card_rt1019_ops;
			links[i].init = acp_card_rt1019_init;
			card->codec_conf = rt1019_conf;
			card->num_configs = ARRAY_SIZE(rt1019_conf);
		}
		if (drv_data->amp_codec_id == MAX98360A) {
			links[i].codecs = max98360a;
			links[i].num_codecs = ARRAY_SIZE(max98360a);
			links[i].ops = &acp_card_maxim_ops;
			links[i].init = acp_card_maxim_init;
		}
		i++;
	}

	if (drv_data->amp_cpu_id == I2S_HS) {
		links[i].name = "acp-amp-codec";
		links[i].id = AMP_BE_ID;
		links[i].cpus = i2s_hs;
		links[i].num_cpus = ARRAY_SIZE(i2s_hs);
		if (drv_data->platform == REMBRANDT) {
			links[i].platforms = platform_rmb_component;
			links[i].num_platforms = ARRAY_SIZE(platform_rmb_component);
		} else if (drv_data->platform == ACP63) {
			links[i].platforms = platform_acp63_component;
			links[i].num_platforms = ARRAY_SIZE(platform_acp63_component);
		} else {
			links[i].platforms = platform_component;
			links[i].num_platforms = ARRAY_SIZE(platform_component);
		}
		links[i].dpcm_playback = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		if (drv_data->amp_codec_id == MAX98360A) {
			links[i].codecs = max98360a;
			links[i].num_codecs = ARRAY_SIZE(max98360a);
			links[i].ops = &acp_card_maxim_ops;
			links[i].init = acp_card_maxim_init;
		}
		if (drv_data->amp_codec_id == RT1019) {
			links[i].codecs = rt1019;
			links[i].num_codecs = ARRAY_SIZE(rt1019);
			links[i].ops = &acp_card_rt1019_ops;
			links[i].init = acp_card_rt1019_init;
			card->codec_conf = rt1019_conf;
			card->num_configs = ARRAY_SIZE(rt1019_conf);
		}
		i++;
	}

	if (drv_data->dmic_cpu_id == DMIC) {
		links[i].name = "acp-dmic-codec";
		links[i].id = DMIC_BE_ID;
		if (drv_data->dmic_codec_id == DMIC) {
			links[i].codecs = dmic_codec;
			links[i].num_codecs = ARRAY_SIZE(dmic_codec);
		} else {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = &snd_soc_dummy_dlc;
			links[i].num_codecs = 1;
		}
		links[i].cpus = pdm_dmic;
		links[i].num_cpus = ARRAY_SIZE(pdm_dmic);
		if (drv_data->platform == REMBRANDT) {
			links[i].platforms = platform_rmb_component;
			links[i].num_platforms = ARRAY_SIZE(platform_rmb_component);
		} else if (drv_data->platform == ACP63) {
			links[i].platforms = platform_acp63_component;
			links[i].num_platforms = ARRAY_SIZE(platform_acp63_component);
		} else if (drv_data->platform == ACP70) {
			links[i].platforms = platform_acp70_component;
			links[i].num_platforms = ARRAY_SIZE(platform_acp70_component);
		} else {
			links[i].platforms = platform_component;
			links[i].num_platforms = ARRAY_SIZE(platform_component);
		}
		links[i].ops = &acp_card_dmic_ops;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = num_links;
	card->set_bias_level = acp_rtk_set_bias_level;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_legacy_dai_links_create, SND_SOC_AMD_MACH);

MODULE_DESCRIPTION("AMD ACP Common Machine driver");
MODULE_LICENSE("GPL v2");
