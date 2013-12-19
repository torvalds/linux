/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja (ola.o.lilja@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
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
static struct snd_soc_dai_link mop500_dai_links[] = {
	{
		.name = "ab8500_0",
		.stream_name = "ab8500_0",
		.cpu_dai_name = "ux500-msp-i2s.1",
		.codec_dai_name = "ab8500-codec-dai.0",
		.platform_name = "ux500-msp-i2s.1",
		.codec_name = "ab8500-codec.0",
		.init = mop500_ab8500_machine_init,
		.ops = mop500_ab8500_ops,
	},
	{
		.name = "ab8500_1",
		.stream_name = "ab8500_1",
		.cpu_dai_name = "ux500-msp-i2s.3",
		.codec_dai_name = "ab8500-codec-dai.1",
		.platform_name = "ux500-msp-i2s.3",
		.codec_name = "ab8500-codec.0",
		.init = NULL,
		.ops = mop500_ab8500_ops,
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

	for (i = 0; i < 2; i++) {
		if (mop500_dai_links[i].cpu_of_node)
			of_node_put((struct device_node *)
				mop500_dai_links[i].cpu_of_node);
		if (mop500_dai_links[i].codec_of_node)
			of_node_put((struct device_node *)
				mop500_dai_links[i].codec_of_node);
	}
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
		mop500_of_node_put();
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		mop500_dai_links[i].cpu_of_node = msp_np[i];
		mop500_dai_links[i].cpu_dai_name = NULL;
		mop500_dai_links[i].codec_of_node = codec_np;
		mop500_dai_links[i].codec_name = NULL;
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
	platform_set_drvdata(pdev, &mop500_card);

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

static struct platform_driver snd_soc_mop500_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-soc-mop500",
		.of_match_table = snd_soc_mop500_match,
	},
	.probe = mop500_probe,
	.remove = mop500_remove,
};

module_platform_driver(snd_soc_mop500_driver);
