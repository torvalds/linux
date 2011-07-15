/*
 * ads117x.c  --  Driver for ads1174/8 ADC chips
 *
 * Copyright 2009 ShotSpotter Inc.
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#define ADS117X_RATES (SNDRV_PCM_RATE_8000_48000)
#define ADS117X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE)

static struct snd_soc_dai_driver ads117x_dai = {
/* ADC */
	.name = "ads117x-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 32,
		.rates = ADS117X_RATES,
		.formats = ADS117X_FORMATS,},
};

static struct snd_soc_codec_driver soc_codec_dev_ads117x;

static __devinit int ads117x_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_ads117x, &ads117x_dai, 1);
}

static int __devexit ads117x_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver ads117x_codec_driver = {
	.driver = {
			.name = "ads117x-codec",
			.owner = THIS_MODULE,
	},

	.probe = ads117x_probe,
	.remove = __devexit_p(ads117x_remove),
};

static int __init ads117x_init(void)
{
	return platform_driver_register(&ads117x_codec_driver);
}
module_init(ads117x_init);

static void __exit ads117x_exit(void)
{
	platform_driver_unregister(&ads117x_codec_driver);
}
module_exit(ads117x_exit);

MODULE_DESCRIPTION("ASoC ads117x driver");
MODULE_AUTHOR("Graeme Gregory");
MODULE_LICENSE("GPL");
