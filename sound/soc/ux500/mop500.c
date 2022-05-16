// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja (ola.o.lilja@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 */

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include <sound/soc.h>
#include <sound/initval.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"

#include "mop500_ab8500.h"

/* Define the whole MOP500 soundcard, linking platform to the codec-drivers  */
SND_SOC_DAILINK_DEFS(link1,
	DAILINK_COMP_ARRAY(COMP_CPU("ux500-msp-i2s.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("ab8500-codec.0", "ab8500-codec-dai.0")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("ux500-msp-i2s.1")));

SND_SOC_DAILINK_DEFS(link2,
	DAILINK_COMP_ARRAY(COMP_CPU("ux500-msp-i2s.3")),
	DAILINK_COMP_ARRAY(COMP_CODEC("ab8500-codec.0", "ab8500-codec-dai.1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("ux500-msp-i2s.3")));

static struct snd_soc_dai_link mop500_dai_links[] = {
	{
		.name = "ab8500_0",
		.stream_name = "ab8500_0",
		.init = mop500_ab8500_machine_init,
		.ops = mop500_ab8500_ops,
		SND_SOC_DAILINK_REG(link1),
	},
	{
		.name = "ab8500_1",
		.stream_name = "ab8500_1",
		.init = NULL,
		.ops = mop500_ab8500_ops,
		SND_SOC_DAILINK_REG(link2),
	},
};

static struct snd_soc_card mop500_card = {
	.name = "MOP500-card",
	.owner = THIS_MODULE,
	.probe = NULL,
	.dai_link = mop500_dai_links,
	.num_links = ARRAY_SIZE(mop500_dai_links),
};

static void mop500_of_node_put(void)
{
	int i;

	for (i = 0; i < 2; i++)
		of_node_put(mop500_dai_links[i].cpus->of_node);

	/* Both links use the same codec, which is refcounted only once */
	of_node_put(mop500_dai_links[0].codecs->of_node);
}

static int mop500_of_probe(struct platform_device *pdev,
			   struct device_node *np)
{
	struct device_node *codec_np, *msp_np[2];
	int i;

	msp_np[0] = of_parse_phandle(np, "stericsson,cpu-dai", 0);
	msp_np[1] = of_parse_phandle(np, "stericsson,cpu-dai", 1);
	codec_np  = of_parse_phandle(np, "stericsson,audio-codec", 0);

	if (!(msp_np[0] && msp_np[1] && codec_np)) {
		dev_err(&pdev->dev, "Phandle missing or invalid\n");
		for (i = 0; i < 2; i++)
			of_node_put(msp_np[i]);
		of_node_put(codec_np);
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		mop500_dai_links[i].cpus->of_node = msp_np[i];
		mop500_dai_links[i].cpus->dai_name = NULL;
		mop500_dai_links[i].platforms->of_node = msp_np[i];
		mop500_dai_links[i].platforms->name = NULL;
		mop500_dai_links[i].codecs->of_node = codec_np;
		mop500_dai_links[i].codecs->name = NULL;
	}

	snd_soc_of_parse_card_name(&mop500_card, "stericsson,card-name");

	return 0;
}

static int mop500_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	dev_dbg(&pdev->dev, "%s: Enter.\n", __func__);

	mop500_card.dev = &pdev->dev;

	if (np) {
		ret = mop500_of_probe(pdev, np);
		if (ret)
			return ret;
	}

	dev_dbg(&pdev->dev, "%s: Card %s: Set platform drvdata.\n",
		__func__, mop500_card.name);

	snd_soc_card_set_drvdata(&mop500_card, NULL);

	dev_dbg(&pdev->dev, "%s: Card %s: num_links = %d\n",
		__func__, mop500_card.name, mop500_card.num_links);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: name = %s\n",
		__func__, mop500_card.name, mop500_card.dai_link[0].name);
	dev_dbg(&pdev->dev, "%s: Card %s: DAI-link 0: stream_name = %s\n",
		__func__, mop500_card.name,
		mop500_card.dai_link[0].stream_name);

	ret = snd_soc_register_card(&mop500_card);
	if (ret)
		dev_err(&pdev->dev,
			"Error: snd_soc_register_card failed (%d)!\n", ret);

	return ret;
}

static int mop500_remove(struct platform_device *pdev)
{
	struct snd_soc_card *mop500_card = platform_get_drvdata(pdev);

	pr_debug("%s: Enter.\n", __func__);

	snd_soc_unregister_card(mop500_card);
	mop500_ab8500_remove(mop500_card);
	mop500_of_node_put();

	return 0;
}

static const struct of_device_id snd_soc_mop500_match[] = {
	{ .compatible = "stericsson,snd-soc-mop500", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_soc_mop500_match);

static struct platform_driver snd_soc_mop500_driver = {
	.driver = {
		.name = "snd-soc-mop500",
		.of_match_table = snd_soc_mop500_match,
	},
	.probe = mop500_probe,
	.remove = mop500_remove,
};

module_platform_driver(snd_soc_mop500_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC MOP500 board driver");
MODULE_AUTHOR("Ola Lilja");
