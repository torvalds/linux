// SPDX-License-Identifier: GPL-2.0
//
// dummy_codec.c  --  dummy audio codec for rockchip
//
// Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

struct dummy_codec_priv {
	struct snd_soc_component *component;
	struct clk *mclk;
};

static int dummy_codec_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct dummy_codec_priv *dcp = snd_soc_component_get_drvdata(dai->component);

	if (!IS_ERR(dcp->mclk))
		clk_prepare_enable(dcp->mclk);

	return 0;
}

static void dummy_codec_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct dummy_codec_priv *dcp = snd_soc_component_get_drvdata(dai->component);

	if (!IS_ERR(dcp->mclk))
		clk_disable_unprepare(dcp->mclk);
}

static struct snd_soc_dai_ops dummy_codec_dai_ops = {
	.startup	= dummy_codec_startup,
	.shutdown	= dummy_codec_shutdown,
};

struct snd_soc_dai_driver dummy_dai = {
	.name = "dummy_codec",
	.playback = {
		.stream_name = "Dummy Playback",
		.channels_min = 2,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "Dummy Capture",
		.channels_min = 2,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &dummy_codec_dai_ops,
};

static const struct snd_soc_component_driver soc_dummy_codec;

static int rockchip_dummy_codec_probe(struct platform_device *pdev)
{
	struct dummy_codec_priv *dcp;

	dcp = devm_kzalloc(&pdev->dev, sizeof(*dcp), GFP_KERNEL);
	if (!dcp)
		return -ENOMEM;

	platform_set_drvdata(pdev, dcp);

	/* optional mclk, if needs, assign mclk in dts node */
	dcp->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(dcp->mclk)) {
		if (PTR_ERR(dcp->mclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		else if (PTR_ERR(dcp->mclk) != -ENOENT)
			return -EINVAL;
	}

	return devm_snd_soc_register_component(&pdev->dev, &soc_dummy_codec,
					       &dummy_dai, 1);
}

static const struct of_device_id rockchip_dummy_codec_of_match[] = {
	{ .compatible = "rockchip,dummy-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_dummy_codec_of_match);

static struct platform_driver rockchip_dummy_codec_driver = {
	.driver = {
		.name = "dummy_codec",
		.of_match_table = of_match_ptr(rockchip_dummy_codec_of_match),
	},
	.probe = rockchip_dummy_codec_probe,
};

module_platform_driver(rockchip_dummy_codec_driver);

MODULE_AUTHOR("Sugar <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Dummy Codec Driver");
MODULE_LICENSE("GPL v2");
