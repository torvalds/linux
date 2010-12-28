/*
 * fsi-da7210.c
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/platform_device.h>
#include <sound/sh_fsi.h>

static int fsi_da7210_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;

	return snd_soc_dai_set_fmt(dai,
				   SND_SOC_DAIFMT_I2S |
				   SND_SOC_DAIFMT_CBM_CFM);
}

static struct snd_soc_dai_link fsi_da7210_dai = {
	.name		= "DA7210",
	.stream_name	= "DA7210",
	.cpu_dai_name	= "fsib-dai", /* FSI B */
	.codec_dai_name	= "da7210-hifi",
	.platform_name	= "sh_fsi.0",
	.codec_name	= "da7210-codec.0-001a",
	.init		= fsi_da7210_init,
};

static struct snd_soc_card fsi_soc_card = {
	.name		= "FSI (DA7210)",
	.dai_link	= &fsi_da7210_dai,
	.num_links	= 1,
};

static struct platform_device *fsi_da7210_snd_device;

static int __init fsi_da7210_sound_init(void)
{
	int ret;

	fsi_da7210_snd_device = platform_device_alloc("soc-audio", FSI_PORT_B);
	if (!fsi_da7210_snd_device)
		return -ENOMEM;

	platform_set_drvdata(fsi_da7210_snd_device, &fsi_soc_card);
	ret = platform_device_add(fsi_da7210_snd_device);
	if (ret)
		platform_device_put(fsi_da7210_snd_device);

	return ret;
}

static void __exit fsi_da7210_sound_exit(void)
{
	platform_device_unregister(fsi_da7210_snd_device);
}

module_init(fsi_da7210_sound_init);
module_exit(fsi_da7210_sound_exit);

/* Module information */
MODULE_DESCRIPTION("ALSA SoC FSI DA2710");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
MODULE_LICENSE("GPL");
