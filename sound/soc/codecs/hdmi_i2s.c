/*
 * hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

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
			SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000),
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE),
	},
};

static struct snd_soc_codec_driver soc_codec_dev_hdmi_i2s;

static int rockchip_hdmi_i2s_audio_probe(struct platform_device *pdev)
{
	int ret;

	//set dev name to driver->name for sound card register
	dev_set_name(&pdev->dev, "%s", pdev->dev.driver->name);

	ret = snd_soc_register_codec(&pdev->dev,
		&soc_codec_dev_hdmi_i2s,
		&hdmi_i2s_dai, 1);

	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_hdmi_i2s_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_hdmi_i2s_of_match[] = {
        { .compatible = "hdmi-i2s", },
        {},
};
MODULE_DEVICE_TABLE(of, rockchip_hdmi_i2s_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_hdmi_i2s_audio_driver = {
        .driver         = {
                .name   = "hdmi-i2s",
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(rockchip_hdmi_i2s_of_match),
        },
        .probe          = rockchip_hdmi_i2s_audio_probe,
        .remove         = rockchip_hdmi_i2s_audio_remove,
};

module_platform_driver(rockchip_hdmi_i2s_audio_driver);

MODULE_DESCRIPTION("HDMI I2S Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi-i2s");
