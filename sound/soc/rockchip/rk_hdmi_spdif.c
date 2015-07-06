/*
 * rk_hdmi_spdif.c  -- hdmi spdif for rockchip
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "card_info.h"
#include "rk_pcm.h"

#if defined(CONFIG_RK_HDMI) && defined(CONFIG_SND_SOC_HDMI_SPDIF)
extern int snd_config_hdmi_audio(struct snd_pcm_hw_params *params);
#endif

static int rk_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned long sclk;
	int ret, ratio;

	/* bmc: 2*32*fs*2 = 128fs */
	ratio = 128;
	switch (params_rate(params)) {
	case 44100:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		sclk = params_rate(params) * ratio;
		break;
	default:
		pr_err("rk_spdif: params not support\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0,
				     sclk, SND_SOC_CLOCK_IN);

#if defined(CONFIG_RK_HDMI) && defined(CONFIG_SND_SOC_HDMI_SPDIF)
	snd_config_hdmi_audio(params);
#endif

	return ret;
}

static struct snd_soc_ops rk_spdif_ops = {
	.hw_params = rk_hw_params,
};

static struct snd_soc_dai_link rk_dai = {
	.name = "SPDIF",
	.stream_name = "SPDIF PCM Playback",
	.codec_dai_name = "rk-hdmi-spdif-hifi",
	.ops = &rk_spdif_ops,
};

static struct snd_soc_card rockchip_hdmi_spdif_snd_card = {
	.name = "RK-HDMI-SPDIF",
	.dai_link = &rk_dai,
	.num_links = 1,
};

static int rockchip_hdmi_spdif_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_hdmi_spdif_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info_(card, false);
	if (ret) {
		pr_err("%s() get sound card info failed:%d\n",
		       __func__, ret);
		return ret;
	}

	ret = snd_soc_register_card(card);

	if (ret)
		pr_err("%s() register card failed:%d\n",
		       __func__, ret);

	return ret;
}

static int rockchip_hdmi_spdif_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_hdmi_spdif_of_match[] = {
	{ .compatible = "rockchip-hdmi-spdif", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_hdmi_spdif_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_hdmi_spdif_audio_driver = {
	.driver = {
		.name = "rockchip-hdmi-spdif",
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_hdmi_spdif_of_match),
	},
	.probe = rockchip_hdmi_spdif_audio_probe,
	.remove = rockchip_hdmi_spdif_audio_remove,
};

module_platform_driver(rockchip_hdmi_spdif_audio_driver);

MODULE_AUTHOR("hzb <hzb@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip HDMI Spdif Card");
MODULE_LICENSE("GPL");
