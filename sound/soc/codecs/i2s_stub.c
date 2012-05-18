/*
 * ALSA SoC I2S Stub Codec driver
 *
 * This driver is used by controllers which can operate in I2S mode with
 * Stub codec driver for I2S mode.
 *
 * The code is based on sound/soc/codec/spdif_transceiver.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>


#define I2S_STUB_RATES		SNDRV_PCM_RATE_8000_192000
#define I2S_STUB_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

static struct snd_soc_codec_driver soc_codec_i2s_stub;

static struct snd_soc_dai_driver i2s_stub_dai = {
	.name		= "i2s-stub-hifi",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= I2S_STUB_RATES,
		.formats	= I2S_STUB_FORMATS,
	},
};

static int i2s_stub_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_i2s_stub,
			&i2s_stub_dai, 1);
}

static int i2s_stub_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id i2s_stub_dt_ids[] = {
	{ .compatible = "linux,i2s-stub", },
	{ }
};
MODULE_DEVICE_TABLE(of, i2s_stub_dt_ids);
#endif

static struct platform_driver i2s_stub_driver = {
	.probe	= i2s_stub_probe,
	.remove	= i2s_stub_remove,
	.driver	= {
		.name	= "i2s-stub",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(i2s_stub_dt_ids),
	},
};

module_platform_driver(i2s_stub_driver);

MODULE_AUTHOR("Tushar Behera");
MODULE_DESCRIPTION("I2S stub codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: i2s-stub");
