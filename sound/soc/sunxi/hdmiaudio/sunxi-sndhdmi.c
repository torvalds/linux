/*
 * sound\soc\sunxi\hdmiaudio\sunxi-sndhdmi.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <plat/sys_config.h>
#include <linux/io.h>

static int sunxi_sndhdmi_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;

	if (!substream) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}
	rtd		= substream->private_data;
	codec_dai	= rtd->codec_dai;
	cpu_dai		= rtd->cpu_dai;

	ret = snd_soc_dai_set_fmt(codec_dai, 0);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops sunxi_sndhdmi_ops = {
	.hw_params	= sunxi_sndhdmi_hw_params,
};

static struct snd_soc_dai_link sunxi_sndhdmi_dai_link = {
	.name		= "HDMIAUDIO",
	.stream_name	= "SUNXI-HDMIAUDIO",
	.cpu_dai_name	= "sunxi-hdmiaudio.0",
	.codec_dai_name = "sndhdmi",
	.platform_name	= "sunxi-hdmiaudio-pcm-audio.0",
	.codec_name	= "sunxi-hdmiaudio-codec.0",
	.ops			= &sunxi_sndhdmi_ops,
};

static struct snd_soc_card snd_soc_sunxi_sndhdmi = {
	.name		= "sunxi-sndhdmi",
	.owner		= THIS_MODULE,
	.dai_link	= &sunxi_sndhdmi_dai_link,
	.num_links	= 1,
};

static int __devinit sunxi_sndhdmi_probe(struct platform_device *pdev)
{
	snd_soc_sunxi_sndhdmi.dev = &pdev->dev;
	return snd_soc_register_card(&snd_soc_sunxi_sndhdmi);
}

static int __devexit sunxi_sndhdmi_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&snd_soc_sunxi_sndhdmi);
	return 0;
}

static struct platform_driver sunxi_sndhdmi_driver = {
	.probe = sunxi_sndhdmi_probe,
	.remove = __devexit_p(sunxi_sndhdmi_remove),
	.driver = {
		.name = "sunxi-sndhdmi",
		.owner = THIS_MODULE,
	},
};

static int __init sunxi_sndhdmi_init(void)
{
	return platform_driver_register(&sunxi_sndhdmi_driver);
}

static void __exit sunxi_sndhdmi_exit(void)
{
	platform_driver_unregister(&sunxi_sndhdmi_driver);
}

module_init(sunxi_sndhdmi_init);
module_exit(sunxi_sndhdmi_exit);

MODULE_AUTHOR("ALL WINNER");
MODULE_DESCRIPTION("SUNXI_SNDHDMI ALSA SoC audio driver");
MODULE_LICENSE("GPL");
