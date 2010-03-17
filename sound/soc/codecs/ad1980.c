/*
 * ad1980.c  --  ALSA Soc AD1980 codec support
 *
 * Copyright:	Analog Device Inc.
 * Author:	Roy Huang <roy.huang@analog.com>
 * 		Cliff Cai <cliff.cai@analog.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "ad1980.h"

static unsigned int ac97_read(struct snd_soc_codec *codec,
	unsigned int reg);
static int ac97_write(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int val);

/*
 * AD1980 register cache
 */
static const u16 ad1980_reg[] = {
	0x0090, 0x8000, 0x8000, 0x8000, /* 0 - 6  */
	0x0000, 0x0000, 0x8008, 0x8008, /* 8 - e  */
	0x8808, 0x8808, 0x0000, 0x8808, /* 10 - 16 */
	0x8808, 0x0000, 0x8000, 0x0000, /* 18 - 1e */
	0x0000, 0x0000, 0x0000, 0x0000, /* 20 - 26 */
	0x03c7, 0x0000, 0xbb80, 0xbb80, /* 28 - 2e */
	0xbb80, 0xbb80, 0x0000, 0x8080, /* 30 - 36 */
	0x8080, 0x2000, 0x0000, 0x0000, /* 38 - 3e */
	0x0000, 0x0000, 0x0000, 0x0000, /* reserved */
	0x0000, 0x0000, 0x0000, 0x0000, /* reserved */
	0x0000, 0x0000, 0x0000, 0x0000, /* reserved */
	0x0000, 0x0000, 0x0000, 0x0000, /* reserved */
	0x8080, 0x0000, 0x0000, 0x0000, /* 60 - 66 */
	0x0000, 0x0000, 0x0000, 0x0000, /* reserved */
	0x0000, 0x0000, 0x1001, 0x0000, /* 70 - 76 */
	0x0000, 0x0000, 0x4144, 0x5370  /* 78 - 7e */
};

static const char *ad1980_rec_sel[] = {"Mic", "CD", "NC", "AUX", "Line",
		"Stereo Mix", "Mono Mix", "Phone"};

static const struct soc_enum ad1980_cap_src =
	SOC_ENUM_DOUBLE(AC97_REC_SEL, 8, 0, 7, ad1980_rec_sel);

static const struct snd_kcontrol_new ad1980_snd_ac97_controls[] = {
SOC_DOUBLE("Master Playback Volume", AC97_MASTER, 8, 0, 31, 1),
SOC_SINGLE("Master Playback Switch", AC97_MASTER, 15, 1, 1),

SOC_DOUBLE("Headphone Playback Volume", AC97_HEADPHONE, 8, 0, 31, 1),
SOC_SINGLE("Headphone Playback Switch", AC97_HEADPHONE, 15, 1, 1),

SOC_DOUBLE("PCM Playback Volume", AC97_PCM, 8, 0, 31, 1),
SOC_SINGLE("PCM Playback Switch", AC97_PCM, 15, 1, 1),

SOC_DOUBLE("PCM Capture Volume", AC97_REC_GAIN, 8, 0, 31, 0),
SOC_SINGLE("PCM Capture Switch", AC97_REC_GAIN, 15, 1, 1),

SOC_SINGLE("Mono Playback Volume", AC97_MASTER_MONO, 0, 31, 1),
SOC_SINGLE("Mono Playback Switch", AC97_MASTER_MONO, 15, 1, 1),

SOC_SINGLE("Phone Capture Volume", AC97_PHONE, 0, 31, 1),
SOC_SINGLE("Phone Capture Switch", AC97_PHONE, 15, 1, 1),

SOC_SINGLE("Mic Volume", AC97_MIC, 0, 31, 1),
SOC_SINGLE("Mic Switch", AC97_MIC, 15, 1, 1),

SOC_SINGLE("Stereo Mic Switch", AC97_AD_MISC, 6, 1, 0),
SOC_DOUBLE("Line HP Swap Switch", AC97_AD_MISC, 10, 5, 1, 0),

SOC_DOUBLE("Surround Playback Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
SOC_DOUBLE("Surround Playback Switch", AC97_SURROUND_MASTER, 15, 7, 1, 1),

SOC_DOUBLE("Center/LFE Playback Volume", AC97_CENTER_LFE_MASTER, 8, 0, 31, 1),
SOC_DOUBLE("Center/LFE Playback Switch", AC97_CENTER_LFE_MASTER, 15, 7, 1, 1),

SOC_ENUM("Capture Source", ad1980_cap_src),

SOC_SINGLE("Mic Boost Switch", AC97_MIC, 6, 1, 0),
};

static unsigned int ac97_read(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;

	switch (reg) {
	case AC97_RESET:
	case AC97_INT_PAGING:
	case AC97_POWERDOWN:
	case AC97_EXTENDED_STATUS:
	case AC97_VENDOR_ID1:
	case AC97_VENDOR_ID2:
		return soc_ac97_ops.read(codec->ac97, reg);
	default:
		reg = reg >> 1;

		if (reg >= ARRAY_SIZE(ad1980_reg))
			return -EINVAL;

		return cache[reg];
	}
}

static int ac97_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int val)
{
	u16 *cache = codec->reg_cache;

	soc_ac97_ops.write(codec->ac97, reg, val);
	reg = reg >> 1;
	if (reg < ARRAY_SIZE(ad1980_reg))
		cache[reg] = val;

	return 0;
}

