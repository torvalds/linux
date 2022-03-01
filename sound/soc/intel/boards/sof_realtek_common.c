// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.

#include <linux/device.h>
#include <linux/kernel.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/sof.h>
#include <uapi/sound/asound.h>
#include "../../codecs/rt1011.h"
#include "../../codecs/rt1015.h"
#include "../../codecs/rt1308.h"
#include "sof_realtek_common.h"

/*
 * Current only 2-amp configuration is supported for rt1011
 */
static const struct snd_soc_dapm_route speaker_map_lr[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left SPO" },
	{ "Right Spk", NULL, "Right SPO" },
};

/*
 * Make sure device's Unique ID follows this configuration:
 *
 * Two speakers:
 *         0: left, 1: right
 * Four speakers:
 *         0: Woofer left, 1: Woofer right
 *         2: Tweeter left, 3: Tweeter right
 */
static struct snd_soc_codec_conf rt1011_codec_confs[] = {
	{
		.dlc = COMP_CODEC_CONF(RT1011_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(RT1011_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_dai_link_component rt1011_dai_link_components[] = {
	{
		.name = RT1011_DEV0_NAME,
		.dai_name = RT1011_CODEC_DAI,
	},
	{
		.name = RT1011_DEV1_NAME,
		.dai_name = RT1011_CODEC_DAI,
	},
};

static const struct {
	unsigned int tx;
	unsigned int rx;
} rt1011_tdm_mask[] = {
	{.tx = 0x4, .rx = 0x1},
	{.tx = 0x8, .rx = 0x2},
};

static int rt1011_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int srate, i, ret = 0;

	srate = params_rate(params);

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		/* 100 Fs to drive 24 bit data */
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1011_PLL1_S_BCLK,
					  100 * srate, 256 * srate);
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set pll, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1011_FS_SYS_PRE_S_PLL1,
					     256 * srate, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set sysclk, ret %d\n",
				ret);
			return ret;
		}

		if (i >= ARRAY_SIZE(rt1011_tdm_mask)) {
			dev_err(codec_dai->dev, "invalid codec index %d\n",
				i);
			return -ENODEV;
		}

		ret = snd_soc_dai_set_tdm_slot(codec_dai, rt1011_tdm_mask[i].tx,
					       rt1011_tdm_mask[i].rx, 4,
					       params_width(params));
		if (ret < 0) {
			dev_err(codec_dai->dev, "fail to set tdm slot, ret %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops rt1011_ops = {
	.hw_params = rt1011_hw_params,
};

static int rt1011_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, speaker_map_lr,
				      ARRAY_SIZE(speaker_map_lr));
	if (ret)
		dev_err(rtd->dev, "Speaker map addition failed: %d\n", ret);
	return ret;
}

void sof_rt1011_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = rt1011_dai_link_components;
	link->num_codecs = ARRAY_SIZE(rt1011_dai_link_components);
	link->init = rt1011_init;
	link->ops = &rt1011_ops;
}
EXPORT_SYMBOL_NS(sof_rt1011_dai_link, SND_SOC_INTEL_SOF_REALTEK_COMMON);

void sof_rt1011_codec_conf(struct snd_soc_card *card)
{
	card->codec_conf = rt1011_codec_confs;
	card->num_configs = ARRAY_SIZE(rt1011_codec_confs);
}
EXPORT_SYMBOL_NS(sof_rt1011_codec_conf, SND_SOC_INTEL_SOF_REALTEK_COMMON);

/*
 * rt1015:  i2c mode driver for ALC1015 and ALC1015Q
 * rt1015p: auto-mode driver for ALC1015, ALC1015Q, and ALC1015Q-VB
 *
 * For stereo output, there are always two amplifiers on the board.
 * However, the ACPI implements only one device instance (UID=0) if they
 * are sharing the same enable pin. The code will detect the number of
 * device instance and use corresponding DAPM structures for
 * initialization.
 */
