/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>

#include "fsl_esai.h"

#define CODEC_CLK_EXTER_OSC   1
#define CODEC_CLK_ESAI_HCKT   2
#define SUPPORT_RATE_NUM    10

struct imx_priv {
	unsigned int mclk_freq;
	struct platform_device *pdev;
	struct platform_device *asrc_pdev;
	u32 asrc_rate;
	u32 asrc_format;
};

static struct imx_priv card_priv;

static int imx_cs42888_surround_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct imx_priv *priv = &card_priv;
	u32 dai_format = 0;

	dai_format = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
		     SND_SOC_DAIFMT_CBS_CFS;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_set_sysclk(cpu_dai, ESAI_HCKT_EXTAL,
			       priv->mclk_freq, SND_SOC_CLOCK_OUT);
	else
		snd_soc_dai_set_sysclk(cpu_dai, ESAI_HCKR_EXTAL,
			       priv->mclk_freq, SND_SOC_CLOCK_OUT);
	snd_soc_dai_set_sysclk(codec_dai, 0, priv->mclk_freq, SND_SOC_CLOCK_IN);

	/* set cpu DAI configuration */
	snd_soc_dai_set_fmt(cpu_dai, dai_format);
	/* set i.MX active slot mask */
	snd_soc_dai_set_tdm_slot(cpu_dai, 0x3, 0x3, 2, 32);

	/* set codec DAI configuration */
	snd_soc_dai_set_fmt(codec_dai, dai_format);
	return 0;
}

static int imx_cs42888_surround_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	static struct snd_pcm_hw_constraint_list constraint_rates;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	static u32 support_rates[SUPPORT_RATE_NUM];
	int ret;

	if (priv->mclk_freq == 24576000) {
		support_rates[0] = 32000;
		support_rates[1] = 48000;
		support_rates[2] = 64000;
		support_rates[3] = 96000;
		support_rates[4] = 192000;
		constraint_rates.list = support_rates;
		constraint_rates.count = 5;

		ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
							&constraint_rates);
		if (ret)
			return ret;
	} else
		dev_warn(dev, "mclk may be not supported %d\n", priv->mclk_freq);

	return 0;
}

static struct snd_soc_ops imx_cs42888_surround_ops = {
	.startup = imx_cs42888_surround_startup,
	.hw_params = imx_cs42888_surround_hw_params,
};

/**
 * imx_cs42888_surround_startup() is to set constrain for hw parameter, but
 * backend use same runtime as frontend, for p2p backend need to use different
 * parameter, so backend can't use the startup.
 */
static struct snd_soc_ops imx_cs42888_surround_ops_be = {
	.hw_params = imx_cs42888_surround_hw_params,
};


static const struct snd_soc_dapm_widget imx_cs42888_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Line out jack */
	{"Line Out Jack", NULL, "AOUT1L"},
	{"Line Out Jack", NULL, "AOUT1R"},
	{"Line Out Jack", NULL, "AOUT2L"},
	{"Line Out Jack", NULL, "AOUT2R"},
	{"Line Out Jack", NULL, "AOUT3L"},
	{"Line Out Jack", NULL, "AOUT3R"},
	{"Line Out Jack", NULL, "AOUT4L"},
	{"Line Out Jack", NULL, "AOUT4R"},
	{"AIN1L", NULL, "Line In Jack"},
	{"AIN1R", NULL, "Line In Jack"},
	{"AIN2L", NULL, "Line In Jack"},
	{"AIN2R", NULL, "Line In Jack"},
	{"CPU-Playback",  NULL, "ASRC-Playback"},
	{"Playback",  NULL, "CPU-Playback"},/* dai route for be and fe */
	{"ASRC-Capture",  NULL, "CPU-Capture"},
	{"CPU-Capture",  NULL, "Capture"},
};

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params) {

	struct imx_priv *priv = &card_priv;
	struct snd_interval *rate;
	struct snd_mask *mask;

	if (!priv->asrc_pdev)
		return -EINVAL;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = rate->min = priv->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set(mask, priv->asrc_format);

	return 0;
}

