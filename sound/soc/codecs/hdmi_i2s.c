/*
 * hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>

#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/io.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#if 0
#define DBG(x...) printk(KERN_INFO "hdmi i2s:"x)
#else
#define DBG(x...) do { } while (0)
#endif

struct snd_soc_dai_driver hdmi_i2s_dai = {
	.name = "rk-hdmi-i2s-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_96000),
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE),
	},
};

static struct snd_soc_codec_driver soc_codec_dev_hdmi_i2s;

static int hdmi_i2s_platform_probe(struct platform_device *pdev)
{
	DBG("Entered %s\n", __func__);

	return snd_soc_register_codec(&pdev->dev,
		&soc_codec_dev_hdmi_i2s,
		&hdmi_i2s_dai, 1);
}

static int hdmi_i2s_platform_remove(struct platform_device *pdev)
{
	DBG("Entered %s\n", __func__);

	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}


static struct platform_driver hdmi_i2s_driver = {
	.probe = hdmi_i2s_platform_probe,
	.remove = hdmi_i2s_platform_remove,
	.driver	= {
		.name	= "hdmi-i2s",
		.owner	= THIS_MODULE,
	},
};


static int __init hdmi_i2s_init(void)
{
	DBG("Entered %s\n", __func__);

	return platform_driver_register(&hdmi_i2s_driver);
}

static void __exit hdmi_i2s_exit(void)
{
	DBG("Entered %s\n", __func__);

	platform_driver_unregister(&hdmi_i2s_driver);
}
module_init(hdmi_i2s_init);
module_exit(hdmi_i2s_exit);

MODULE_DESCRIPTION("HDMI I2S Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi-i2s");
