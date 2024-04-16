// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra_wm8903.c - Tegra machine ASoC driver for boards using WM8903 codec.
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

#include "../codecs/wm8903.h"

#include "tegra_asoc_machine.h"

static struct snd_soc_jack_pin tegra_wm8903_mic_jack_pins[] = {
	{ .pin = "Mic Jack", .mask = SND_JACK_MICROPHONE },
};

static unsigned int tegra_wm8903_mclk_rate(unsigned int srate)
{
	unsigned int mclk;

	switch (srate) {
	case 64000:
	case 88200:
	case 96000:
		mclk = 128 * srate;
		break;
	default:
		mclk = 256 * srate;
		break;
	}
	/* FIXME: Codec only requires >= 3MHz if OSR==0 */
	while (mclk < 6000000)
		mclk *= 2;

	return mclk;
}

static int tegra_wm8903_init(struct snd_soc_pcm_runtime *rtd)
{
	struct tegra_machine *machine = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_card *card = rtd->card;
	int err;

	/*
	 * Older version of machine driver was ignoring GPIO polarity,
	 * forcing it to active-low.  This means that all older device-trees
	 * which set the polarity to active-high are wrong and we need to fix
	 * them up.
	 */
	if (machine->asoc->hp_jack_gpio_active_low) {
		bool active_low = gpiod_is_active_low(machine->gpiod_hp_det);

		machine->hp_jack_gpio->invert = !active_low;
	}

	err = tegra_asoc_machine_init(rtd);
	if (err)
		return err;

	if (!machine->gpiod_mic_det && machine->asoc->add_mic_jack) {
		struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
		struct snd_soc_component *component = codec_dai->component;
		int shrt = 0;

		err = snd_soc_card_jack_new_pins(rtd->card, "Mic Jack",
						 SND_JACK_MICROPHONE,
						 machine->mic_jack,
						 tegra_wm8903_mic_jack_pins,
						 ARRAY_SIZE(tegra_wm8903_mic_jack_pins));
		if (err) {
			dev_err(rtd->dev, "Mic Jack creation failed: %d\n", err);
			return err;
		}

		if (of_property_read_bool(card->dev->of_node, "nvidia,headset"))
			shrt = SND_JACK_MICROPHONE;

		wm8903_mic_detect(component, machine->mic_jack,
				  SND_JACK_MICROPHONE, shrt);
	}

	snd_soc_dapm_force_enable_pin(&card->dapm, "MICBIAS");

	return 0;
}

static int tegra_wm8903_remove(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link = &card->dai_link[0];
	struct snd_soc_pcm_runtime *rtd = snd_soc_get_pcm_runtime(card, link);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;

	wm8903_mic_detect(component, NULL, 0, 0);

	return 0;
}

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8903-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tegra_wm8903_dai = {
	.name = "WM8903",
	.stream_name = "WM8903 PCM",
	.init = tegra_wm8903_init,
	.dai_fmt = SND_SOC_DAIFMT_I2S |
		   SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	SND_SOC_DAILINK_REG(hifi),
};

static struct snd_soc_card snd_soc_tegra_wm8903 = {
	.components = "codec:wm8903",
	.owner = THIS_MODULE,
	.dai_link = &tegra_wm8903_dai,
	.num_links = 1,
	.remove = tegra_wm8903_remove,
	.fully_routed = true,
};

/* older device-trees used wrong polarity for the headphones-detection GPIO */
static const struct tegra_asoc_data tegra_wm8903_data_legacy = {
	.mclk_rate = tegra_wm8903_mclk_rate,
	.card = &snd_soc_tegra_wm8903,
	.hp_jack_gpio_active_low = true,
	.add_common_dapm_widgets = true,
	.add_common_controls = true,
	.add_common_snd_ops = true,
	.add_mic_jack = true,
	.add_hp_jack = true,
};

static const struct tegra_asoc_data tegra_wm8903_data = {
	.mclk_rate = tegra_wm8903_mclk_rate,
	.card = &snd_soc_tegra_wm8903,
	.add_common_dapm_widgets = true,
	.add_common_controls = true,
	.add_common_snd_ops = true,
	.add_mic_jack = true,
	.add_hp_jack = true,
};

static const struct of_device_id tegra_wm8903_of_match[] = {
	{ .compatible = "ad,tegra-audio-plutux", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "ad,tegra-audio-wm8903-medcom-wide", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "ad,tegra-audio-wm8903-tec", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903-cardhu", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903-harmony", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903-picasso", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903-seaboard", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903-ventana", .data = &tegra_wm8903_data_legacy },
	{ .compatible = "nvidia,tegra-audio-wm8903", .data = &tegra_wm8903_data },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_wm8903_of_match);

static struct platform_driver tegra_wm8903_driver = {
	.driver = {
		.name = "tegra-wm8903",
		.of_match_table = tegra_wm8903_of_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = tegra_asoc_machine_probe,
};
module_platform_driver(tegra_wm8903_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra+WM8903 machine ASoC driver");
MODULE_LICENSE("GPL");
