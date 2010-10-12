/*
 * ALSA SoC SPDIF DIT driver
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

#include "spdif_transciever.h"

MODULE_LICENSE("GPL");

#define STUB_RATES	SNDRV_PCM_RATE_8000_96000
#define STUB_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

static struct snd_soc_codec *spdif_dit_codec;

static int spdif_dit_codec_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret;

	if (spdif_dit_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = spdif_dit_codec;
	codec = spdif_dit_codec;

	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto err_create_pcms;
	}

	return 0;

err_create_pcms:
	return ret;
}

static int spdif_dit_codec_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_spdif_dit = {
	.probe		= spdif_dit_codec_probe,
	.remove		= spdif_dit_codec_remove,
}; EXPORT_SYMBOL_GPL(soc_codec_dev_spdif_dit);

struct snd_soc_dai dit_stub_dai = {
	.name		= "DIT",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};
EXPORT_SYMBOL_GPL(dit_stub_dai);

static int spdif_dit_probe(struct platform_device *pdev)
{
	struct snd_soc_codec *codec;
	int ret;

	if (spdif_dit_codec) {
		dev_err(&pdev->dev, "Another Codec is registered\n");
		ret = -EINVAL;
		goto err_reg_codec;
	}

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	codec->dev = &pdev->dev;

	mutex_init(&codec->mutex);

	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->name = "spdif-dit";
	codec->owner = THIS_MODULE;
	codec->dai = &dit_stub_dai;
	codec->num_dai = 1;

	spdif_dit_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto err_reg_codec;
	}

	dit_stub_dai.dev = &pdev->dev;
	ret = snd_soc_register_dai(&dit_stub_dai);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to register dai: %d\n", ret);
		goto err_reg_dai;
	}

	return 0;

err_reg_dai:
	snd_soc_unregister_codec(codec);
err_reg_codec:
	kfree(spdif_dit_codec);
	return ret;
}

static int spdif_dit_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&dit_stub_dai);
	snd_soc_unregister_codec(spdif_dit_codec);
	kfree(spdif_dit_codec);
	spdif_dit_codec = NULL;
	return 0;
}

static struct platform_driver spdif_dit_driver = {
	.probe		= spdif_dit_probe,
	.remove		= spdif_dit_remove,
	.driver		= {
		.name	= "spdif-dit",
		.owner	= THIS_MODULE,
	},
};

static int __init dit_modinit(void)
{
	return platform_driver_register(&spdif_dit_driver);
}

static void __exit dit_exit(void)
{
	platform_driver_unregister(&spdif_dit_driver);
}

module_init(dit_modinit);
module_exit(dit_exit);

