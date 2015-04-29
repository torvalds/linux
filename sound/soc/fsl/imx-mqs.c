/*
 * Copyright 2012, 2014-2015 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#define SUPPORT_RATE_NUM    10

struct imx_priv {
	unsigned int mclk_freq;
	struct platform_device *pdev;
	struct platform_device *asrc_pdev;
	u32 asrc_rate;
	u32 asrc_format;
};

static struct imx_priv card_priv;

static int imx_mqs_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	static struct snd_pcm_hw_constraint_list constraint_rates;
	struct imx_priv *priv = &card_priv;
	struct device *dev = &priv->pdev->dev;
	static u32 support_rates[SUPPORT_RATE_NUM];
	int ret;

	if (priv->mclk_freq == 24576000) {
		support_rates[0] = 48000;
		support_rates[1] = 96000;
		support_rates[2] = 192000;
		constraint_rates.list = support_rates;
		constraint_rates.count = 3;

		ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
							&constraint_rates);
		if (ret)
			return ret;
	} else
		dev_warn(dev, "mclk may be not supported %d\n", priv->mclk_freq);

	return 0;
}

static int imx_mqs_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0, 0, 2, params_width(params));
	if (ret) {
		dev_err(card->dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}
	return 0;
}

static struct snd_soc_ops imx_mqs_ops = {
	.startup = imx_mqs_startup,
	.hw_params = imx_mqs_hw_params,
};

static struct snd_soc_ops imx_mqs_ops_be = {
	.hw_params = imx_mqs_hw_params,
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"CPU-Playback",  NULL, "ASRC-Playback"},
	{"Playback",  NULL, "CPU-Playback"},/* dai route for be and fe */
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

static struct snd_soc_dai_link imx_mqs_dai[] = {
	{
		.name = "HiFi",
		.stream_name = "HiFi",
		.dai_fmt = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.ops = &imx_mqs_ops,
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
		.codec_dai_name = "fsl-mqs-dai",
		.platform_name = "snd-soc-dummy",
		.dai_fmt = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.no_pcm = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &imx_mqs_ops_be,
		.be_hw_params_fixup = be_hw_params_fixup,
	},
};

static struct snd_soc_card snd_soc_card_imx_mqs = {
	.name = "mqs-audio",
	.dai_link = imx_mqs_dai,
	.owner = THIS_MODULE,
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static int imx_mqs_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np, *codec_np;
	struct imx_priv *priv = &card_priv;
	struct clk *codec_clk = NULL;
	struct platform_device *codec_dev;
	struct platform_device *asrc_pdev = NULL;
	struct platform_device *cpu_pdev;
	struct device_node *asrc_np;
	int ret;
	u32 width;

	priv->pdev = pdev;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	if (!cpu_np) {
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find cpu dai device\n");
		ret = -EINVAL;
		goto fail;
	}

	asrc_np = of_parse_phandle(pdev->dev.of_node, "asrc-controller", 0);
	if (asrc_np) {
		asrc_pdev = of_find_device_by_node(asrc_np);
		priv->asrc_pdev = asrc_pdev;
	}

	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_dev = of_find_device_by_node(codec_np);
	if (!codec_dev) {
		dev_err(&codec_dev->dev, "failed to find codec device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_clk = devm_clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(codec_clk)) {
		ret = PTR_ERR(codec_clk);
		dev_err(&codec_dev->dev, "failed to get codec clk: %d\n", ret);
		goto fail;
	}
	priv->mclk_freq = clk_get_rate(codec_clk);

	imx_mqs_dai[0].codec_dai_name = "fsl-mqs-dai";

	if (!asrc_pdev) {
		imx_mqs_dai[0].codec_of_node    = codec_np;
		imx_mqs_dai[0].cpu_dai_name     = dev_name(&cpu_pdev->dev);
		imx_mqs_dai[0].platform_of_node = cpu_np;
		snd_soc_card_imx_mqs.num_links = 1;
	} else {
		imx_mqs_dai[0].codec_of_node    = codec_np;
		imx_mqs_dai[0].cpu_dai_name     = dev_name(&cpu_pdev->dev);
		imx_mqs_dai[0].platform_of_node = cpu_np;
		imx_mqs_dai[1].cpu_of_node      = asrc_np;
		imx_mqs_dai[1].platform_of_node = asrc_np;
		imx_mqs_dai[2].codec_of_node    = codec_np;
		imx_mqs_dai[2].cpu_dai_name     = dev_name(&cpu_pdev->dev);
		snd_soc_card_imx_mqs.num_links = 3;

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

	snd_soc_card_imx_mqs.dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_imx_mqs);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

fail:
	if (cpu_np)
		of_node_put(cpu_np);

	return ret;
}

static const struct of_device_id imx_mqs_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-mqs", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_mqs_dt_ids);

static struct platform_driver imx_mqs_driver = {
	.driver = {
		.name = "imx-mqs",
		.of_match_table = imx_mqs_dt_ids,
	},
	.probe = imx_mqs_probe,
};
module_platform_driver(imx_mqs_driver);

MODULE_AUTHOR("Nicolin Chen <Guangyu.Chen@freescale.com>");
MODULE_DESCRIPTION("Freescale i.MX MQS ASoC machine driver");
MODULE_ALIAS("platform:imx-mqs");
MODULE_LICENSE("GPL v2");
