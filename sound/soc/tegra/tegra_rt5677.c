// SPDX-License-Identifier: GPL-2.0-only
/*
* tegra_rt5677.c - Tegra machine ASoC driver for boards using RT5677 codec.
 *
 * Copyright (c) 2014, The Chromium OS Authors.  All rights reserved.
 *
 * Based on code copyright/by:
 *
 * Copyright (C) 2010-2012 - NVIDIA, Inc.
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.lauchpad.net>
 * (c) 2009, 2010 Nvidia Graphics Pvt. Ltd.
 * Copyright 2007 Wolfson Microelectronics PLC.
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

#include "../codecs/rt5677.h"

#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-rt5677"

struct tegra_rt5677 {
	struct tegra_asoc_utils_data util_data;
	int gpio_hp_det;
	int gpio_hp_en;
	int gpio_mic_present;
	int gpio_dmic_clk_en;
};

static int tegra_rt5677_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct tegra_rt5677 *machine = snd_soc_card_get_drvdata(card);
	int srate, mclk, err;

	srate = params_rate(params);
	mclk = 256 * srate;

	err = tegra_asoc_utils_set_rate(&machine->util_data, srate, mclk);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(codec_dai, RT5677_SCLK_S_MCLK, mclk,
					SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return err;
	}

	return 0;
}

static int tegra_rt5677_event_hp(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct tegra_rt5677 *machine = snd_soc_card_get_drvdata(card);

	if (!gpio_is_valid(machine->gpio_hp_en))
		return 0;

	gpio_set_value_cansleep(machine->gpio_hp_en,
		SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_ops tegra_rt5677_ops = {
	.hw_params = tegra_rt5677_asoc_hw_params,
};

static struct snd_soc_jack tegra_rt5677_hp_jack;

static struct snd_soc_jack_pin tegra_rt5677_hp_jack_pins = {
	.pin = "Headphone",
	.mask = SND_JACK_HEADPHONE,
};
static struct snd_soc_jack_gpio tegra_rt5677_hp_jack_gpio = {
	.name = "Headphone detection",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 150,
};

static struct snd_soc_jack tegra_rt5677_mic_jack;

static struct snd_soc_jack_pin tegra_rt5677_mic_jack_pins = {
	.pin = "Headset Mic",
	.mask = SND_JACK_MICROPHONE,
};

static struct snd_soc_jack_gpio tegra_rt5677_mic_jack_gpio = {
	.name = "Headset Mic detection",
	.report = SND_JACK_MICROPHONE,
	.debounce_time = 150,
	.invert = 1
};

static const struct snd_soc_dapm_widget tegra_rt5677_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", tegra_rt5677_event_hp),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic 1", NULL),
	SND_SOC_DAPM_MIC("Internal Mic 2", NULL),
};

static const struct snd_kcontrol_new tegra_rt5677_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic 1"),
	SOC_DAPM_PIN_SWITCH("Internal Mic 2"),
};

static int tegra_rt5677_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct tegra_rt5677 *machine = snd_soc_card_get_drvdata(rtd->card);

	snd_soc_card_jack_new(rtd->card, "Headphone Jack", SND_JACK_HEADPHONE,
			      &tegra_rt5677_hp_jack,
			      &tegra_rt5677_hp_jack_pins, 1);

	if (gpio_is_valid(machine->gpio_hp_det)) {
		tegra_rt5677_hp_jack_gpio.gpio = machine->gpio_hp_det;
		snd_soc_jack_add_gpios(&tegra_rt5677_hp_jack, 1,
				&tegra_rt5677_hp_jack_gpio);
	}


	snd_soc_card_jack_new(rtd->card, "Mic Jack", SND_JACK_MICROPHONE,
			      &tegra_rt5677_mic_jack,
			      &tegra_rt5677_mic_jack_pins, 1);

	if (gpio_is_valid(machine->gpio_mic_present)) {
		tegra_rt5677_mic_jack_gpio.gpio = machine->gpio_mic_present;
		snd_soc_jack_add_gpios(&tegra_rt5677_mic_jack, 1,
				&tegra_rt5677_mic_jack_gpio);
	}

	snd_soc_dapm_force_enable_pin(&rtd->card->dapm, "MICBIAS1");

	return 0;
}

SND_SOC_DAILINK_DEFS(pcm,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "rt5677-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tegra_rt5677_dai = {
	.name = "RT5677",
	.stream_name = "RT5677 PCM",
	.init = tegra_rt5677_asoc_init,
	.ops = &tegra_rt5677_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	SND_SOC_DAILINK_REG(pcm),
};

static struct snd_soc_card snd_soc_tegra_rt5677 = {
	.name = "tegra-rt5677",
	.owner = THIS_MODULE,
	.dai_link = &tegra_rt5677_dai,
	.num_links = 1,
	.controls = tegra_rt5677_controls,
	.num_controls = ARRAY_SIZE(tegra_rt5677_controls),
	.dapm_widgets = tegra_rt5677_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra_rt5677_dapm_widgets),
	.fully_routed = true,
};

static int tegra_rt5677_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_tegra_rt5677;
	struct tegra_rt5677 *machine;
	int ret;

	machine = devm_kzalloc(&pdev->dev,
			sizeof(struct tegra_rt5677), GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, machine);

	machine->gpio_hp_det = of_get_named_gpio(np, "nvidia,hp-det-gpios", 0);
	if (machine->gpio_hp_det == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	machine->gpio_mic_present = of_get_named_gpio(np,
			"nvidia,mic-present-gpios", 0);
	if (machine->gpio_mic_present == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	machine->gpio_hp_en = of_get_named_gpio(np, "nvidia,hp-en-gpios", 0);
	if (machine->gpio_hp_en == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (gpio_is_valid(machine->gpio_hp_en)) {
		ret = devm_gpio_request_one(&pdev->dev, machine->gpio_hp_en,
				GPIOF_OUT_INIT_LOW, "hp_en");
		if (ret) {
			dev_err(card->dev, "cannot get hp_en gpio\n");
			return ret;
		}
	}

	machine->gpio_dmic_clk_en = of_get_named_gpio(np,
		"nvidia,dmic-clk-en-gpios", 0);
	if (machine->gpio_dmic_clk_en == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (gpio_is_valid(machine->gpio_dmic_clk_en)) {
		ret = devm_gpio_request_one(&pdev->dev,
				machine->gpio_dmic_clk_en,
				GPIOF_OUT_INIT_HIGH, "dmic_clk_en");
		if (ret) {
			dev_err(card->dev, "cannot get dmic_clk_en gpio\n");
			return ret;
		}
	}

	ret = snd_soc_of_parse_card_name(card, "nvidia,model");
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card, "nvidia,audio-routing");
	if (ret)
		goto err;

	tegra_rt5677_dai.codecs->of_node = of_parse_phandle(np,
			"nvidia,audio-codec", 0);
	if (!tegra_rt5677_dai.codecs->of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,audio-codec' missing or invalid\n");
		ret = -EINVAL;
		goto err;
	}

	tegra_rt5677_dai.cpus->of_node = of_parse_phandle(np,
			"nvidia,i2s-controller", 0);
	if (!tegra_rt5677_dai.cpus->of_node) {
		dev_err(&pdev->dev,
			"Property 'nvidia,i2s-controller' missing or invalid\n");
		ret = -EINVAL;
		goto err_put_codec_of_node;
	}
	tegra_rt5677_dai.platforms->of_node = tegra_rt5677_dai.cpus->of_node;

	ret = tegra_asoc_utils_init(&machine->util_data, &pdev->dev);
	if (ret)
		goto err_put_cpu_of_node;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err_fini_utils;
	}

	return 0;

err_fini_utils:
	tegra_asoc_utils_fini(&machine->util_data);
err_put_cpu_of_node:
	of_node_put(tegra_rt5677_dai.cpus->of_node);
	tegra_rt5677_dai.cpus->of_node = NULL;
	tegra_rt5677_dai.platforms->of_node = NULL;
err_put_codec_of_node:
	of_node_put(tegra_rt5677_dai.codecs->of_node);
	tegra_rt5677_dai.codecs->of_node = NULL;
err:
	return ret;
}

static int tegra_rt5677_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_rt5677 *machine = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	tegra_asoc_utils_fini(&machine->util_data);

	tegra_rt5677_dai.platforms->of_node = NULL;
	of_node_put(tegra_rt5677_dai.codecs->of_node);
	tegra_rt5677_dai.codecs->of_node = NULL;
	of_node_put(tegra_rt5677_dai.cpus->of_node);
	tegra_rt5677_dai.cpus->of_node = NULL;

	return 0;
}

static const struct of_device_id tegra_rt5677_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-rt5677", },
	{},
};

static struct platform_driver tegra_rt5677_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = tegra_rt5677_of_match,
	},
	.probe = tegra_rt5677_probe,
	.remove = tegra_rt5677_remove,
};
module_platform_driver(tegra_rt5677_driver);

MODULE_AUTHOR("Anatol Pomozov <anatol@google.com>");
MODULE_DESCRIPTION("Tegra+RT5677 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra_rt5677_of_match);
