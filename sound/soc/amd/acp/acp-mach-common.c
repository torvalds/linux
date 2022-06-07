// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
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
#include "acp-mach.h"

#define PCO_PLAT_CLK 48000000
#define RT5682_PLL_FREQ (48000 * 512)
#define DUAL_CHANNEL	2
#define FOUR_CHANNEL	4

static struct snd_soc_jack pco_jack;

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

static int acp_clk_enable(struct acp_card_drvdata *drvdata)
{
	clk_set_rate(drvdata->wclk, 48000);
	clk_set_rate(drvdata->bclk, 48000 * 64);

	return clk_prepare_enable(drvdata->wclk);
}

/* Declare RT5682 codec components */
SND_SOC_DAILINK_DEF(rt5682,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00", "rt5682-aif1")));

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
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != RT5682)
		return -EINVAL;

	ret =  snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				   | SND_SOC_DAIFMT_CBP_CFP);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
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

	/* Set tdm/i2s1 master bclk ratio */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, 64);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set rt5682 tdm bclk ratio: %d\n", ret);
		return ret;
	}

	drvdata->wclk = clk_get(component->dev, "rt5682-dai-wclk");
	drvdata->bclk = clk_get(component->dev, "rt5682-dai-bclk");

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_LINEOUT |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &pco_jack);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &pco_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, rt5682_map, ARRAY_SIZE(rt5682_map));
}

static int acp_card_hs_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
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

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				      &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &constraints_rates);
	if (!drvdata->soc_mclk) {
		ret = acp_clk_enable(drvdata);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to enable HS clk: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static void acp_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;

	if (!drvdata->soc_mclk)
		clk_disable_unprepare(drvdata->wclk);
}

static const struct snd_soc_ops acp_card_rt5682_ops = {
	.startup = acp_card_hs_startup,
	.shutdown = acp_card_shutdown,
};

/* Define RT5682S CODEC component*/
SND_SOC_DAILINK_DEF(rt5682s,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-RTL5682:00", "rt5682s-aif1")));

static const struct snd_soc_dapm_route rt5682s_map[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
};

static int acp_card_rt5682s_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	unsigned int fmt;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != RT5682S)
		return -EINVAL;

	if (drvdata->soc_mclk)
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
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

	/* Set tdm/i2s1 master bclk ratio */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, 64);
	if (ret < 0) {
		dev_err(rtd->dev, "Failed to set rt5682 tdm bclk ratio: %d\n", ret);
		return ret;
	}

	if (!drvdata->soc_mclk) {
		drvdata->wclk = clk_get(component->dev, "rt5682-dai-wclk");
		drvdata->bclk = clk_get(component->dev, "rt5682-dai-bclk");
	}

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_LINEOUT |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &pco_jack);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &pco_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, rt5682s_map, ARRAY_SIZE(rt5682s_map));
}

static const struct snd_soc_ops acp_card_rt5682s_ops = {
	.startup = acp_card_hs_startup,
	.shutdown = acp_card_shutdown,
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
SND_SOC_DAILINK_DEF(rt1019_1,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC1019:02", "rt1019-aif"),
		    COMP_CODEC("i2c-10EC1019:01", "rt1019-aif")));

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

static struct snd_soc_codec_conf rt1019_1_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1019:02"),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1019:01"),
		.name_prefix = "Right",
	},
};

static int acp_card_rt1019_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;

	if (drvdata->amp_codec_id != RT1019)
		return -EINVAL;

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
	int srate, i, ret = 0;

	srate = params_rate(params);

	if (drvdata->amp_codec_id != RT1019)
		return -EINVAL;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (strcmp(codec_dai->name, "rt1019-aif"))
			continue;

		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1019_PLL_S_BCLK,
					  64 * srate, 256 * srate);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1019_SCLK_S_PLL,
					     256 * srate, SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int acp_card_amp_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	int ret = 0;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				      &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &constraints_rates);

	if (!drvdata->soc_mclk) {
		ret = acp_clk_enable(drvdata);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to enable AMP clk: %d\n", ret);
			return ret;
		}
	}
	return ret;
}

static const struct snd_soc_ops acp_card_rt1019_ops = {
	.startup = acp_card_amp_startup,
	.shutdown = acp_card_shutdown,
	.hw_params = acp_card_rt1019_hw_params,
};

/* Declare Maxim codec components */
SND_SOC_DAILINK_DEF(max98360a,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98360A:00", "HiFi")));

static const struct snd_soc_dapm_route max98360a_map[] = {
	{"Spk", NULL, "Speaker"},
};

static int acp_card_maxim_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;

	if (drvdata->amp_codec_id != MAX98360A)
		return -EINVAL;

	return snd_soc_dapm_add_routes(&rtd->card->dapm, max98360a_map,
				       ARRAY_SIZE(max98360a_map));
}

