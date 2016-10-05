/*
 * wm8711.c  --  WM8711 ALSA SoC Audio driver
 *
 * Copyright 2006 Wolfson Microelectronics
 *
 * Author: Mike Arthur <Mike.Arthur@wolfsonmicro.com>
 *
 * Based on wm8731.c by Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/initval.h>

#include "wm8711.h"

/* codec private data */
struct wm8711_priv {
	struct regmap *regmap;
	unsigned int sysclk;
};

/*
 * wm8711 register cache
 * We can't read the WM8711 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const struct reg_default wm8711_reg_defaults[] = {
	{ 0, 0x0079 }, { 1, 0x0079 }, { 2, 0x000a }, { 3, 0x0008 },
	{ 4, 0x009f }, { 5, 0x000a }, { 6, 0x0000 }, { 7, 0x0000 },
};

static bool wm8711_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8711_RESET:
		return true;
	default:
		return false;
	}
}

#define wm8711_reset(c)	snd_soc_write(c, WM8711_RESET, 0)

static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);

static const struct snd_kcontrol_new wm8711_snd_controls[] = {

SOC_DOUBLE_R_TLV("Master Playback Volume", WM8711_LOUT1V, WM8711_ROUT1V,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Master Playback ZC Switch", WM8711_LOUT1V, WM8711_ROUT1V,
	7, 1, 0),

};

/* Output Mixer */
static const struct snd_kcontrol_new wm8711_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8711_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", WM8711_APANA, 4, 1, 0),
};

static const struct snd_soc_dapm_widget wm8711_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Output Mixer", WM8711_PWR, 4, 1,
	&wm8711_output_mixer_controls[0],
	ARRAY_SIZE(wm8711_output_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8711_PWR, 3, 1),
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
};

static const struct snd_soc_dapm_route wm8711_intercon[] = {
	/* output mixer */
	{"Output Mixer", "Line Bypass Switch", "Line Input"},
	{"Output Mixer", "HiFi Playback Switch", "DAC"},

	/* outputs */
	{"RHPOUT", NULL, "Output Mixer"},
	{"ROUT", NULL, "Output Mixer"},
	{"LHPOUT", NULL, "Output Mixer"},
	{"LOUT", NULL, "Output Mixer"},
};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:4;
	u8 bosr:1;
	u8 usb:1;
};

/* codec mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 48k */
	{12288000, 48000, 256, 0x0, 0x0, 0x0},
	{18432000, 48000, 384, 0x0, 0x1, 0x0},
	{12000000, 48000, 250, 0x0, 0x0, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0x6, 0x0, 0x0},
	{18432000, 32000, 576, 0x6, 0x1, 0x0},
	{12000000, 32000, 375, 0x6, 0x0, 0x1},

	/* 8k */
	{12288000, 8000, 1536, 0x3, 0x0, 0x0},
	{18432000, 8000, 2304, 0x3, 0x1, 0x0},
	{11289600, 8000, 1408, 0xb, 0x0, 0x0},
	{16934400, 8000, 2112, 0xb, 0x1, 0x0},
	{12000000, 8000, 1500, 0x3, 0x0, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0x7, 0x0, 0x0},
	{18432000, 96000, 192, 0x7, 0x1, 0x0},
	{12000000, 96000, 125, 0x7, 0x0, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x8, 0x0, 0x0},
	{16934400, 44100, 384, 0x8, 0x1, 0x0},
	{12000000, 44100, 272, 0x8, 0x1, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0xf, 0x0, 0x0},
	{16934400, 88200, 192, 0xf, 0x1, 0x0},
	{12000000, 88200, 136, 0xf, 0x1, 0x1},
};

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return 0;
}

static int wm8711_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8711_priv *wm8711 =  snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8711_IFACE) & 0xfff3;
	int i = get_coeff(wm8711->sysclk, params_rate(params));
	u16 srate = (coeff_div[i].sr << 2) |
		(coeff_div[i].bosr << 1) | coeff_div[i].usb;

	snd_soc_write(codec, WM8711_SRATE, srate);

	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface |= 0x0004;
		break;
	case 24:
		iface |= 0x0008;
		break;
	}

	snd_soc_write(codec, WM8711_IFACE, iface);
	return 0;
}

static int wm8711_pcm_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	/* set active */
	snd_soc_write(codec, WM8711_ACTIVE, 0x0001);

	return 0;
}

static void wm8711_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	/* deactivate */
	if (!snd_soc_codec_is_active(codec)) {
		udelay(50);
		snd_soc_write(codec, WM8711_ACTIVE, 0x0);
	}
}

static int wm8711_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8711_APDIGI) & 0xfff7;

	if (mute)
		snd_soc_write(codec, WM8711_APDIGI, mute_reg | 0x8);
	else
		snd_soc_write(codec, WM8711_APDIGI, mute_reg);

	return 0;
}

