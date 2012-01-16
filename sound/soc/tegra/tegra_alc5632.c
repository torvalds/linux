/*
* tegra_alc5632.c  --  Toshiba AC100(PAZ00) machine ASoC driver
*
* Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.lauchpad.net>
*
* Authors:  Leon Romanovsky <leon@leon.nu>
*           Andrey Danin <danindrey@mail.ru>
*           Marc Dietrich <marvin24@gmx.de>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/alc5632.h"

#include "tegra_das.h"
#include "tegra_i2s.h"
#include "tegra_pcm.h"
#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-alc5632"

struct tegra_alc5632 {
	struct tegra_asoc_utils_data util_data;
};

static int tegra_alc5632_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
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

static struct snd_soc_ops tegra_alc5632_asoc_ops = {
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

static const struct snd_soc_dapm_widget tegra_alc5632_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route tegra_alc5632_audio_map[] = {
	/* Internal Speaker */
	{"Int Spk", NULL, "SPKOUT"},
	{"Int Spk", NULL, "SPKOUTN"},

	/* Headset Mic */
	{"MIC1", NULL, "MICBIAS1"},
	{"MICBIAS1", NULL, "Headset Mic"},

	/* Headset Stereophone */
	{"Headset Stereophone", NULL, "HPR"},
	{"Headset Stereophone", NULL, "HPL"},
};

static const struct snd_kcontrol_new tegra_alc5632_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
};

static int tegra_alc5632_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_jack_new(codec, "Headset Jack", SND_JACK_HEADSET,
			 &tegra_alc5632_hs_jack);
	snd_soc_jack_add_pins(&tegra_alc5632_hs_jack,
			ARRAY_SIZE(tegra_alc5632_hs_jack_pins),
			tegra_alc5632_hs_jack_pins);

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");

	return 0;
}

static struct snd_soc_dai_link tegra_alc5632_dai = {
	.name = "ALC5632",
	.stream_name = "ALC5632 PCM",
	.codec_name = "alc5632.0-001e",
	.platform_name = "tegra-pcm-audio",
	.cpu_dai_name = "tegra-i2s.0",
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
	.dai_link = &tegra_alc5632_dai,
	.num_links = 1,
	.controls = tegra_alc5632_controls,
	.num_controls = ARRAY_SIZE(tegra_alc5632_controls),
	.dapm_widgets = tegra_alc5632_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra_alc5632_dapm_widgets),
	.dapm_routes = tegra_alc5632_audio_map,
	.num_dapm_routes = ARRAY_SIZE(tegra_alc5632_audio_map),
	.fully_routed = true,
};

static __devinit int tegra_alc5632_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_tegra_alc5632;
	struct tegra_alc5632 *alc5632;
	int ret;

	alc5632 = devm_kzalloc(&pdev->dev,
			sizeof(struct tegra_alc5632), GFP_KERNEL);
	if (!alc5632) {
		dev_err(&pdev->dev, "Can't allocate tegra_alc5632\n");
		return -ENOMEM;
	}

	ret = tegra_asoc_utils_init(&alc5632->util_data, &pdev->dev);
	if (ret)
		return ret;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, alc5632);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		tegra_asoc_utils_fini(&alc5632->util_data);
		return ret;
	}

	return 0;
}

static int __devexit tegra_alc5632_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_alc5632 *alc5632 = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	tegra_asoc_utils_fini(&alc5632->util_data);

	return 0;
}

static struct platform_driver tegra_alc5632_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = tegra_alc5632_probe,
	.remove = __devexit_p(tegra_alc5632_remove),
};
module_platform_driver(tegra_alc5632_driver);

MODULE_AUTHOR("Leon Romanovsky <leon@leon.nu>");
MODULE_DESCRIPTION("Tegra+ALC5632 machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
