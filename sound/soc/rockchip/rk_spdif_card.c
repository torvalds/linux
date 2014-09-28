/*
 * rk_spdif_card.c  --  spdif card for rockchip
 *
 * Copyright 2014 rockchip Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

static int rk_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned long pll_out, rclk_rate;
	int ret, ratio;

	switch (params_rate(params)) {
	case 44100:
		pll_out = 11289600;
		break;
	case 32000:
		pll_out = 8192000;
		break;
	case 48000:
		pll_out = 12288000;
		break;
	case 96000:
		pll_out = 24576000;
		break;
	default:
		pr_err("rk_spdif: params not support\n");
		return -EINVAL;
	}

	ratio = 256;
	rclk_rate = params_rate(params) * ratio;

	/* Set audio source clock rates */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0,
				     rclk_rate, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

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

static struct snd_soc_card rockchip_spdif_card = {
	.name = "RK-SPDIF-CARD",
	.dai_link = &rk_dai,
	.num_links = 1,
};

static int rockchip_spdif_card_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_spdif_card;

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

static int rockchip_spdif_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_spdif_card_of_match[] = {
	{ .compatible = "rockchip-spdif-card"},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_spdif_card_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_spdif_card_driver = {
	.driver         = {
		.name   = "rockchip-spdif-card",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_spdif_card_of_match),
	},
	.probe          = rockchip_spdif_card_probe,
	.remove         = rockchip_spdif_card_remove,
};

module_platform_driver(rockchip_spdif_card_driver);

MODULE_AUTHOR("hzb, <hzb@rock-chips.com>");
MODULE_DESCRIPTION("ALSA SoC RK S/PDIF");
MODULE_LICENSE("GPL");
