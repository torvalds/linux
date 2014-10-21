/*
 * Copyright 2012, 2014 Freescale Semiconductor, Inc.
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

#define SUPPORT_RATE_NUM    10

struct imx_priv {
	unsigned int mclk_freq;
	struct platform_device *pdev;
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

static struct snd_soc_ops imx_mqs_ops = {
	.startup = imx_mqs_startup,
};



static struct snd_soc_dai_link imx_mqs_dai = {
	.name = "HiFi",
	.stream_name = "HiFi",
	.dai_fmt = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
	.ops = &imx_mqs_ops,
};

static struct snd_soc_card snd_soc_card_imx_mqs = {
	.name = "mqs-audio",
	.dai_link = &imx_mqs_dai,
	.owner = THIS_MODULE,
	.num_links = 1,
};

static int imx_mqs_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np, *codec_np;
	struct imx_priv *priv = &card_priv;
	struct clk *codec_clk = NULL;
	struct platform_device *codec_dev;
	int ret;

	priv->pdev = pdev;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	if (!cpu_np) {
		ret = -EINVAL;
		goto fail;
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


	imx_mqs_dai.cpu_of_node = cpu_np;
	imx_mqs_dai.platform_of_node = cpu_np;
	imx_mqs_dai.codec_dai_name = "fsl-mqs-dai";
	imx_mqs_dai.codec_of_node = codec_np;
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
