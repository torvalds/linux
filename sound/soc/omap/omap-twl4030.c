/*
 * omap-twl4030.c  --  SoC audio for TI SoC based boards with twl4030 codec
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This driver replaces the following machine drivers:
 * omap3beagle (Author: Steve Sakoman <steve@sakoman.com>)
 * omap3evm (Author: Anuj Aggarwal <anuj.aggarwal@ti.com>)
 * overo (Author: Steve Sakoman <steve@sakoman.com>)
 * igep0020 (Author: Enric Balletbo i Serra <eballetbo@iseebcn.com>)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/platform_device.h>
#include <linux/platform_data/omap-twl4030.h>
#include <linux/module.h>
#include <linux/of.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

static int omap_twl4030_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	unsigned int fmt;
	int ret;

	switch (params_channels(params)) {
	case 2: /* Stereo I2S mode */
		fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
		break;
	case 4: /* Four channel TDM mode */
		fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
		break;
	default:
		return -EINVAL;
	}

	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		dev_err(card->dev, "can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		dev_err(card->dev, "can't set cpu DAI configuration\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops omap_twl4030_ops = {
	.hw_params = omap_twl4030_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link omap_twl4030_dai_links[] = {
	{
		.name = "TWL4030",
		.stream_name = "TWL4030",
		.cpu_dai_name = "omap-mcbsp.2",
		.codec_dai_name = "twl4030-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "twl4030-codec",
		.ops = &omap_twl4030_ops,
	},
};

/* Audio machine driver */
static struct snd_soc_card omap_twl4030_card = {
	.owner = THIS_MODULE,
	.dai_link = omap_twl4030_dai_links,
	.num_links = ARRAY_SIZE(omap_twl4030_dai_links),
};

static __devinit int omap_twl4030_probe(struct platform_device *pdev)
{
	struct omap_tw4030_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;
	struct snd_soc_card *card = &omap_twl4030_card;
	int ret = 0;

	card->dev = &pdev->dev;

	if (node) {
		struct device_node *dai_node;

		if (snd_soc_of_parse_card_name(card, "ti,model")) {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}

		dai_node = of_parse_phandle(node, "ti,mcbsp", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node is not provided\n");
			return -EINVAL;
		}
		omap_twl4030_dai_links[0].cpu_dai_name  = NULL;
		omap_twl4030_dai_links[0].cpu_of_node = dai_node;

	} else if (pdata) {
		if (pdata->card_name) {
			card->name = pdata->card_name;
		} else {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}
	} else {
		dev_err(&pdev->dev, "Missing pdata\n");
		return -ENODEV;
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int __devexit omap_twl4030_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id omap_twl4030_of_match[] = {
	{.compatible = "ti,omap-twl4030", },
	{ },
};
MODULE_DEVICE_TABLE(of, omap_twl4030_of_match);

static struct platform_driver omap_twl4030_driver = {
	.driver = {
		.name = "omap-twl4030",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = omap_twl4030_of_match,
	},
	.probe = omap_twl4030_probe,
	.remove = __devexit_p(omap_twl4030_remove),
};

module_platform_driver(omap_twl4030_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("ALSA SoC for TI SoC based boards with twl4030 codec");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-twl4030");
