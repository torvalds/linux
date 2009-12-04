/*
 * ak4642.c  --  AK4642/AK4643 ALSA Soc Audio driver
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on wm8731.c by Richard Purdie
 * Based on ak4535.c by Richard Purdie
 * Based on wm8753.c by Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* ** CAUTION **
 *
 * This is very simple driver.
 * It can use headphone output / stereo input only
 *
 * AK4642 is not tested.
 * AK4643 is tested.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "ak4642.h"

#define AK4642_VERSION "0.0.1"

#define PW_MGMT1	0x00
#define PW_MGMT2	0x01
#define SG_SL1		0x02
#define SG_SL2		0x03
#define MD_CTL1		0x04
#define MD_CTL2		0x05
#define TIMER		0x06
#define ALC_CTL1	0x07
#define ALC_CTL2	0x08
#define L_IVC		0x09
#define L_DVC		0x0a
#define ALC_CTL3	0x0b
#define R_IVC		0x0c
#define R_DVC		0x0d
#define MD_CTL3		0x0e
#define MD_CTL4		0x0f
#define PW_MGMT3	0x10
#define DF_S		0x11
#define FIL3_0		0x12
#define FIL3_1		0x13
#define FIL3_2		0x14
#define FIL3_3		0x15
#define EQ_0		0x16
#define EQ_1		0x17
#define EQ_2		0x18
#define EQ_3		0x19
#define EQ_4		0x1a
#define EQ_5		0x1b
#define FIL1_0		0x1c
#define FIL1_1		0x1d
#define FIL1_2		0x1e
#define FIL1_3		0x1f
#define PW_MGMT4	0x20
#define MD_CTL5		0x21
#define LO_MS		0x22
#define HP_MS		0x23
#define SPK_MS		0x24

#define AK4642_CACHEREGNUM 	0x25

struct snd_soc_codec_device soc_codec_dev_ak4642;

/* codec private data */
struct ak4642_priv {
	struct snd_soc_codec codec;
	unsigned int sysclk;
};

static struct snd_soc_codec *ak4642_codec;

/*
 * ak4642 register cache
 */
static const u16 ak4642_reg[AK4642_CACHEREGNUM] = {
	0x0000, 0x0000, 0x0001, 0x0000,
	0x0002, 0x0000, 0x0000, 0x0000,
	0x00e1, 0x00e1, 0x0018, 0x0000,
	0x00e1, 0x0018, 0x0011, 0x0008,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000,
};

/*
 * read ak4642 register cache
 */
static inline unsigned int ak4642_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg >= AK4642_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write ak4642 register cache
 */
static inline void ak4642_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= AK4642_CACHEREGNUM)
		return;

	cache[reg] = value;
}

/*
 * write to the AK4642 register space
 */
static int ak4642_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D8 AK4642 register offset
	 *   D7...D0 register data
	 */
	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	if (codec->hw_write(codec->control_data, data, 2) == 2) {
		ak4642_write_reg_cache(codec, reg, value);
		return 0;
	} else
		return -EIO;
}

static int ak4642_sync(struct snd_soc_codec *codec)
{
	u16 *cache = codec->reg_cache;
	int i, r = 0;

	for (i = 0; i < AK4642_CACHEREGNUM; i++)
		r |= ak4642_write(codec, i, cache[i]);

	return r;
};

static int ak4642_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_codec *codec = dai->codec;

	if (is_play) {
		/*
		 * start headphone output
		 *
		 * PLL, Master Mode
		 * Audio I/F Format :MSB justified (ADC & DAC)
		 * Sampling Frequency: 44.1kHz
		 * Digital Volume: −8dB
		 * Bass Boost Level : Middle
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p97.
		 *
		 * Example code use 0x39, 0x79 value for 0x01 address,
		 * But we need MCKO (0x02) bit now
		 */
		ak4642_write(codec, 0x05, 0x27);
		ak4642_write(codec, 0x0f, 0x09);
		ak4642_write(codec, 0x0e, 0x19);
		ak4642_write(codec, 0x09, 0x91);
		ak4642_write(codec, 0x0c, 0x91);
		ak4642_write(codec, 0x0a, 0x28);
		ak4642_write(codec, 0x0d, 0x28);
		ak4642_write(codec, 0x00, 0x64);
		ak4642_write(codec, 0x01, 0x3b); /* + MCKO bit */
		ak4642_write(codec, 0x01, 0x7b); /* + MCKO bit */
	} else {
		/*
		 * start stereo input
		 *
		 * PLL Master Mode
		 * Audio I/F Format:MSB justified (ADC & DAC)
		 * Sampling Frequency:44.1kHz
		 * Pre MIC AMP:+20dB
		 * MIC Power On
		 * ALC setting:Refer to Table 35
		 * ALC bit=“1”
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p94.
		 */
		ak4642_write(codec, 0x05, 0x27);
		ak4642_write(codec, 0x02, 0x05);
		ak4642_write(codec, 0x06, 0x3c);
		ak4642_write(codec, 0x08, 0xe1);
		ak4642_write(codec, 0x0b, 0x00);
		ak4642_write(codec, 0x07, 0x21);
		ak4642_write(codec, 0x00, 0x41);
		ak4642_write(codec, 0x10, 0x01);
	}

	return 0;
}

