/*
 * wm8731.c  --  WM8731 ALSA SoC Audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on wm8753.c by Liam Girdwood
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
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wm8731.h"

static struct snd_soc_codec *wm8731_codec;
struct snd_soc_codec_device soc_codec_dev_wm8731;

/* codec private data */
struct wm8731_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[WM8731_CACHEREGNUM];
	unsigned int sysclk;
};

#ifdef CONFIG_SPI_MASTER
static int wm8731_spi_write(struct spi_device *spi, const char *data, int len);
#endif

/*
 * wm8731 register cache
 * We can't read the WM8731 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const u16 wm8731_reg[WM8731_CACHEREGNUM] = {
    0x0097, 0x0097, 0x0079, 0x0079,
    0x000a, 0x0008, 0x009f, 0x000a,
    0x0000, 0x0000
};

/*
 * read wm8731 register cache
 */
static inline unsigned int wm8731_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8731_RESET)
		return 0;
	if (reg >= WM8731_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8731 register cache
 */
static inline void wm8731_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8731_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8731 register space
 */
static int wm8731_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8731 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8731_write_reg_cache(codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8731_reset(c)	wm8731_write(c, WM8731_RESET, 0)

static const char *wm8731_input_select[] = {"Line In", "Mic"};
static const char *wm8731_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};

static const struct soc_enum wm8731_enum[] = {
	SOC_ENUM_SINGLE(WM8731_APANA, 2, 2, wm8731_input_select),
	SOC_ENUM_SINGLE(WM8731_APDIGI, 1, 4, wm8731_deemph),
};

static const struct snd_kcontrol_new wm8731_snd_controls[] = {

SOC_DOUBLE_R("Master Playback Volume", WM8731_LOUT1V, WM8731_ROUT1V,
	0, 127, 0),
SOC_DOUBLE_R("Master Playback ZC Switch", WM8731_LOUT1V, WM8731_ROUT1V,
	7, 1, 0),

SOC_DOUBLE_R("Capture Volume", WM8731_LINVOL, WM8731_RINVOL, 0, 31, 0),
SOC_DOUBLE_R("Line Capture Switch", WM8731_LINVOL, WM8731_RINVOL, 7, 1, 1),

SOC_SINGLE("Mic Boost (+20dB)", WM8731_APANA, 0, 1, 0),
SOC_SINGLE("Capture Mic Switch", WM8731_APANA, 1, 1, 1),

SOC_SINGLE("Sidetone Playback Volume", WM8731_APANA, 6, 3, 1),

SOC_SINGLE("ADC High Pass Filter Switch", WM8731_APDIGI, 0, 1, 1),
SOC_SINGLE("Store DC Offset Switch", WM8731_APDIGI, 4, 1, 0),

SOC_ENUM("Playback De-emphasis", wm8731_enum[1]),
};

/* Output Mixer */
static const struct snd_kcontrol_new wm8731_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8731_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("Mic Sidetone Switch", WM8731_APANA, 5, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", WM8731_APANA, 4, 1, 0),
};

/* Input mux */
static const struct snd_kcontrol_new wm8731_input_mux_controls =
SOC_DAPM_ENUM("Input Select", wm8731_enum[0]);

static const struct snd_soc_dapm_widget wm8731_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Output Mixer", WM8731_PWR, 4, 1,
	&wm8731_output_mixer_controls[0],
	ARRAY_SIZE(wm8731_output_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8731_PWR, 3, 1),
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8731_PWR, 2, 1),
SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0, &wm8731_input_mux_controls),
SND_SOC_DAPM_PGA("Line Input", WM8731_PWR, 0, 1, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias", WM8731_PWR, 1, 1),
SND_SOC_DAPM_INPUT("MICIN"),
SND_SOC_DAPM_INPUT("RLINEIN"),
SND_SOC_DAPM_INPUT("LLINEIN"),
};

