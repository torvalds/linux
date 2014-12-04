/*
 * ALSA SoC SPDIF CODEC driver
 *
 *  This driver is used by controllers which can operate in DIT (SPDI/F) where
 *  no codec is needed.  This file provides stub codec that can be used
 *  in these configurations. TI DaVinci Audio controller uses this driver.
 *
 * Author:      Steve Chen,  <schen@mvista.com>
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/of.h>


#define DRV_NAME "spdif-dit"

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	SNDRV_PCM_FMTBIT_S16_LE


static struct snd_soc_codec_driver soc_codec_spdif_dit;

struct pinctrl *pin_spdif_ctl;
struct device *spdif_dev;
static struct snd_soc_dai_driver dit_stub_dai = {
	.name		= "dit-hifi",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
	.capture 	= {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},	
};

static unsigned int spdif_pinmux = 0;
void aml_spdif_pinmux_init(struct device *dev)
{
    printk(KERN_INFO"aml_spdif_unmute \n");
    if(!spdif_pinmux){
        spdif_pinmux = 1;
    }
}

void aml_spdif_pinmux_deinit(struct device *dev)
{
    printk(KERN_INFO"aml_spdif_mute \n");
    if(spdif_pinmux){
        spdif_pinmux = 0;
    }
}

static int spdif_dit_probe(struct platform_device *pdev)
{
    int ret=0;
	printk("enter spdif_dit_probe \n");
    spdif_dev = &pdev->dev;

    aml_spdif_pinmux_init(&pdev->dev);
    if (ret < 0)
		printk("spdif: failed to add spdif_mute sysfs: %d\n",ret);
	return snd_soc_register_codec(&pdev->dev, &soc_codec_spdif_dit,
			&dit_stub_dai, 1);
}
static int spdif_dit_remove(struct platform_device *pdev)
{
    aml_spdif_pinmux_deinit(&pdev->dev);
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_spdif_codec_dt_match[]={
	{	.compatible = "amlogic,aml-spdif-codec",
	},
	{},
};
#else
#define amlogic_spdif_codec_dt_match NULL
#endif


static struct platform_driver spdif_dit_driver = {
	.probe		= spdif_dit_probe,
	.remove		= spdif_dit_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = amlogic_spdif_codec_dt_match,
	},
};
static int __init spdif_codec_init(void)
{
	return platform_driver_register(&spdif_dit_driver);
}

static void __exit spdif_codec_exit(void)
{
	platform_driver_unregister(&spdif_dit_driver);
}

module_init(spdif_codec_init);
module_exit(spdif_codec_exit);

MODULE_AUTHOR("Steve Chen <schen@mvista.com>");
MODULE_DESCRIPTION("SPDIF dummy codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

