/*
 * wm8523.c  --  WM8523 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
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
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8523.h"

#define WM8523_NUM_SUPPLIES 2
static const char *wm8523_supply_names[WM8523_NUM_SUPPLIES] = {
	"AVDD",
	"LINEVDD",
};

#define WM8523_NUM_RATES 7

/* codec private data */
struct wm8523_priv {
	enum snd_soc_control_type control_type;
	struct regulator_bulk_data supplies[WM8523_NUM_SUPPLIES];
	unsigned int sysclk;
	unsigned int rate_constraint_list[WM8523_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
};

static const u16 wm8523_reg[WM8523_REGISTER_COUNT] = {
	0x8523,     /* R0 - DEVICE_ID */
	0x0001,     /* R1 - REVISION */
	0x0000,     /* R2 - PSCTRL1 */
	0x1812,     /* R3 - AIF_CTRL1 */
	0x0000,     /* R4 - AIF_CTRL2 */
	0x0001,     /* R5 - DAC_CTRL3 */
	0x0190,     /* R6 - DAC_GAINL */
	0x0190,     /* R7 - DAC_GAINR */
	0x0000,     /* R8 - ZERO_DETECT */
};

static int wm8523_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case WM8523_DEVICE_ID:
	case WM8523_REVISION:
		return 1;
	default:
		return 0;
	}
}

static int wm8523_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8523_DEVICE_ID, 0);
}

static const DECLARE_TLV_DB_SCALE(dac_tlv, -10000, 25, 0);

static const char *wm8523_zd_count_text[] = {
	"1024",
	"2048",
};

static const struct soc_enum wm8523_zc_count =
	SOC_ENUM_SINGLE(WM8523_ZERO_DETECT, 0, 2, wm8523_zd_count_text);

static const struct snd_kcontrol_new wm8523_snd_controls[] = {
SOC_DOUBLE_R_TLV("Playback Volume", WM8523_DAC_GAINL, WM8523_DAC_GAINR,
		 0, 448, 0, dac_tlv),
SOC_SINGLE("ZC Switch", WM8523_DAC_CTRL3, 4, 1, 0),
SOC_SINGLE("Playback Deemphasis Switch", WM8523_AIF_CTRL1, 8, 1, 0),
SOC_DOUBLE("Playback Switch", WM8523_DAC_CTRL3, 2, 3, 1, 1),
SOC_SINGLE("Volume Ramp Up Switch", WM8523_DAC_CTRL3, 1, 1, 0),
SOC_SINGLE("Volume Ramp Down Switch", WM8523_DAC_CTRL3, 0, 1, 0),
SOC_ENUM("Zero Detect Count", wm8523_zc_count),
};

static const struct snd_soc_dapm_widget wm8523_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

static int wm8523_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8523_dapm_widgets,
				  ARRAY_SIZE(wm8523_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	return 0;
}

static struct {
	int value;
	int ratio;
} lrclk_ratios[WM8523_NUM_RATES] = {
	{ 1, 128 },
	{ 2, 192 },
	{ 3, 256 },
	{ 4, 384 },
	{ 5, 512 },
	{ 6, 768 },
	{ 7, 1152 },
};

static int wm8523_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8523->sysclk) {
		dev_err(codec->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &wm8523->rate_constraint);

	return 0;
}

static int wm8523_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);
	int i;
	u16 aifctrl1 = snd_soc_read(codec, WM8523_AIF_CTRL1);
	u16 aifctrl2 = snd_soc_read(codec, WM8523_AIF_CTRL2);

	/* Find a supported LRCLK ratio */
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		if (wm8523->sysclk / params_rate(params) ==
		    lrclk_ratios[i].ratio)
			break;
	}

	/* Should never happen, should be handled by constraints */
	if (i == ARRAY_SIZE(lrclk_ratios)) {
		dev_err(codec->dev, "MCLK/fs ratio %d unsupported\n",
			wm8523->sysclk / params_rate(params));
		return -EINVAL;
	}

	aifctrl2 &= ~WM8523_SR_MASK;
	aifctrl2 |= lrclk_ratios[i].value;

	aifctrl1 &= ~WM8523_WL_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		aifctrl1 |= 0x8;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		aifctrl1 |= 0x10;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		aifctrl1 |= 0x18;
		break;
	}

	snd_soc_write(codec, WM8523_AIF_CTRL1, aifctrl1);
	snd_soc_write(codec, WM8523_AIF_CTRL2, aifctrl2);

	return 0;
}

static int wm8523_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;
	int i;

	wm8523->sysclk = freq;

	wm8523->rate_constraint.count = 0;
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		val = freq / lrclk_ratios[i].ratio;
		/* Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */
		switch (val) {
		case 8000:
		case 11025:
		case 16000:
		case 22050:
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
		case 176400:
		case 192000:
			dev_dbg(codec->dev, "Supported sample rate: %dHz\n",
				val);
			wm8523->rate_constraint_list[i] = val;
			wm8523->rate_constraint.count++;
			break;
		default:
			dev_dbg(codec->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (wm8523->rate_constraint.count == 0)
		return -EINVAL;

	return 0;
}


static int wm8523_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 aifctrl1 = snd_soc_read(codec, WM8523_AIF_CTRL1);

	aifctrl1 &= ~(WM8523_BCLK_INV_MASK | WM8523_LRCLK_INV_MASK |
		      WM8523_FMT_MASK | WM8523_AIF_MSTR_MASK);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aifctrl1 |= WM8523_AIF_MSTR;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		aifctrl1 |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aifctrl1 |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		aifctrl1 |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aifctrl1 |= 0x0023;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		aifctrl1 |= WM8523_BCLK_INV | WM8523_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		aifctrl1 |= WM8523_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		aifctrl1 |= WM8523_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8523_AIF_CTRL1, aifctrl1);

	return 0;
}