static const struct snd_soc_dapm_route intercon[] = {
	/* output mixer */
	{"Output Mixer", "Line Bypass Switch", "Line Input"},
	{"Output Mixer", "HiFi Playback Switch", "DAC"},
	{"Output Mixer", "Mic Sidetone Switch", "Mic Bias"},

	/* outputs */
	{"RHPOUT", NULL, "Output Mixer"},
	{"ROUT", NULL, "Output Mixer"},
	{"LHPOUT", NULL, "Output Mixer"},
	{"LOUT", NULL, "Output Mixer"},

	/* input mux */
	{"Input Mux", "Line In", "Line Input"},
	{"Input Mux", "Mic", "Mic Bias"},
	{"ADC", NULL, "Input Mux"},

	/* inputs */
	{"Line Input", NULL, "LLINEIN"},
	{"Line Input", NULL, "RLINEIN"},
	{"Mic Bias", NULL, "MICIN"},
};

static int wm8731_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8731_dapm_widgets,
				  ARRAY_SIZE(wm8731_dapm_widgets));

	snd_soc_dapm_add_routes(codec, intercon, ARRAY_SIZE(intercon));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

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

static int wm8731_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8731_priv *wm8731 = codec->private_data;
	u16 iface = wm8731_read_reg_cache(codec, WM8731_IFACE) & 0xfff3;
	int i = get_coeff(wm8731->sysclk, params_rate(params));
	u16 srate = (coeff_div[i].sr << 2) |
		(coeff_div[i].bosr << 1) | coeff_div[i].usb;

	wm8731_write(codec, WM8731_SRATE, srate);

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	}

	wm8731_write(codec, WM8731_IFACE, iface);
	return 0;
}

static int wm8731_pcm_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;

	/* set active */
	wm8731_write(codec, WM8731_ACTIVE, 0x0001);

	return 0;
}

static void wm8731_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;

	/* deactivate */
	if (!codec->active) {
		udelay(50);
		wm8731_write(codec, WM8731_ACTIVE, 0x0);
	}
}

static int wm8731_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8731_read_reg_cache(codec, WM8731_APDIGI) & 0xfff7;

	if (mute)
		wm8731_write(codec, WM8731_APDIGI, mute_reg | 0x8);
	else
		wm8731_write(codec, WM8731_APDIGI, mute_reg);
	return 0;
}

static int wm8731_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8731_priv *wm8731 = codec->private_data;

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		wm8731->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}


static int wm8731_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

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
	wm8731_write(codec, WM8731_IFACE, iface);
	return 0;
}

static int wm8731_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* Clear PWROFF, gate CLKOUT, everything else as-is */
		reg = wm8731_read_reg_cache(codec, WM8731_PWR) & 0xff7f;
		wm8731_write(codec, WM8731_PWR, reg | 0x0040);
		break;
	case SND_SOC_BIAS_OFF:
		wm8731_write(codec, WM8731_ACTIVE, 0x0);
		wm8731_write(codec, WM8731_PWR, 0xffff);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define WM8731_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
		SNDRV_PCM_RATE_96000)

#define WM8731_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8731_dai_ops = {
	.prepare	= wm8731_pcm_prepare,
	.hw_params	= wm8731_hw_params,
	.shutdown	= wm8731_shutdown,
	.digital_mute	= wm8731_mute,
	.set_sysclk	= wm8731_set_dai_sysclk,
	.set_fmt	= wm8731_set_dai_fmt,
};

struct snd_soc_dai wm8731_dai = {
	.name = "WM8731",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8731_RATES,
		.formats = WM8731_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8731_RATES,
		.formats = WM8731_FORMATS,},
	.ops = &wm8731_dai_ops,
};
EXPORT_SYMBOL_GPL(wm8731_dai);

static int wm8731_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8731_write(codec, WM8731_ACTIVE, 0x0);
	wm8731_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8731_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8731_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	wm8731_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8731_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

static int wm8731_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8731_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8731_codec;
	codec = wm8731_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8731_snd_controls,
			     ARRAY_SIZE(wm8731_snd_controls));
	wm8731_add_widgets(codec);
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(codec->dev, "failed to register card: %d\n", ret);
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	return ret;
}

/* power down chip */
static int wm8731_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8731 = {
	.probe = 	wm8731_probe,
	.remove = 	wm8731_remove,
	.suspend = 	wm8731_suspend,
	.resume =	wm8731_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8731);