static void ak4642_dai_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_codec *codec = dai->codec;

	if (is_play) {
		/* stop headphone output */
		ak4642_write(codec, 0x01, 0x3b);
		ak4642_write(codec, 0x01, 0x0b);
		ak4642_write(codec, 0x00, 0x40);
		ak4642_write(codec, 0x0e, 0x11);
		ak4642_write(codec, 0x0f, 0x08);
	} else {
		/* stop stereo input */
		ak4642_write(codec, 0x00, 0x40);
		ak4642_write(codec, 0x10, 0x00);
		ak4642_write(codec, 0x07, 0x01);
	}
}

static int ak4642_dai_set_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ak4642_priv *ak4642 = codec->private_data;

	ak4642->sysclk = freq;
	return 0;
}

static struct snd_soc_dai_ops ak4642_dai_ops = {
	.startup	= ak4642_dai_startup,
	.shutdown	= ak4642_dai_shutdown,
	.set_sysclk	= ak4642_dai_set_sysclk,
};

struct snd_soc_dai ak4642_dai = {
	.name = "AK4642",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE },
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &ak4642_dai_ops,
};
EXPORT_SYMBOL_GPL(ak4642_dai);

static int ak4642_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	ak4642_sync(codec);
	return 0;
}

/*
 * initialise the AK4642 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int ak4642_init(struct ak4642_priv *ak4642)
{
	struct snd_soc_codec *codec = &ak4642->codec;
	int ret = 0;

	if (ak4642_codec) {
		dev_err(codec->dev, "Another ak4642 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data	= ak4642;
	codec->name		= "AK4642";
	codec->owner		= THIS_MODULE;
	codec->read		= ak4642_read_reg_cache;
	codec->write		= ak4642_write;
	codec->dai		= &ak4642_dai;
	codec->num_dai		= 1;
	codec->hw_write		= (hw_write_t)i2c_master_send;
	codec->reg_cache_size	= ARRAY_SIZE(ak4642_reg);
	codec->reg_cache	= kmemdup(ak4642_reg,
					  sizeof(ak4642_reg), GFP_KERNEL);

	if (!codec->reg_cache)
		return -ENOMEM;

	ak4642_dai.dev = codec->dev;
	ak4642_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto reg_cache_err;
	}

	ret = snd_soc_register_dai(&ak4642_dai);
	if (ret) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		goto reg_cache_err;
	}

	/*
	 * clock setting
	 *
	 * Audio I/F Format: MSB justified (ADC & DAC)
	 * BICK frequency at Master Mode: 64fs
	 * Input Master Clock Select at PLL Mode: 11.2896MHz
	 * MCKO: Enable
	 * Sampling Frequency: 44.1kHz
	 *
	 * This operation came from example code of
	 * "ASAHI KASEI AK4642" (japanese) manual p89.
	 *
	 * please fix-me
	 */
	ak4642_write(codec, 0x01, 0x08);
	ak4642_write(codec, 0x04, 0x4a);
	ak4642_write(codec, 0x05, 0x27);
	ak4642_write(codec, 0x00, 0x40);
	ak4642_write(codec, 0x01, 0x0b);

	return ret;

reg_cache_err:
	kfree(codec->reg_cache);
	codec->reg_cache = NULL;

	return ret;
}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int ak4642_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct ak4642_priv *ak4642;
	struct snd_soc_codec *codec;
	int ret;

	ak4642 = kzalloc(sizeof(struct ak4642_priv), GFP_KERNEL);
	if (!ak4642)
		return -ENOMEM;

	codec = &ak4642->codec;
	codec->dev = &i2c->dev;

	i2c_set_clientdata(i2c, ak4642);
	codec->control_data = i2c;

	ret = ak4642_init(ak4642);
	if (ret < 0)
		printk(KERN_ERR "failed to initialise AK4642\n");

	return ret;
}

static int ak4642_i2c_remove(struct i2c_client *client)
{
	struct ak4642_priv *ak4642 = i2c_get_clientdata(client);

	snd_soc_unregister_dai(&ak4642_dai);
	snd_soc_unregister_codec(&ak4642->codec);
	kfree(ak4642->codec.reg_cache);
	kfree(ak4642);
	ak4642_codec = NULL;

	return 0;
}

static const struct i2c_device_id ak4642_i2c_id[] = {
	{ "ak4642", 0 },
	{ "ak4643", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4642_i2c_id);

static struct i2c_driver ak4642_i2c_driver = {
	.driver = {
		.name = "AK4642 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe		= ak4642_i2c_probe,
	.remove		= ak4642_i2c_remove,
	.id_table	= ak4642_i2c_id,
};

#endif

static int ak4642_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	int ret;

	if (!ak4642_codec) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = ak4642_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "ak4642: failed to create pcms\n");
		goto pcm_err;
	}

	dev_info(&pdev->dev, "AK4642 Audio Codec %s", AK4642_VERSION);
	return ret;

pcm_err:
	return ret;

}

/* power down chip */
static int ak4642_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_ak4642 = {
	.probe =	ak4642_probe,
	.remove =	ak4642_remove,
	.resume =	ak4642_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ak4642);

static int __init ak4642_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&ak4642_i2c_driver);
#endif
	return ret;

}
module_init(ak4642_modinit);

static void __exit ak4642_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&ak4642_i2c_driver);
#endif

}
module_exit(ak4642_exit);

MODULE_DESCRIPTION("Soc AK4642 driver");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
MODULE_LICENSE("GPL");