static const struct snd_soc_ops acp_card_maxim_ops = {
	.startup = acp_card_amp_startup,
	.shutdown = acp_card_shutdown,
};

/* Declare nau8825 codec components */
SND_SOC_DAILINK_DEF(nau8825,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10508825:00", "nau8825-hifi")));

static const struct snd_soc_dapm_route nau8825_map[] = {
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },
};

static int acp_card_nau8825_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct acp_card_drvdata *drvdata = card->drvdata;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	unsigned int fmt;
	int ret;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	if (drvdata->hs_codec_id != NAU8825)
		return -EINVAL;

	if (drvdata->soc_mclk)
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	else
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	ret =  snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Failed to set dai fmt: %d\n", ret);
		return ret;
	}
	ret = snd_soc_card_jack_new(card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 &pco_jack);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &pco_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return snd_soc_dapm_add_routes(&rtd->card->dapm, nau8825_map, ARRAY_SIZE(nau8825_map));
}

static int acp_nau8825_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

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

/* Declare DMIC codec components */
SND_SOC_DAILINK_DEF(dmic_codec,
		DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

/* Declare ACP CPU components */
static struct snd_soc_dai_link_component dummy_codec[] = {
	{
		.name = "snd-soc-dummy",
		.dai_name = "snd-soc-dummy-dai",
	}
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		 .name = "acp_asoc_renoir.0",
	}
};

static struct snd_soc_dai_link_component sof_component[] = {
	{
		 .name = "0000:04:00.5",
	}
};

SND_SOC_DAILINK_DEF(i2s_sp,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-i2s-sp")));
SND_SOC_DAILINK_DEF(sof_sp,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-sp")));
SND_SOC_DAILINK_DEF(sof_hs,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-hs")));
SND_SOC_DAILINK_DEF(sof_dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-sof-dmic")));
SND_SOC_DAILINK_DEF(pdm_dmic,
	DAILINK_COMP_ARRAY(COMP_CPU("acp-pdm-dmic")));

int acp_sofdsp_dai_links_create(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *links;
	struct device *dev = card->dev;
	struct acp_card_drvdata *drv_data = card->drvdata;
	int i = 0, num_links = 0;

	if (drv_data->hs_cpu_id)
		num_links++;
	if (drv_data->amp_cpu_id)
		num_links++;
	if (drv_data->dmic_cpu_id)
		num_links++;

	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) * num_links, GFP_KERNEL);
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
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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
		links[i].cpus = sof_sp;
		links[i].num_cpus = ARRAY_SIZE(sof_sp);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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
		links[i].cpus = sof_hs;
		links[i].num_cpus = ARRAY_SIZE(sof_hs);
		links[i].platforms = sof_component;
		links[i].num_platforms = ARRAY_SIZE(sof_component);
		links[i].dpcm_playback = 1;
		links[i].nonatomic = true;
		links[i].no_pcm = 1;
		if (!drv_data->amp_codec_id) {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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
			links[i].codecs = rt1019_1;
			links[i].num_codecs = ARRAY_SIZE(rt1019_1);
			card->codec_conf = rt1019_1_conf;
			card->num_configs = ARRAY_SIZE(rt1019_1_conf);
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

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_sofdsp_dai_links_create, SND_SOC_AMD_MACH);

int acp_legacy_dai_links_create(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *links;
	struct device *dev = card->dev;
	struct acp_card_drvdata *drv_data = card->drvdata;
	int i = 0, num_links = 0;

	if (drv_data->hs_cpu_id)
		num_links++;
	if (drv_data->amp_cpu_id)
		num_links++;
	if (drv_data->dmic_cpu_id)
		num_links++;

	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) * num_links, GFP_KERNEL);
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
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
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

	if (drv_data->dmic_cpu_id == DMIC) {
		links[i].name = "acp-dmic-codec";
		links[i].id = DMIC_BE_ID;
		if (drv_data->dmic_codec_id == DMIC) {
			links[i].codecs = dmic_codec;
			links[i].num_codecs = ARRAY_SIZE(dmic_codec);
		} else {
			/* Use dummy codec if codec id not specified */
			links[i].codecs = dummy_codec;
			links[i].num_codecs = ARRAY_SIZE(dummy_codec);
		}
		links[i].cpus = pdm_dmic;
		links[i].num_cpus = ARRAY_SIZE(pdm_dmic);
		links[i].platforms = platform_component;
		links[i].num_platforms = ARRAY_SIZE(platform_component);
		links[i].ops = &acp_card_dmic_ops;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = num_links;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_legacy_dai_links_create, SND_SOC_AMD_MACH);

MODULE_LICENSE("GPL v2");