static int wm8523_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);
	u16 *reg_cache = codec->reg_cache;
	int ret, i;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Full power on */
		snd_soc_update_bits(codec, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 3);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8523->supplies),
						    wm8523->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			/* Initial power up */
			snd_soc_update_bits(codec, WM8523_PSCTRL1,
					    WM8523_SYS_ENA_MASK, 1);

			/* Sync back default/cached values */
			for (i = WM8523_AIF_CTRL1;
			     i < WM8523_MAX_REGISTER; i++)
				snd_soc_write(codec, i, reg_cache[i]);


			msleep(100);
		}

		/* Power up to mute */
		snd_soc_update_bits(codec, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 2);

		break;

	case SND_SOC_BIAS_OFF:
		/* The chip runs through the power down sequence for us. */
		snd_soc_update_bits(codec, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 0);
		msleep(100);

		regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies),
				       wm8523->supplies);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8523_RATES SNDRV_PCM_RATE_8000_192000

#define WM8523_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8523_dai_ops = {
	.startup	= wm8523_startup,
	.hw_params	= wm8523_hw_params,
	.set_sysclk	= wm8523_set_dai_sysclk,
	.set_fmt	= wm8523_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8523_dai = {
	.name = "wm8523-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,  /* Mono modes not yet supported */
		.channels_max = 2,
		.rates = WM8523_RATES,
		.formats = WM8523_FORMATS,
	},
	.ops = &wm8523_dai_ops,
};

#ifdef CONFIG_PM
static int wm8523_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	wm8523_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8523_resume(struct snd_soc_codec *codec)
{
	wm8523_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define wm8523_suspend NULL
#define wm8523_resume NULL
#endif

static int wm8523_probe(struct snd_soc_codec *codec)
{
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);
	int ret, i;

	codec->hw_write = (hw_write_t)i2c_master_send;
	wm8523->rate_constraint.list = &wm8523->rate_constraint_list[0];
	wm8523->rate_constraint.count =
		ARRAY_SIZE(wm8523->rate_constraint_list);

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, wm8523->control_type);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8523->supplies); i++)
		wm8523->supplies[i].supply = wm8523_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8523->supplies),
				 wm8523->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8523->supplies),
				    wm8523->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = snd_soc_read(codec, WM8523_DEVICE_ID);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (ret != wm8523_reg[WM8523_DEVICE_ID]) {
		dev_err(codec->dev, "Device is not a WM8523, ID is %x\n", ret);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = snd_soc_read(codec, WM8523_REVISION);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read revision register\n");
		goto err_enable;
	}
	dev_info(codec->dev, "revision %c\n",
		 (ret & WM8523_CHIP_REV_MASK) + 'A');

	ret = wm8523_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	/* Change some default settings - latch VU and enable ZC */
	snd_soc_update_bits(codec, WM8523_DAC_GAINR,
			    WM8523_DACR_VU, WM8523_DACR_VU);
	snd_soc_update_bits(codec, WM8523_DAC_CTRL3, WM8523_ZC, WM8523_ZC);

	wm8523_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);

	snd_soc_add_controls(codec, wm8523_snd_controls,
			     ARRAY_SIZE(wm8523_snd_controls));
	wm8523_add_widgets(codec);

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);

	return ret;
}

static int wm8523_remove(struct snd_soc_codec *codec)
{
	struct wm8523_priv *wm8523 = snd_soc_codec_get_drvdata(codec);

	wm8523_set_bias_level(codec, SND_SOC_BIAS_OFF);
	regulator_bulk_free(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8523 = {
	.probe =	wm8523_probe,
	.remove =	wm8523_remove,
	.suspend =	wm8523_suspend,
	.resume =	wm8523_resume,
	.set_bias_level = wm8523_set_bias_level,
	.reg_cache_size = WM8523_REGISTER_COUNT,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8523_reg,
	.volatile_register = wm8523_volatile_register,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8523_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8523_priv *wm8523;
	int ret;

	wm8523 = kzalloc(sizeof(struct wm8523_priv), GFP_KERNEL);
	if (wm8523 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8523);
	wm8523->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8523, &wm8523_dai, 1);
	if (ret < 0)
		kfree(wm8523);
	return ret;

}

static __devexit int wm8523_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8523_i2c_id[] = {
	{ "wm8523", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8523_i2c_id);

static struct i2c_driver wm8523_i2c_driver = {
	.driver = {
		.name = "wm8523-codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8523_i2c_probe,
	.remove =   __devexit_p(wm8523_i2c_remove),
	.id_table = wm8523_i2c_id,
};
#endif

static int __init wm8523_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8523_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8523 I2C driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8523_modinit);

static void __exit wm8523_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8523_i2c_driver);
#endif
}
module_exit(wm8523_exit);

MODULE_DESCRIPTION("ASoC WM8523 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
