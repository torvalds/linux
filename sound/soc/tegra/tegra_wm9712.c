// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra20_wm9712.c - Tegra machine ASoC driver for boards using WM9712 codec.
 *
 * Copyright 2012 Lucas Stach <dev@lynxeye.de>
 *
 * Partly based on code copyright/by:
 * Copyright 2011,2012 Toradex Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-wm9712"

struct tegra_wm9712 {
	struct platform_device *codec;
	struct tegra_asoc_utils_data util_data;
};

static const struct snd_soc_dapm_widget tegra_wm9712_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_LINE("LineIn", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static int tegra_wm9712_init(struct snd_soc_pcm_runtime *rtd)
{
	return snd_soc_dapm_force_enable_pin(&rtd->card->dapm, "Mic Bias");
}

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9712-codec", "wm9712-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tegra_wm9712_dai = {
	.name = "AC97 HiFi",
	.stream_name = "AC97 HiFi",
	.init = tegra_wm9712_init,
	SND_SOC_DAILINK_REG(hifi),
};

static struct snd_soc_card snd_soc_tegra_wm9712 = {
	.name = "tegra-wm9712",
	.owner = THIS_MODULE,
	.dai_link = &tegra_wm9712_dai,
	.num_links = 1,

	.dapm_widgets = tegra_wm9712_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra_wm9712_dapm_widgets),
	.fully_routed = true,
};

static int tegra_wm9712_driver_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_tegra_wm9712;
	struct tegra_wm9712 *machine;
	int ret;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct tegra_wm9712),
			       GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, machine);

	machine->codec = platform_device_alloc("wm9712-codec", -1);
	if (!machine->codec) {
		dev_err(&pdev->dev, "Can't allocate wm9712 platform device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(machine->codec);
	if (ret)
		goto codec_put;

	ret = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (ret)
		goto codec_unregister;

	ret = snd_soc_of_parse_audio_routing(card, "nvidia,audio-routing");
	if (ret)
		goto codec_unregister;

	tegra_wm9712_dai.cpus->of_node = of_parse_phandle(np,
				       "nvidia,ac97-controller", 0);
	if (!tegra_wm9712_dai.cpus->of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,ac97-controller' missing or invalid\n");
		ret = -EINVAL;
		goto codec_unregister;
	}

	tegra_wm9712_dai.platforms->of_node = tegra_wm9712_dai.cpus->of_node;

	ret = tegra_asoc_utils_init(&machine->util_data, &pdev->dev);
	if (ret)
		goto codec_unregister;

	ret = tegra_asoc_utils_set_ac97_rate(&machine->util_data);
	if (ret)
		goto codec_unregister;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto codec_unregister;
	}

	return 0;

codec_unregister:
	platform_device_del(machine->codec);
codec_put:
	platform_device_put(machine->codec);
	return ret;
}

static int tegra_wm9712_driver_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_wm9712 *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	platform_device_unregister(machine->codec);

	return 0;
}

static const struct of_device_id tegra_wm9712_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-wm9712", },
	{},
};

static struct platform_driver tegra_wm9712_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = tegra_wm9712_of_match,
	},
	.probe = tegra_wm9712_driver_probe,
	.remove = tegra_wm9712_driver_remove,
};
module_platform_driver(tegra_wm9712_driver);

MODULE_AUTHOR("Lucas Stach");
MODULE_DESCRIPTION("Tegra+WM9712 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_wm9712_of_match);
