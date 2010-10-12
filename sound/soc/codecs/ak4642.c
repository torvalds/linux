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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

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

/* PW_MGMT2 */
#define HPMTN		(1 << 6)
#define PMHPL		(1 << 5)
#define PMHPR		(1 << 4)
#define MS		(1 << 3) /* master/slave select */
#define MCKO		(1 << 1)
#define PMPLL		(1 << 0)

#define PMHP_MASK	(PMHPL | PMHPR)
#define PMHP		PMHP_MASK

/* MD_CTL1 */
#define PLL3		(1 << 7)
#define PLL2		(1 << 6)
#define PLL1		(1 << 5)
#define PLL0		(1 << 4)
#define PLL_MASK	(PLL3 | PLL2 | PLL1 | PLL0)

#define BCKO_MASK	(1 << 3)
#define BCKO_64		BCKO_MASK

/* MD_CTL2 */
#define FS0		(1 << 0)
#define FS1		(1 << 1)
#define FS2		(1 << 2)
#define FS3		(1 << 5)
#define FS_MASK		(FS0 | FS1 | FS2 | FS3)

struct snd_soc_codec_device soc_codec_dev_ak4642;

/*
 * Playback Volume (table 39)
 *
 * max : 0x00 : +12.0 dB
 *       ( 0.5 dB step )
 * min : 0xFE : -115.0 dB
 * mute: 0xFF
 */
static const DECLARE_TLV_DB_SCALE(out_tlv, -11500, 50, 1);

static const struct snd_kcontrol_new ak4642_snd_controls[] = {

	SOC_DOUBLE_R_TLV("Digital Playback Volume", L_DVC, R_DVC,
			 0, 0xFF, 1, out_tlv),
};


/* codec private data */
struct ak4642_priv {
	struct snd_soc_codec codec;
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
		 * Bass Boost Level : Middle
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p97.
		 */
		ak4642_write(codec, 0x0f, 0x09);
		ak4642_write(codec, 0x0e, 0x19);
		ak4642_write(codec, 0x09, 0x91);
		ak4642_write(codec, 0x0c, 0x91);
		ak4642_write(codec, 0x00, 0x64);
		snd_soc_update_bits(codec, PW_MGMT2, PMHP_MASK,	PMHP);
		snd_soc_update_bits(codec, PW_MGMT2, HPMTN,	HPMTN);
	} else {
		/*
		 * start stereo input
		 *
		 * PLL Master Mode
		 * Audio I/F Format:MSB justified (ADC & DAC)
		 * Pre MIC AMP:+20dB
		 * MIC Power On
		 * ALC setting:Refer to Table 35
		 * ALC bit=“1”
		 *
		 * This operation came from example code of
		 * "ASAHI KASEI AK4642" (japanese) manual p94.
		 */
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
		snd_soc_update_bits(codec, PW_MGMT2, HPMTN,	0);
		snd_soc_update_bits(codec, PW_MGMT2, PMHP_MASK,	0);
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
	u8 pll;

	switch (freq) {
	case 11289600:
		pll = PLL2;
		break;
	case 12288000:
		pll = PLL2 | PLL0;
		break;
	case 12000000:
		pll = PLL2 | PLL1;
		break;
	case 24000000:
		pll = PLL2 | PLL1 | PLL0;
		break;
	case 13500000:
		pll = PLL3 | PLL2;
		break;
	case 27000000:
		pll = PLL3 | PLL2 | PLL0;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, MD_CTL1, PLL_MASK, pll);

	return 0;
}

static int ak4642_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 data;
	u8 bcko;

	data = MCKO | PMPLL; /* use MCKO */
	bcko = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		data |= MS;
		bcko = BCKO_64;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, PW_MGMT2, MS, data);
	snd_soc_update_bits(codec, MD_CTL1, BCKO_MASK, bcko);

	return 0;
}

static int ak4642_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 rate;

	switch (params_rate(params)) {
	case 7350:
		rate = FS2;
		break;
	case 8000:
		rate = 0;
		break;
	case 11025:
		rate = FS2 | FS0;
		break;
	case 12000:
		rate = FS0;
		break;
	case 14700:
		rate = FS2 | FS1;
		break;
	case 16000:
		rate = FS1;
		break;
	case 22050:
		rate = FS2 | FS1 | FS0;
		break;
	case 24000:
		rate = FS1 | FS0;
		break;
	case 29400:
		rate = FS3 | FS2 | FS1;
		break;
	case 32000:
		rate = FS3 | FS1;
		break;
	case 44100:
		rate = FS3 | FS2 | FS1 | FS0;
		break;
	case 48000:
		rate = FS3 | FS1 | FS0;
		break;
	default:
		return -EINVAL;
		break;
	}
	snd_soc_update_bits(codec, MD_CTL2, FS_MASK, rate);

	return 0;
}

static struct snd_soc_dai_ops ak4642_dai_ops = {
	.startup	= ak4642_dai_startup,
	.shutdown	= ak4642_dai_shutdown,
	.set_sysclk	= ak4642_dai_set_sysclk,
	.set_fmt	= ak4642_dai_set_fmt,
	.hw_params	= ak4642_dai_hw_params,
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
	.symmetric_rates = 1,
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

	snd_soc_codec_set_drvdata(codec, ak4642);
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
	if (ret < 0) {
		printk(KERN_ERR "failed to initialise AK4642\n");
		kfree(ak4642);
	}

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

	snd_soc_add_controls(ak4642_codec, ak4642_snd_controls,
			     ARRAY_SIZE(ak4642_snd_controls));

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
	int ret = 0;
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