static const struct snd_soc_dapm_route rt1015p_1dev_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "Speaker" },
	{ "Right Spk", NULL, "Speaker" },
};

static const struct snd_soc_dapm_route rt1015p_2dev_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left Speaker" },
	{ "Right Spk", NULL, "Right Speaker" },
};

static struct snd_soc_codec_conf rt1015p_codec_confs[] = {
	{
		.dlc = COMP_CODEC_CONF(RT1015P_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(RT1015P_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_dai_link_component rt1015p_dai_link_components[] = {
	{
		.name = RT1015P_DEV0_NAME,
		.dai_name = RT1015P_CODEC_DAI,
	},
	{
		.name = RT1015P_DEV1_NAME,
		.dai_name = RT1015P_CODEC_DAI,
	},
};

static int rt1015p_get_num_codecs(void)
{
	static int dev_num;

	if (dev_num)
		return dev_num;

	if (!acpi_dev_present("RTL1015", "1", -1))
		dev_num = 1;
	else
		dev_num = 2;

	return dev_num;
}

static int rt1015p_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	/* reserved for debugging purpose */

	return 0;
}

static const struct snd_soc_ops rt1015p_ops = {
	.hw_params = rt1015p_hw_params,
};

static int rt1015p_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	if (rt1015p_get_num_codecs() == 1)
		ret = snd_soc_dapm_add_routes(&card->dapm, rt1015p_1dev_dapm_routes,
					      ARRAY_SIZE(rt1015p_1dev_dapm_routes));
	else
		ret = snd_soc_dapm_add_routes(&card->dapm, rt1015p_2dev_dapm_routes,
					      ARRAY_SIZE(rt1015p_2dev_dapm_routes));
	if (ret)
		dev_err(rtd->dev, "Speaker map addition failed: %d\n", ret);
	return ret;
}

void sof_rt1015p_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = rt1015p_dai_link_components;
	link->num_codecs = rt1015p_get_num_codecs();
	link->init = rt1015p_init;
	link->ops = &rt1015p_ops;
}
EXPORT_SYMBOL_NS(sof_rt1015p_dai_link, SND_SOC_INTEL_SOF_REALTEK_COMMON);

void sof_rt1015p_codec_conf(struct snd_soc_card *card)
{
	if (rt1015p_get_num_codecs() == 1)
		return;

	card->codec_conf = rt1015p_codec_confs;
	card->num_configs = ARRAY_SIZE(rt1015p_codec_confs);
}
EXPORT_SYMBOL_NS(sof_rt1015p_codec_conf, SND_SOC_INTEL_SOF_REALTEK_COMMON);

/*
 * RT1015 audio amplifier
 */

static int rt1015_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int i, fs = 64, ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1015_PLL_S_BCLK,
					  params_rate(params) * fs,
					  params_rate(params) * 256);
		if (ret)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1015_SCLK_S_PLL,
					     params_rate(params) * 256,
					     SND_SOC_CLOCK_IN);
		if (ret)
			return ret;
	}

	return 0;
}

static int rt1015_hw_params_pll_and_tdm(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	int i, fs = 100, ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1015_PLL_S_BCLK,
					  params_rate(params) * fs,
					  params_rate(params) * 256);
		if (ret)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, RT1015_SCLK_S_PLL,
					     params_rate(params) * 256,
					     SND_SOC_CLOCK_IN);
		if (ret)
			return ret;
	}
	/* rx slot 1 for RT1015_DEV0_NAME */
	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_codec(rtd, 0),
				       0x0, 0x1, 4, 24);
	if (ret)
		return ret;

	/* rx slot 2 for RT1015_DEV1_NAME */
	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_codec(rtd, 1),
				       0x0, 0x2, 4, 24);
	if (ret)
		return ret;

	return 0;
}

static struct snd_soc_ops rt1015_ops = {
	.hw_params = rt1015_hw_params,
};

