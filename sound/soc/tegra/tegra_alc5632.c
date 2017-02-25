/*
* tegra_alc5632.c  --  Toshiba AC100(PAZ00) machine ASoC driver
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.lauchpad.net>
 * Copyright (C) 2012 - NVIDIA, Inc.
 *
 * Authors:  Leon Romanovsky <leon@leon.nu>
 *           Andrey Danin <danindrey@mail.ru>
 *           Marc Dietrich <marvin24@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include "../codecs/alc5632.h"

#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-alc5632"

struct tegra_alc5632 {
	struct tegra_asoc_utils_data util_data;
	int gpio_hp_det;
};

static int tegra_alc5632_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct tegra_alc5632 *alc5632 = snd_soc_card_get_drvdata(card);
	int srate, mclk;
	int err;

	srate = params_rate(params);
	mclk = 512 * srate;

	err = tegra_asoc_utils_set_rate(&alc5632->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return err;
	}

	return 0;
}

static const struct snd_soc_ops tegra_alc5632_asoc_ops = {
	.hw_params = tegra_alc5632_asoc_hw_params,
};

static struct snd_soc_jack tegra_alc5632_hs_jack;

static struct snd_soc_jack_pin tegra_alc5632_hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio tegra_alc5632_hp_jack_gpio = {
	.name = "Headset detection",
	.report = SND_JACK_HEADSET,
	.debounce_time = 150,
};

static const struct snd_soc_dapm_widget tegra_alc5632_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Digital Mic", NULL),
};

static const struct snd_kcontrol_new tegra_alc5632_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
};

static int tegra_alc5632_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct tegra_alc5632 *machine = snd_soc_card_get_drvdata(rtd->card);

	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET,
				    &tegra_alc5632_hs_jack,
				    tegra_alc5632_hs_jack_pins,
				    ARRAY_SIZE(tegra_alc5632_hs_jack_pins));
	if (ret)
		return ret;

	if (gpio_is_valid(machine->gpio_hp_det)) {
		tegra_alc5632_hp_jack_gpio.gpio = machine->gpio_hp_det;
		snd_soc_jack_add_gpios(&tegra_alc5632_hs_jack,
						1,
						&tegra_alc5632_hp_jack_gpio);
	}

	snd_soc_dapm_force_enable_pin(&rtd->card->dapm, "MICBIAS1");

	return 0;
}

static int tegra_alc5632_card_remove(struct snd_soc_card *card)
{
	struct tegra_alc5632 *machine = snd_soc_card_get_drvdata(card);

	if (gpio_is_valid(machine->gpio_hp_det)) {
		snd_soc_jack_free_gpios(&tegra_alc5632_hs_jack, 1,
					&tegra_alc5632_hp_jack_gpio);
	}

	return 0;
}

static struct snd_soc_dai_link tegra_alc5632_dai = {
	.name = "ALC5632",
	.stream_name = "ALC5632 PCM",
	.codec_dai_name = "alc5632-hifi",
	.init = tegra_alc5632_asoc_init,
	.ops = &tegra_alc5632_asoc_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S
			   | SND_SOC_DAIFMT_NB_NF
			   | SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_tegra_alc5632 = {
	.name = "tegra-alc5632",
	.owner = THIS_MODULE,
	.remove = tegra_alc5632_card_remove,
	.dai_link = &tegra_alc5632_dai,
	.num_links = 1,
	.controls = tegra_alc5632_controls,
	.num_controls = ARRAY_SIZE(tegra_alc5632_controls),
	.dapm_widgets = tegra_alc5632_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra_alc5632_dapm_widgets),
	.fully_routed = true,
};

static int tegra_alc5632_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_tegra_alc5632;
	struct tegra_alc5632 *alc5632;
	int ret;

	alc5632 = devm_kzalloc(&pdev->dev,
			sizeof(struct tegra_alc5632), GFP_KERNEL);
	if (!alc5632)
		return -ENOMEM;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, alc5632);

	alc5632->gpio_hp_det = of_get_named_gpio(np, "nvidia,hp-det-gpios", 0);
	if (alc5632->gpio_hp_det == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	ret = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card, "nvidia,audio-routing");
	if (ret)
		goto err;

	tegra_alc5632_dai.codec_of_node = of_parse_phandle(
			pdev->dev.of_node, "nvidia,audio-codec", 0);

	if (!tegra_alc5632_dai.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	tegra_alc5632_dai.cpu_of_node = of_parse_phandle(np,
			"nvidia,i2s-controller", 0);
	if (!tegra_alc5632_dai.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,i2s-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	tegra_alc5632_dai.platform_of_node = tegra_alc5632_dai.cpu_of_node;

	ret = tegra_asoc_utils_init(&alc5632->util_data, &pdev->dev);
	if (ret)
		goto err;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err_fini_utils;
	}

	return 0;

err_fini_utils:
	tegra_asoc_utils_fini(&alc5632->util_data);
err:
	return ret;
}

static int tegra_alc5632_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_alc5632 *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	tegra_asoc_utils_fini(&machine->util_data);

	return 0;
}

static const struct of_device_id tegra_alc5632_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-alc5632", },
	{},
};

static struct platform_driver tegra_alc5632_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = tegra_alc5632_of_match,
	},
	.probe = tegra_alc5632_probe,
	.remove = tegra_alc5632_remove,
};
module_platform_driver(tegra_alc5632_driver);

MODULE_AUTHOR("Leon Romanovsky <leon@leon.nu>");
MODULE_DESCRIPTION("Tegra+ALC5632 machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_alc5632_of_match);
