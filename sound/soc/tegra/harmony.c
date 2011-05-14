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
#include <linux/gpio.h>

#include <mach/harmony_audio.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/wm8903.h"

#include "tegra_das.h"
#include "tegra_i2s.h"
#include "tegra_pcm.h"
#include "tegra_asoc_utils.h"

#define DRV_NAME "tegra-snd-harmony"

#define GPIO_SPKR_EN    BIT(0)
#define GPIO_INT_MIC_EN BIT(1)
#define GPIO_EXT_MIC_EN BIT(2)

struct tegra_harmony {
	struct tegra_asoc_utils_data util_data;
	struct harmony_audio_platform_data *pdata;
	int gpio_requested;
};

static int harmony_asoc_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_harmony *harmony = snd_soc_card_get_drvdata(card);
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

	err = tegra_asoc_utils_set_rate(&harmony->util_data, srate, mclk,
					&mclk_change);
	if (err < 0) {
		dev_err(card->dev, "Can't configure clocks\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(codec_dai,
					SND_SOC_DAIFMT_I2S |
					SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		dev_err(card->dev, "codec_dai fmt not set\n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai,
					SND_SOC_DAIFMT_I2S |
					SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
	if (err < 0) {
		dev_err(card->dev, "cpu_dai fmt not set\n");
		return err;
	}

	if (mclk_change) {
		err = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					     SND_SOC_CLOCK_IN);
		if (err < 0) {
			dev_err(card->dev, "codec_dai clock not set\n");
			return err;
		}
	}

	return 0;
}

static struct snd_soc_ops harmony_asoc_ops = {
	.hw_params = harmony_asoc_hw_params,
};

static struct snd_soc_jack harmony_hp_jack;

static struct snd_soc_jack_pin harmony_hp_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio harmony_hp_jack_gpios[] = {
	{
		.name = "headphone detect",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 150,
		.invert = 1,
	}
};

static struct snd_soc_jack harmony_mic_jack;

static struct snd_soc_jack_pin harmony_mic_jack_pins[] = {
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int harmony_event_int_spk(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct snd_soc_card *card = codec->card;
	struct tegra_harmony *harmony = snd_soc_card_get_drvdata(card);
	struct harmony_audio_platform_data *pdata = harmony->pdata;

	gpio_set_value_cansleep(pdata->gpio_spkr_en,
				SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_dapm_widget harmony_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Int Spk", harmony_event_int_spk),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route harmony_audio_map[] = {
	{"Headphone Jack", NULL, "HPOUTR"},
	{"Headphone Jack", NULL, "HPOUTL"},
	{"Int Spk", NULL, "ROP"},
	{"Int Spk", NULL, "RON"},
	{"Int Spk", NULL, "LOP"},
	{"Int Spk", NULL, "LON"},
	{"Mic Bias", NULL, "Mic Jack"},
	{"IN1L", NULL, "Mic Bias"},
};

static const struct snd_kcontrol_new harmony_controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Spk"),
};

static int harmony_asoc_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_card *card = codec->card;
	struct tegra_harmony *harmony = snd_soc_card_get_drvdata(card);
	struct harmony_audio_platform_data *pdata = harmony->pdata;
	int ret;

	ret = gpio_request(pdata->gpio_spkr_en, "spkr_en");
	if (ret) {
		dev_err(card->dev, "cannot get spkr_en gpio\n");
		return ret;
	}
	harmony->gpio_requested |= GPIO_SPKR_EN;

	gpio_direction_output(pdata->gpio_spkr_en, 0);

	ret = gpio_request(pdata->gpio_int_mic_en, "int_mic_en");
	if (ret) {
		dev_err(card->dev, "cannot get int_mic_en gpio\n");
		return ret;
	}
	harmony->gpio_requested |= GPIO_INT_MIC_EN;

	/* Disable int mic; enable signal is active-high */
	gpio_direction_output(pdata->gpio_int_mic_en, 0);

	ret = gpio_request(pdata->gpio_ext_mic_en, "ext_mic_en");
	if (ret) {
		dev_err(card->dev, "cannot get ext_mic_en gpio\n");
		return ret;
	}
	harmony->gpio_requested |= GPIO_EXT_MIC_EN;

	/* Enable ext mic; enable signal is active-low */
	gpio_direction_output(pdata->gpio_ext_mic_en, 0);

	ret = snd_soc_add_controls(codec, harmony_controls,
				   ARRAY_SIZE(harmony_controls));
	if (ret < 0)
		return ret;

	snd_soc_dapm_new_controls(dapm, harmony_dapm_widgets,
					ARRAY_SIZE(harmony_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, harmony_audio_map,
				ARRAY_SIZE(harmony_audio_map));

	harmony_hp_jack_gpios[0].gpio = pdata->gpio_hp_det;
	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
			 &harmony_hp_jack);
	snd_soc_jack_add_pins(&harmony_hp_jack,
			      ARRAY_SIZE(harmony_hp_jack_pins),
			      harmony_hp_jack_pins);
	snd_soc_jack_add_gpios(&harmony_hp_jack,
			       ARRAY_SIZE(harmony_hp_jack_gpios),
			       harmony_hp_jack_gpios);

	snd_soc_jack_new(codec, "Mic Jack", SND_JACK_MICROPHONE,
			 &harmony_mic_jack);
	snd_soc_jack_add_pins(&harmony_mic_jack,
			      ARRAY_SIZE(harmony_mic_jack_pins),
			      harmony_mic_jack_pins);
	wm8903_mic_detect(codec, &harmony_mic_jack, SND_JACK_MICROPHONE, 0);

	snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");

	snd_soc_dapm_nc_pin(dapm, "IN3L");
	snd_soc_dapm_nc_pin(dapm, "IN3R");
	snd_soc_dapm_nc_pin(dapm, "LINEOUTL");
	snd_soc_dapm_nc_pin(dapm, "LINEOUTR");

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link harmony_wm8903_dai = {
	.name = "WM8903",
	.stream_name = "WM8903 PCM",
	.codec_name = "wm8903.0-001a",
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
	struct harmony_audio_platform_data *pdata;
	int ret;

	if (!machine_is_harmony()) {
		dev_err(&pdev->dev, "Not running on Tegra Harmony!\n");
		return -ENODEV;
	}

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data supplied\n");
		return -EINVAL;
	}

	harmony = kzalloc(sizeof(struct tegra_harmony), GFP_KERNEL);
	if (!harmony) {
		dev_err(&pdev->dev, "Can't allocate tegra_harmony\n");
		return -ENOMEM;
	}

	harmony->pdata = pdata;

	ret = tegra_asoc_utils_init(&harmony->util_data, &pdev->dev);
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
	tegra_asoc_utils_fini(&harmony->util_data);
err_free_harmony:
	kfree(harmony);
	return ret;
}

static int __devexit tegra_snd_harmony_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tegra_harmony *harmony = snd_soc_card_get_drvdata(card);
	struct harmony_audio_platform_data *pdata = harmony->pdata;

	snd_soc_unregister_card(card);

	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	tegra_asoc_utils_fini(&harmony->util_data);

	if (harmony->gpio_requested & GPIO_EXT_MIC_EN)
		gpio_free(pdata->gpio_ext_mic_en);
	if (harmony->gpio_requested & GPIO_INT_MIC_EN)
		gpio_free(pdata->gpio_int_mic_en);
	if (harmony->gpio_requested & GPIO_SPKR_EN)
		gpio_free(pdata->gpio_spkr_en);

	kfree(harmony);

	return 0;
}

static struct platform_driver tegra_snd_harmony_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
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
MODULE_ALIAS("platform:" DRV_NAME);
