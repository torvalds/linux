/*
 * sound\soc\sunxi\hdmiaudio\sunxi-hdmiaudio.c
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <mach/clock.h>
#include <mach/hardware.h>
#include <plat/system.h>
#include <plat/sys_config.h>
#include <plat/dma_compat.h>

static struct sunxi_dma_params sunxi_hdmiaudio_pcm_stereo_out = {
	.client.name	=	"HDMIAUDIO PCM Stereo out",
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	.channel	=	DMACH_HDMIAUDIO,
#endif
	.dma_addr	=	0,
};

void sunxi_snd_txctrl_hdmiaudio(struct snd_pcm_substream *substream, int on)
{
}

static int sunxi_hdmiaudio_set_fmt(struct snd_soc_dai *cpu_dai,
							unsigned int fmt)
{
	return 0;
}

static int sunxi_hdmiaudio_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd;
	struct sunxi_dma_params *dma_data;

	if (!substream) {
		printk("error:%s,line:%d\n", __func__, __LINE__);
		return -EAGAIN;
	}

	rtd = substream->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &sunxi_hdmiaudio_pcm_stereo_out;
	else
		printk("error:hdmiaudio can't support capture:%s,line:%d\n",
							__func__, __LINE__);

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_data);

	return 0;
}

static int sunxi_hdmiaudio_trigger(struct snd_pcm_substream *substream,
					int cmd, struct snd_soc_dai *dai)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sunxi_dma_params *dma_data =
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (sunxi_is_sun7i())
		return 0; /* No rx / tx control, etc. on sun7i() */

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sunxi_snd_txctrl_hdmiaudio(substream, 1);
		sunxi_dma_started(dma_data);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sunxi_snd_txctrl_hdmiaudio(substream, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

/* freq: 1: 22.5792MHz 0: 24.576MHz */
static int sunxi_hdmiaudio_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
						unsigned int freq, int dir)
{
	return 0;
}

static int sunxi_hdmiaudio_set_clkdiv(struct snd_soc_dai *cpu_dai, int div_id,
									int div)
{
	return 0;
}

u32 sunxi_hdmiaudio_get_clockrate(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_hdmiaudio_get_clockrate);

static int sunxi_hdmiaudio_dai_probe(struct snd_soc_dai *dai)
{
	return 0;
}
static int sunxi_hdmiaudio_dai_remove(struct snd_soc_dai *dai)
{
	return 0;
}

static int sunxi_hdmiaudio_suspend(struct snd_soc_dai *cpu_dai)
{
	printk("[HDMIAUDIO]Entered %s\n", __func__);

	/*
	 *	printk("[HDMIAUDIO]PLL2 0x01c20008 = %#x, line = %d\n",
	 *			*(volatile int *)0xF1C20008, __LINE__);
	 */
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c20068 = %#x, line= %d\n",
					*(volatile int *)0xF1C20068, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n",
					*(volatile int *)0xF1C200B8, __LINE__);

	return 0;
}

static int sunxi_hdmiaudio_resume(struct snd_soc_dai *cpu_dai)
{
	printk("[HDMIAUDIO]Entered %s\n", __func__);

	/*
	 *	printk("[HDMIAUDIO]PLL2 0x01c20008 = %#x, line = %d\n",
	 *				*(volatile int *)0xF1C20008, __LINE__);
	 */
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c20068 = %#x, line= %d\n",
					*(volatile int *)0xF1C20068, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n",
					*(volatile int *)0xF1C200B8, __LINE__);

	return 0;
}

#define SUNXI_HDMI_RATES (SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT)
static struct snd_soc_dai_ops sunxi_hdmiaudio_dai_ops = {
	.trigger		= sunxi_hdmiaudio_trigger,
	.hw_params		= sunxi_hdmiaudio_hw_params,
	.set_fmt		= sunxi_hdmiaudio_set_fmt,
	.set_clkdiv		= sunxi_hdmiaudio_set_clkdiv,
	.set_sysclk		= sunxi_hdmiaudio_set_sysclk,
};
static struct snd_soc_dai_driver sunxi_hdmiaudio_dai = {

	.probe			= sunxi_hdmiaudio_dai_probe,
	.suspend		= sunxi_hdmiaudio_suspend,
	.resume			= sunxi_hdmiaudio_resume,
	.remove			= sunxi_hdmiaudio_dai_remove,
	.playback		= {
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SUNXI_HDMI_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
	.symmetric_rates	= 1,
	.ops			= &sunxi_hdmiaudio_dai_ops,
};

static int __devinit sunxi_hdmiaudio_dev_probe(struct platform_device *pdev)
{
	return snd_soc_register_dai(&pdev->dev, &sunxi_hdmiaudio_dai);
}

static int __devexit sunxi_hdmiaudio_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver sunxi_hdmiaudio_driver = {
	.probe	= sunxi_hdmiaudio_dev_probe,
	.remove	= __devexit_p(sunxi_hdmiaudio_dev_remove),
	.driver	= {
		.name = "sunxi-hdmiaudio",
		.owner = THIS_MODULE,
	},
};

static int __init sunxi_hdmiaudio_init(void)
{
	int err = 0;

	err = platform_driver_register(&sunxi_hdmiaudio_driver);
	if (err < 0)
		return err;

	return 0;
}
module_init(sunxi_hdmiaudio_init);

static void __exit sunxi_hdmiaudio_exit(void)
{
	platform_driver_unregister(&sunxi_hdmiaudio_driver);
}
module_exit(sunxi_hdmiaudio_exit);


/* Module information */
MODULE_AUTHOR("ALLWINNER");
MODULE_DESCRIPTION("sunxi hdmiaudio SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: sunxi-hdmiaudio");
