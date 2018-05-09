/*
 * Rockchip machine ASoC driver for Rockchip built-in HDMI and external codec IC
 * which use the same i2s interface
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * Authors: Zhangjun <showy.zhang@rock-chips.com>,
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "rockchip_i2s.h"

#define DRV_NAME "rk-hdmi-analog-sound"
#define MAX_CODECS	2

static int rk_hdmi_analog_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	int mclk;
	unsigned int i;

	switch (params_rate(params)) {
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
	case 176400:
		mclk = 11289600 * 2;
		break;
	case 192000:
		mclk = 12288000 * 2;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);

	if (ret && ret != -ENOTSUPP) {
		dev_err(cpu_dai->dev, "Can't set cpu clock %d\n", ret);
		return ret;
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = codec_dais[i];

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP) {
			dev_err(codec_dai->dev,
				"Can't set codec clock %d\n", ret);
			return ret;
		}

	}

	return 0;
}

static struct snd_soc_ops rk_ops = {
	.hw_params = rk_hdmi_analog_hw_params,
};

static struct snd_soc_dai_link rk_dailink = {
	.name = "HDMI-ANALOG",
	.stream_name = "HDMI-ANALOG",
	.ops = &rk_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "rk-hdmi-analog-snd",
	.dai_link = &rk_dailink,
	.num_links = 1,
	.num_aux_devs = 0,
};

static int rk_hdmi_analog_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link *link = card->dai_link;
	struct snd_soc_dai_link_component *codecs;
	struct of_phandle_args args;
	struct device_node *node;
	int count;
	int ret = 0, i = 0, idx = 0;

	snd_soc_of_parse_card_name(card, "rockchip,model");

	card->dev = &pdev->dev;
	count = of_count_phandle_with_args(np, "rockchip,codec", NULL);
	if (count < 0 || count > MAX_CODECS)
		return -EINVAL;

	/* refine codecs, remove unavailable node */
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "rockchip,codec", i);
		if (!node)
			return -ENODEV;
		if (of_device_is_available(node))
			idx++;
	}

	if (!idx)
		return -ENODEV;

	codecs = devm_kcalloc(&pdev->dev, idx,
			      sizeof(*codecs), GFP_KERNEL);
	link->codecs = codecs;
	link->num_codecs = idx;
	idx = 0;
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "rockchip,codec", i);
		if (!node)
			return -ENODEV;
		if (!of_device_is_available(node))
			continue;

		ret = of_parse_phandle_with_fixed_args(np, "rockchip,codec",
						       0, i, &args);
		if (ret)
			return ret;

		codecs[idx].of_node = node;
		ret = snd_soc_get_dai_name(&args, &codecs[idx].dai_name);
		if (ret)
			return ret;
		idx++;
	}

	link->cpu_of_node = of_parse_phandle(np, "rockchip,cpu", 0);
	if (!link->cpu_of_node)
		return -ENODEV;

	link->platform_of_node = link->cpu_of_node;

	ret = snd_soc_of_parse_audio_simple_widgets(card, "rockchip,widgets");
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to parse 'rockchip,widget' property\n");
	}

	ret = snd_soc_of_parse_audio_routing(card, "rockchip,routing");
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to parse 'rockchip,routing' property\n");
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret) {
		dev_err(&pdev->dev, "card register failed %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static const struct of_device_id rockchip_sound_of_match[] = {
	{ .compatible = "rockchip,rk3368-hdmi-analog", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_sound_of_match);

static struct platform_driver rockchip_sound_driver = {
	.probe = rk_hdmi_analog_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_sound_of_match,
	},
};

module_platform_driver(rockchip_sound_driver);

MODULE_AUTHOR("Zhangjun <showy.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Built-in HDMI and Codec IC machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
