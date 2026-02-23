// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra_wm8962.c - Tegra machine ASoC driver for boards using WM8962 codec.
 *
 * Copyright (C) 2021-2024 Jonas Schwöbel <jonasschwoebel@yahoo.de>
 *			   Svyatoslav Ryhel <clamor95@gmail.com>
 *
 * Based on tegra_wm8903 code copyright/by:
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010-2012 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * (c) 2009, 2010 Nvidia Graphics Pvt. Ltd.
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 */

#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/soc.h>

#include "../codecs/wm8962.h"

#include "tegra_asoc_machine.h"

static struct snd_soc_jack_pin tegra_wm8962_mic_jack_pins[] = {
	{ .pin = "Mic Jack", .mask = SND_JACK_MICROPHONE },
};

static unsigned int tegra_wm8962_mclk_rate(unsigned int srate)
{
	unsigned int mclk;

	switch (srate) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		mclk = 12000000;
		break;
	}

	return mclk;
}

static int tegra_wm8962_init(struct snd_soc_pcm_runtime *rtd)
{
	struct tegra_machine *machine = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	int err;

	err = tegra_asoc_machine_init(rtd);
	if (err)
		return err;

	if (!machine->gpiod_mic_det && machine->asoc->add_mic_jack) {
		struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
		struct snd_soc_component *component = codec_dai->component;

		err = snd_soc_card_jack_new_pins(rtd->card, "Mic Jack",
						 SND_JACK_MICROPHONE,
						 machine->mic_jack,
						 tegra_wm8962_mic_jack_pins,
						 ARRAY_SIZE(tegra_wm8962_mic_jack_pins));
		if (err) {
			dev_err(rtd->dev, "Mic Jack creation failed: %d\n", err);
			return err;
		}

		wm8962_mic_detect(component, machine->mic_jack);
	}

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");

	return 0;
}

static int tegra_wm8962_remove(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link = &card->dai_link[0];
	struct snd_soc_pcm_runtime *rtd = snd_soc_get_pcm_runtime(card, link);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;

	wm8962_mic_detect(component, NULL);

	return 0;
}

SND_SOC_DAILINK_DEFS(wm8962_hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8962")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tegra_wm8962_dai = {
	.name = "WM8962",
	.stream_name = "WM8962 PCM",
	.init = tegra_wm8962_init,
	.dai_fmt = SND_SOC_DAIFMT_I2S |
		   SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBC_CFC,
	SND_SOC_DAILINK_REG(wm8962_hifi),
};

static struct snd_soc_card snd_soc_tegra_wm8962 = {
	.components = "codec:wm8962",
	.owner = THIS_MODULE,
	.dai_link = &tegra_wm8962_dai,
	.num_links = 1,
	.remove = tegra_wm8962_remove,
	.fully_routed = true,
};

static const struct tegra_asoc_data tegra_wm8962_data = {
	.mclk_rate = tegra_wm8962_mclk_rate,
	.card = &snd_soc_tegra_wm8962,
	.add_common_dapm_widgets = true,
	.add_common_controls = true,
	.add_common_snd_ops = true,
	.add_mic_jack = true,
	.add_hp_jack = true,
};

static const struct of_device_id tegra_wm8962_of_match[] = {
	{ .compatible = "nvidia,tegra-audio-wm8962", .data = &tegra_wm8962_data },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_wm8962_of_match);

static struct platform_driver tegra_wm8962_driver = {
	.driver = {
		.name = "tegra-wm8962",
		.of_match_table = tegra_wm8962_of_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = tegra_asoc_machine_probe,
};
module_platform_driver(tegra_wm8962_driver);

MODULE_AUTHOR("Jonas Schwöbel <jonasschwoebel@yahoo.de>");
MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Tegra+WM8962 machine ASoC driver");
MODULE_LICENSE("GPL");