static struct snd_soc_codec_conf rt1015_amp_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(RT1015_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(RT1015_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_dai_link_component rt1015_components[] = {
	{
		.name = RT1015_DEV0_NAME,
		.dai_name = RT1015_CODEC_DAI,
	},
	{
		.name = RT1015_DEV1_NAME,
		.dai_name = RT1015_CODEC_DAI,
	},
};

static int speaker_codec_init_lr(struct snd_soc_pcm_runtime *rtd)
{
	return snd_soc_dapm_add_routes(&rtd->card->dapm, speaker_map_lr,
					ARRAY_SIZE(speaker_map_lr));
}

void sof_rt1015_codec_conf(struct snd_soc_card *card)
{
	card->codec_conf = rt1015_amp_conf;
	card->num_configs = ARRAY_SIZE(rt1015_amp_conf);
}
EXPORT_SYMBOL_NS(sof_rt1015_codec_conf, SND_SOC_INTEL_SOF_REALTEK_COMMON);

void sof_rt1015_dai_link(struct snd_soc_dai_link *link, unsigned int fs)
{
	link->codecs = rt1015_components;
	link->num_codecs = ARRAY_SIZE(rt1015_components);
	link->init = speaker_codec_init_lr;
	link->ops = &rt1015_ops;

	if (fs == 100)
		rt1015_ops.hw_params = rt1015_hw_params_pll_and_tdm;
}
EXPORT_SYMBOL_NS(sof_rt1015_dai_link, SND_SOC_INTEL_SOF_REALTEK_COMMON);

/*
 * RT1308 audio amplifier
 */
static const struct snd_kcontrol_new rt1308_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Speakers"),
};

static const struct snd_soc_dapm_widget rt1308_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speakers", NULL),
};

static const struct snd_soc_dapm_route rt1308_dapm_routes[] = {
	/* speaker */
	{"Speakers", NULL, "SPOL"},
	{"Speakers", NULL, "SPOR"},
};

static struct snd_soc_dai_link_component rt1308_components[] = {
	{
		.name = RT1308_DEV0_NAME,
		.dai_name = RT1308_CODEC_DAI,
	}
};

static int rt1308_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, rt1308_dapm_widgets,
					ARRAY_SIZE(rt1308_dapm_widgets));
	if (ret) {
		dev_err(rtd->dev, "fail to add dapm controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, rt1308_kcontrols,
					ARRAY_SIZE(rt1308_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "fail to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, rt1308_dapm_routes,
				      ARRAY_SIZE(rt1308_dapm_routes));

	if (ret)
		dev_err(rtd->dev, "fail to add dapm routes, ret %d\n", ret);

	return ret;
}

static int rt1308_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int clk_id, clk_freq, pll_out;
	int ret;

	clk_id = RT1308_PLL_S_MCLK;
	/* get the tplg configured mclk. */
	clk_freq = sof_dai_get_mclk(rtd);

	pll_out = params_rate(params) * 512;

	/* Set rt1308 pll */
	ret = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, pll_out);
	if (ret < 0) {
		dev_err(card->dev, "Failed to set RT1308 PLL: %d\n", ret);
		return ret;
	}

	/* Set rt1308 sysclk */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT1308_FS_SYS_S_PLL, pll_out,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(card->dev, "Failed to set RT1308 SYSCLK: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops rt1308_ops = {
	.hw_params = rt1308_hw_params,
};

void sof_rt1308_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = rt1308_components;
	link->num_codecs = ARRAY_SIZE(rt1308_components);
	link->init = rt1308_init;
	link->ops = &rt1308_ops;
}
EXPORT_SYMBOL_NS(sof_rt1308_dai_link, SND_SOC_INTEL_SOF_REALTEK_COMMON);

MODULE_DESCRIPTION("ASoC Intel SOF Realtek helpers");
MODULE_LICENSE("GPL");