static int wm8711_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8711_priv *wm8711 =  snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		wm8711->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8711_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = snd_soc_read(codec, WM8711_IFACE) & 0x000c;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= 0x0040;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	snd_soc_write(codec, WM8711_IFACE, iface);
	return 0;
}

static int wm8711_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	struct wm8711_priv *wm8711 = snd_soc_codec_get_drvdata(codec);
	u16 reg = snd_soc_read(codec, WM8711_PWR) & 0xff7f;

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_write(codec, WM8711_PWR, reg);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF)
			regcache_sync(wm8711->regmap);

		snd_soc_write(codec, WM8711_PWR, reg | 0x0040);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, WM8711_ACTIVE, 0x0);
		snd_soc_write(codec, WM8711_PWR, 0xffff);
		break;
	}
	return 0;
}

#define WM8711_RATES SNDRV_PCM_RATE_8000_96000

#define WM8711_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8711_ops = {
	.prepare = wm8711_pcm_prepare,
	.hw_params = wm8711_hw_params,
	.shutdown = wm8711_shutdown,
	.digital_mute = wm8711_mute,
	.set_sysclk = wm8711_set_dai_sysclk,
	.set_fmt = wm8711_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8711_dai = {
	.name = "wm8711-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8711_RATES,
		.formats = WM8711_FORMATS,
	},
	.ops = &wm8711_ops,
};

static int wm8711_probe(struct snd_soc_codec *codec)
{
	int ret;

	ret = wm8711_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	/* Latch the update bits */
	snd_soc_update_bits(codec, WM8711_LOUT1V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8711_ROUT1V, 0x0100, 0x0100);

	return ret;

}

static const struct snd_soc_codec_driver soc_codec_dev_wm8711 = {
	.probe =	wm8711_probe,
	.set_bias_level = wm8711_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls		= wm8711_snd_controls,
		.num_controls		= ARRAY_SIZE(wm8711_snd_controls),
		.dapm_widgets		= wm8711_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(wm8711_dapm_widgets),
		.dapm_routes		= wm8711_intercon,
		.num_dapm_routes	= ARRAY_SIZE(wm8711_intercon),
	},
};

static const struct of_device_id wm8711_of_match[] = {
	{ .compatible = "wlf,wm8711", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8711_of_match);

static const struct regmap_config wm8711_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8711_RESET,

	.reg_defaults = wm8711_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8711_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm8711_volatile,
};

#if defined(CONFIG_SPI_MASTER)
static int wm8711_spi_probe(struct spi_device *spi)
{
	struct wm8711_priv *wm8711;
	int ret;

	wm8711 = devm_kzalloc(&spi->dev, sizeof(struct wm8711_priv),
			      GFP_KERNEL);
	if (wm8711 == NULL)
		return -ENOMEM;

	wm8711->regmap = devm_regmap_init_spi(spi, &wm8711_regmap);
	if (IS_ERR(wm8711->regmap))
		return PTR_ERR(wm8711->regmap);

	spi_set_drvdata(spi, wm8711);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_wm8711, &wm8711_dai, 1);

	return ret;
}

static int wm8711_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);

	return 0;
}

static struct spi_driver wm8711_spi_driver = {
	.driver = {
		.name	= "wm8711",
		.of_match_table = wm8711_of_match,
	},
	.probe		= wm8711_spi_probe,
	.remove		= wm8711_spi_remove,
};
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
static int wm8711_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct wm8711_priv *wm8711;
	int ret;

	wm8711 = devm_kzalloc(&client->dev, sizeof(struct wm8711_priv),
			      GFP_KERNEL);
	if (wm8711 == NULL)
		return -ENOMEM;

	wm8711->regmap = devm_regmap_init_i2c(client, &wm8711_regmap);
	if (IS_ERR(wm8711->regmap))
		return PTR_ERR(wm8711->regmap);

	i2c_set_clientdata(client, wm8711);

	ret =  snd_soc_register_codec(&client->dev,
			&soc_codec_dev_wm8711, &wm8711_dai, 1);

	return ret;
}

static int wm8711_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id wm8711_i2c_id[] = {
	{ "wm8711", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8711_i2c_id);

static struct i2c_driver wm8711_i2c_driver = {
	.driver = {
		.name = "wm8711",
		.of_match_table = wm8711_of_match,
	},
	.probe =    wm8711_i2c_probe,
	.remove =   wm8711_i2c_remove,
	.id_table = wm8711_i2c_id,
};
#endif

static int __init wm8711_modinit(void)
{
	int ret;
#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8711_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8711 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8711_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8711 SPI driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8711_modinit);

static void __exit wm8711_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8711_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8711_spi_driver);
#endif
}
module_exit(wm8711_exit);

MODULE_DESCRIPTION("ASoC WM8711 driver");
MODULE_AUTHOR("Mike Arthur");
MODULE_LICENSE("GPL");