struct snd_soc_dai_driver ad1980_dai = {
	.name = "ad1980-hifi",
	.ac97_control = 1,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SND_SOC_STD_AC97_FMTS, },
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SND_SOC_STD_AC97_FMTS, },
};
EXPORT_SYMBOL_GPL(ad1980_dai);

static int ad1980_reset(struct snd_soc_codec *codec, int try_warm)
{
	u16 retry_cnt = 0;

retry:
	if (try_warm && soc_ac97_ops.warm_reset) {
		soc_ac97_ops.warm_reset(codec->ac97);
		if (ac97_read(codec, AC97_RESET) == 0x0090)
			return 1;
	}

	soc_ac97_ops.reset(codec->ac97);
	/* Set bit 16slot in register 74h, then every slot will has only 16
	 * bits. This command is sent out in 20bit mode, in which case the
	 * first nibble of data is eaten by the addr. (Tag is always 16 bit)*/
	ac97_write(codec, AC97_AD_SERIAL_CFG, 0x9900);

	if (ac97_read(codec, AC97_RESET)  != 0x0090)
		goto err;
	return 0;

err:
	while (retry_cnt++ < 10)
		goto retry;

	printk(KERN_ERR "AD1980 AC97 reset failed\n");
	return -EIO;
}

static int ad1980_soc_probe(struct snd_soc_codec *codec)
{
	int ret;
	u16 vendor_id2;
	u16 ext_status;

	printk(KERN_INFO "AD1980 SoC Audio Codec\n");

	ret = snd_soc_new_ac97_codec(codec, &soc_ac97_ops, 0);
	if (ret < 0) {
		printk(KERN_ERR "ad1980: failed to register AC97 codec\n");
		return ret;
	}

	ret = ad1980_reset(codec, 0);
	if (ret < 0) {
		printk(KERN_ERR "Failed to reset AD1980: AC97 link error\n");
		goto reset_err;
	}

	/* Read out vendor ID to make sure it is ad1980 */
	if (ac97_read(codec, AC97_VENDOR_ID1) != 0x4144)
		goto reset_err;

	vendor_id2 = ac97_read(codec, AC97_VENDOR_ID2);

	if (vendor_id2 != 0x5370) {
		if (vendor_id2 != 0x5374)
			goto reset_err;
		else
			printk(KERN_WARNING "ad1980: "
				"Found AD1981 - only 2/2 IN/OUT Channels "
				"supported\n");
	}

	/* unmute captures and playbacks volume */
	ac97_write(codec, AC97_MASTER, 0x0000);
	ac97_write(codec, AC97_PCM, 0x0000);
	ac97_write(codec, AC97_REC_GAIN, 0x0000);
	ac97_write(codec, AC97_CENTER_LFE_MASTER, 0x0000);
	ac97_write(codec, AC97_SURROUND_MASTER, 0x0000);

	/*power on LFE/CENTER/Surround DACs*/
	ext_status = ac97_read(codec, AC97_EXTENDED_STATUS);
	ac97_write(codec, AC97_EXTENDED_STATUS, ext_status&~0x3800);

	snd_soc_add_controls(codec, ad1980_snd_ac97_controls,
				ARRAY_SIZE(ad1980_snd_ac97_controls));

	return 0;

reset_err:
	snd_soc_free_ac97_codec(codec);
	return ret;
}

static int ad1980_soc_remove(struct snd_soc_codec *codec)
{
	snd_soc_free_ac97_codec(codec);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_ad1980 = {
	.probe = 	ad1980_soc_probe,
	.remove = 	ad1980_soc_remove,
	.reg_cache_size = ARRAY_SIZE(ad1980_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 2,
	.write = ac97_write,
	.read = ac97_read,
};

static __devinit int ad1980_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_ad1980, &ad1980_dai, 1);
}

static int __devexit ad1980_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver ad1980_codec_driver = {
	.driver = {
			.name = "ad1980-codec",
			.owner = THIS_MODULE,
	},

	.probe = ad1980_probe,
	.remove = __devexit_p(ad1980_remove),
};

static int __init ad1980_init(void)
{
	return platform_driver_register(&ad1980_codec_driver);
}
module_init(ad1980_init);

static void __exit ad1980_exit(void)
{
	platform_driver_unregister(&ad1980_codec_driver);
}
module_exit(ad1980_exit);

MODULE_DESCRIPTION("ASoC ad1980 driver");
MODULE_AUTHOR("Roy Huang, Cliff Cai");
MODULE_LICENSE("GPL");
