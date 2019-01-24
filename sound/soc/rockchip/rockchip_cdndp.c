/*
 * Rockchip machine ASoC driver for boards using CDN DP
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
 */

#include <linux/module.h>
#include <linux/input.h>
#include <sound/jack.h>
#include <sound/hdmi-codec.h>
#include <sound/soc.h>

#define DRV_NAME "rockchip-cdndp-sound"

static int rockchip_sound_cdndp_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int mclk, ret;

	/* in bypass mode, the mclk has to be one of the frequencies below */
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

	return 0;
}

static struct snd_soc_jack cdn_dp_card_jack;

static int rockchip_sound_cdndp_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_codec *codec = runtime->codec;
	int ret;

	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "DP Jack", SND_JACK_LINEOUT,
				    &cdn_dp_card_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "Can't create DP Jack %d\n", ret);
		return ret;
	}

	return hdmi_codec_set_jack_detect(codec, &cdn_dp_card_jack);
}

static struct snd_soc_ops rockchip_sound_cdndp_ops = {
	.hw_params = rockchip_sound_cdndp_hw_params,
};

static struct snd_soc_dai_link cdndp_dailink = {
	.name = "DP",
	.stream_name = "DP PCM",
	.codec_dai_name = "spdif-hifi",
	.init = rockchip_sound_cdndp_init,
	.ops = &rockchip_sound_cdndp_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card rockchip_sound_card = {
	.name = "rockchip-cdndp-sound",
	.owner = THIS_MODULE,
	.dai_link = &cdndp_dailink,
	.num_links = 1,
};

static int rockchip_sound_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &rockchip_sound_card;
	struct device_node *cpu_node;
	int ret;

	cpu_node = of_parse_phandle(pdev->dev.of_node, "rockchip,cpu", 0);
	if (!cpu_node) {
		dev_err(&pdev->dev, "Property 'rockchip,cpu' missing or invalid\n");
		return -EINVAL;
	}

	cdndp_dailink.platform_of_node = cpu_node;
	cdndp_dailink.cpu_of_node = cpu_node;

	cdndp_dailink.codec_of_node = of_parse_phandle(pdev->dev.of_node,
							   "rockchip,codec", 0);
	if (!cdndp_dailink.codec_of_node) {
		dev_err(&pdev->dev, "Property 'rockchip,codec' invalid\n");
		return -EINVAL;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);

	return ret;
}

static const struct of_device_id rockchip_sound_of_match[] = {
	{ .compatible = "rockchip,cdndp-sound", },
	{},
};

static struct platform_driver rockchip_sound_driver = {
	.probe = rockchip_sound_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = rockchip_sound_of_match,
#ifdef CONFIG_PM
		.pm = &snd_soc_pm_ops,
#endif
	},
};

module_platform_driver(rockchip_sound_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip CDN DP Machine Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_sound_of_match);