static struct snd_soc_dai_link imx_cs42888_dai[] = {
	{
		.name = "HiFi",
		.stream_name = "HiFi",
		.codec_dai_name = "cs42888",
		.ops = &imx_cs42888_surround_ops,
		.ignore_pmdown_time = 1,
	},
	{
		.name = "HiFi-ASRC-FE",
		.stream_name = "HiFi-ASRC-FE",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dynamic = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "HiFi-ASRC-BE",
		.stream_name = "HiFi-ASRC-BE",
		.codec_dai_name = "cs42888",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &imx_cs42888_surround_ops_be,
		.be_hw_params_fixup = be_hw_params_fixup,
	},
};

static struct snd_soc_card snd_soc_card_imx_cs42888 = {
	.name = "cs42888-audio",
	.dai_link = imx_cs42888_dai,
	.dapm_widgets = imx_cs42888_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(imx_cs42888_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

/*
 * This function will register the snd_soc_pcm_link drivers.
 */
static int imx_cs42888_probe(struct platform_device *pdev)
{
	struct device_node *esai_np, *codec_np;
	struct device_node *asrc_np;
	struct platform_device *esai_pdev;
	struct platform_device *asrc_pdev = NULL;
	struct i2c_client *codec_dev;
	struct imx_priv *priv = &card_priv;
	struct clk *codec_clk = NULL;
	int ret;
	u32 width;

	priv->pdev = pdev;
	priv->asrc_pdev = NULL;

	esai_np = of_parse_phandle(pdev->dev.of_node, "esai-controller", 0);
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!esai_np || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	asrc_np = of_parse_phandle(pdev->dev.of_node, "asrc-controller", 0);
	if (asrc_np) {
		asrc_pdev = of_find_device_by_node(asrc_np);
		priv->asrc_pdev = asrc_pdev;
	}

	esai_pdev = of_find_device_by_node(esai_np);
	if (!esai_pdev) {
		dev_err(&pdev->dev, "failed to find ESAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}
	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "failed to find codec platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	/*if there is no asrc controller, we only enable one device*/
	if (!asrc_pdev) {
		imx_cs42888_dai[0].codec_of_node   = codec_np;
		imx_cs42888_dai[0].cpu_dai_name    = dev_name(&esai_pdev->dev);
		imx_cs42888_dai[0].platform_of_node = esai_np;
		snd_soc_card_imx_cs42888.num_links = 1;
	} else {
		imx_cs42888_dai[0].codec_of_node   = codec_np;
		imx_cs42888_dai[0].cpu_dai_name    = dev_name(&esai_pdev->dev);
		imx_cs42888_dai[0].platform_of_node = esai_np;
		imx_cs42888_dai[1].cpu_of_node    = asrc_np;
		imx_cs42888_dai[1].platform_of_node   = asrc_np;
		imx_cs42888_dai[2].codec_of_node   = codec_np;
		imx_cs42888_dai[2].cpu_dai_name    = dev_name(&esai_pdev->dev);
		snd_soc_card_imx_cs42888.num_links = 3;

		ret = of_property_read_u32(asrc_np, "fsl,asrc-rate",
						&priv->asrc_rate);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto fail;
		}

		ret = of_property_read_u32(asrc_np, "fsl,asrc-width", &width);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto fail;
		}

		if (width == 24)
			priv->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
		else
			priv->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
	}

	codec_clk = devm_clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(codec_clk)) {
		ret = PTR_ERR(codec_clk);
		dev_err(&codec_dev->dev, "failed to get codec clk: %d\n", ret);
		goto fail;
	}
	priv->mclk_freq = clk_get_rate(codec_clk);

	snd_soc_card_imx_cs42888.dev = &pdev->dev;

	platform_set_drvdata(pdev, &snd_soc_card_imx_cs42888);

	ret = snd_soc_register_card(&snd_soc_card_imx_cs42888);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
fail:
	if (asrc_np)
		of_node_put(asrc_np);
	if (esai_np)
		of_node_put(esai_np);
	if (codec_np)
		of_node_put(codec_np);
	return ret;
}

static int imx_cs42888_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&snd_soc_card_imx_cs42888);
	return 0;
}

static const struct of_device_id imx_cs42888_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-cs42888", },
	{ /* sentinel */ }
};

static struct platform_driver imx_cs42888_driver = {
	.probe = imx_cs42888_probe,
	.remove = imx_cs42888_remove,
	.driver = {
		.name = "imx-cs42888",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_cs42888_dt_ids,
	},
};
module_platform_driver(imx_cs42888_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("ALSA SoC cs42888 Machine Layer Driver");
MODULE_ALIAS("platform:imx-cs42888");
MODULE_LICENSE("GPL");