static int wm8731_register(struct wm8731_priv *wm8731)
{
	int ret;
	struct snd_soc_codec *codec = &wm8731->codec;
	u16 reg;

	if (wm8731_codec) {
		dev_err(codec->dev, "Another WM8731 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data = wm8731;
	codec->name = "WM8731";
	codec->owner = THIS_MODULE;
	codec->read = wm8731_read_reg_cache;
	codec->write = wm8731_write;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8731_set_bias_level;
	codec->dai = &wm8731_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8731_CACHEREGNUM;
	codec->reg_cache = &wm8731->reg_cache;

	memcpy(codec->reg_cache, wm8731_reg, sizeof(wm8731_reg));

	ret = wm8731_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	wm8731_dai.dev = codec->dev;

	wm8731_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Latch the update bits */
	reg = wm8731_read_reg_cache(codec, WM8731_LOUT1V);
	wm8731_write(codec, WM8731_LOUT1V, reg & ~0x0100);
	reg = wm8731_read_reg_cache(codec, WM8731_ROUT1V);
	wm8731_write(codec, WM8731_ROUT1V, reg & ~0x0100);
	reg = wm8731_read_reg_cache(codec, WM8731_LINVOL);
	wm8731_write(codec, WM8731_LINVOL, reg & ~0x0100);
	reg = wm8731_read_reg_cache(codec, WM8731_RINVOL);
	wm8731_write(codec, WM8731_RINVOL, reg & ~0x0100);

	/* Disable bypass path by default */
	reg = wm8731_read_reg_cache(codec, WM8731_APANA);
	wm8731_write(codec, WM8731_APANA, reg & ~0x4);

	wm8731_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm8731_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;
}

static void wm8731_unregister(struct wm8731_priv *wm8731)
{
	wm8731_set_bias_level(&wm8731->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8731_dai);
	snd_soc_unregister_codec(&wm8731->codec);
	kfree(wm8731);
	wm8731_codec = NULL;
}

#if defined(CONFIG_SPI_MASTER)
static int wm8731_spi_write(struct spi_device *spi, const char *data, int len)
{
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[2];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}

static int __devinit wm8731_spi_probe(struct spi_device *spi)
{
	struct snd_soc_codec *codec;
	struct wm8731_priv *wm8731;

	wm8731 = kzalloc(sizeof(struct wm8731_priv), GFP_KERNEL);
	if (wm8731 == NULL)
		return -ENOMEM;

	codec = &wm8731->codec;
	codec->control_data = spi;
	codec->hw_write = (hw_write_t)wm8731_spi_write;
	codec->dev = &spi->dev;

	spi->dev.driver_data = wm8731;

	return wm8731_register(wm8731);
}

static int __devexit wm8731_spi_remove(struct spi_device *spi)
{
	struct wm8731_priv *wm8731 = spi->dev.driver_data;

	wm8731_unregister(wm8731);

	return 0;
}

static struct spi_driver wm8731_spi_driver = {
	.driver = {
		.name	= "wm8731",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm8731_spi_probe,
	.remove		= __devexit_p(wm8731_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8731_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8731_priv *wm8731;
	struct snd_soc_codec *codec;

	wm8731 = kzalloc(sizeof(struct wm8731_priv), GFP_KERNEL);
	if (wm8731 == NULL)
		return -ENOMEM;

	codec = &wm8731->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;

	i2c_set_clientdata(i2c, wm8731);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8731_register(wm8731);
}

static __devexit int wm8731_i2c_remove(struct i2c_client *client)
{
	struct wm8731_priv *wm8731 = i2c_get_clientdata(client);
	wm8731_unregister(wm8731);
	return 0;
}

static const struct i2c_device_id wm8731_i2c_id[] = {
	{ "wm8731", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8731_i2c_id);

static struct i2c_driver wm8731_i2c_driver = {
	.driver = {
		.name = "WM8731 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8731_i2c_probe,
	.remove =   __devexit_p(wm8731_i2c_remove),
	.id_table = wm8731_i2c_id,
};
#endif

static int __init wm8731_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8731_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8731 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8731_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8731 SPI driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8731_modinit);

static void __exit wm8731_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8731_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8731_spi_driver);
#endif
}
module_exit(wm8731_exit);

MODULE_DESCRIPTION("ASoC WM8731 driver");
MODULE_AUTHOR("Richard Purdie");
MODULE_LICENSE("GPL");
