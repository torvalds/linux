/*
 * Rockchip machine ASoC driver for boards using a DA7219 CODEC.
 *
 * Copyright (c) 2016, ROCKCHIP CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "rockchip_i2s.h"
#include "../codecs/da7219.h"
#include "../codecs/da7219-aad.h"

#define DRV_NAME "rockchip-snd-da7219"

static struct snd_soc_jack mach_da7219_jack;

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
};

static const struct snd_soc_dapm_route rk_audio_map[] = {
	/* Input Lines */
	{"MIC", NULL, "Headset Mic"},

	/* Output Lines */
	{"Headphones", NULL, "HPL"},
	{"Headphones", NULL, "HPR"},
};

static const struct snd_kcontrol_new rk_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static int rk_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set cpu clock out %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock in %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set pll sysclk mclk %d\n", ret);
		return ret;
	}

	return ret;
}

static int rk_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	int ret = 0;

	card->dapm.idle_bias_off = true;

	snd_soc_dapm_enable_pin(dapm, "Headphones");
	snd_soc_dapm_enable_pin(dapm, "Speakers");
	snd_soc_dapm_enable_pin(dapm, "Headset Mic");
	snd_soc_dapm_enable_pin(dapm, "Int Mic");

	snd_soc_dapm_sync(dapm);

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(card, "Headset Jack",
				    SND_JACK_HEADSET |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &mach_da7219_jack, NULL, 0);

	if (ret) {
		dev_err(card->dev, "New Headset Jack failed! (%d)\n", ret);
		return ret;
	}

	da7219_aad_jack_det(runtime->codec, &mach_da7219_jack);

	return ret;
}

static struct snd_soc_ops rk_aif1_ops = {
	.hw_params = rk_aif1_hw_params,
};

static struct snd_soc_dai_link rk_dailink = {
	.name = "da7219",
	.stream_name = "da7219 PCM",
	.codec_dai_name = "da7219-hifi",
	.init = rk_init,
	.ops = &rk_aif1_ops,
	/* set da7219 as slave */
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "audio-da7219",
	.dai_link = &rk_dailink,
	.num_links = 1,
	.dapm_widgets = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes = rk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_audio_map),
	.controls = rk_mc_controls,
	.num_controls = ARRAY_SIZE(rk_mc_controls),
};

static int snd_rk_mc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;

	/* register the soc card */
	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	rk_dailink.codec_of_node = of_parse_phandle(np,
			"rockchip,audio-codec", 0);
	if (!rk_dailink.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,audio-codec' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.cpu_of_node = of_parse_phandle(np,
			"rockchip,i2s-controller", 0);
	if (!rk_dailink.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.platform_of_node = rk_dailink.cpu_of_node;

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret) {
		dev_err(&pdev->dev,
			"snd_soc_of_parse_card_name failed %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id rockchip_da7219_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-da7219", },
	{},
};

static struct platform_driver snd_rk_mc_driver = {
	.probe = snd_rk_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_da7219_of_match,
	},
};

module_platform_driver(snd_rk_mc_driver);

MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip da7219 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_da7219_of_match);
