/*
 * harmony.c - Harmony machine ASoC driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010-2011 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * (c) 2009, 2010 Nvidia Graphics Pvt. Ltd.
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
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

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_das.h"
#include "tegra_i2s.h"
#include "tegra_pcm.h"
#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-harmony"
#define PREFIX DRV_NAME ": "

struct tegra_harmony {
};

static int harmony_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int srate, mclk, mclk_change;
	int err;

	srate = params_rate(params);
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

	err = tegra_asoc_utils_set_rate(srate, mclk, &mclk_change);
	if (err < 0) {
		pr_err(PREFIX "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(codec_dai,
					SND_SOC_DAIFMT_I2S |
					SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		pr_err(PREFIX "codec_dai fmt not set\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai,
					SND_SOC_DAIFMT_I2S |
					SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		pr_err(PREFIX "cpu_dai fmt not set\n");
		return err;
	}

	if (mclk_change) {
	    err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
	    if (err < 0) {
		    pr_err(PREFIX "codec_dai clock not set\n");
		    return err;
	    }
	}

	return 0;
}

static struct snd_soc_ops harmony_asoc_ops = {
	.hw_params = harmony_asoc_hw_params,
};

static const struct snd_soc_dapm_widget harmony_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route harmony_audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Mic Bias", NULL, "Mic Jack"},
	{"IN1L", NULL, "Mic Bias"},
};

static int harmony_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, harmony_dapm_widgets,
					ARRAY_SIZE(harmony_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, harmony_audio_map,
				ARRAY_SIZE(harmony_audio_map));

	snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	snd_soc_dapm_enable_pin(dapm, "Mic Jack");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link harmony_wm8903_dai = {
	.name = "WM8903",
	.stream_name = "WM8903 PCM",
	.codec_name = "wm8903-codec.0-001a",
	.platform_name = "tegra-pcm-audio",
	.cpu_dai_name = "tegra-i2s.0",
	.codec_dai_name = "wm8903-hifi",
	.init = harmony_asoc_init,
	.ops = &harmony_asoc_ops,
};

static struct snd_soc_card snd_soc_harmony = {
	.name = "tegra-harmony",
	.dai_link = &harmony_wm8903_dai,
	.num_links = 1,
};

static __devinit int tegra_snd_harmony_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_harmony;
	struct tegra_harmony *harmony;
	int ret;

	if (!machine_is_harmony()) {
		dev_err(&pdev->dev, "Not running on Tegra Harmony!\n");
		return -ENODEV;
	}

	harmony = kzalloc(sizeof(struct tegra_harmony), GFP_KERNEL);
	if (!harmony) {
		dev_err(&pdev->dev, "Can't allocate tegra_harmony\n");
		return -ENOMEM;
	}

	ret = tegra_asoc_utils_init();
	if (ret)
		goto err_free_harmony;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, harmony);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err_clear_drvdata;
	}

	return 0;

err_clear_drvdata:
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;
	tegra_asoc_utils_fini();
err_free_harmony:
	kfree(harmony);
	return ret;
}

static int __devexit tegra_snd_harmony_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_harmony *harmony = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	tegra_asoc_utils_fini();

	kfree(harmony);

	return 0;
}

static struct platform_driver tegra_snd_harmony_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = tegra_snd_harmony_probe,
	.remove = __devexit_p(tegra_snd_harmony_remove),
};

static int __init snd_tegra_harmony_init(void)
{
	return platform_driver_register(&tegra_snd_harmony_driver);
}
module_init(snd_tegra_harmony_init);

static void __exit snd_tegra_harmony_exit(void)
{
	platform_driver_unregister(&tegra_snd_harmony_driver);
}
module_exit(snd_tegra_harmony_exit);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Harmony machine ASoC driver");
MODULE_LICENSE("GPL");
