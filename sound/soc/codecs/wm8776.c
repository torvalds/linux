/*
 * wm8776.c  --  WM8776 ALSA SoC Audio driver
 *
 * Copyright 2009-12 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO: Input ALC/limiter support
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8776.h"

enum wm8776_chip_type {
	WM8775 = 1,
	WM8776,
};

/* codec private data */
struct wm8776_priv {
	struct regmap *regmap;
	int sysclk[2];
};

static const struct reg_default wm8776_reg_defaults[] = {
	{  0, 0x79 },
	{  1, 0x79 },
	{  2, 0x79 },
	{  3, 0xff },
	{  4, 0xff },
	{  5, 0xff },
	{  6, 0x00 },
	{  7, 0x90 },
	{  8, 0x00 },
	{  9, 0x00 },
	{ 10, 0x22 },
	{ 11, 0x22 },
	{ 12, 0x22 },
	{ 13, 0x08 },
	{ 14, 0xcf },
	{ 15, 0xcf },
	{ 16, 0x7b },
	{ 17, 0x00 },
	{ 18, 0x32 },
	{ 19, 0x00 },
	{ 20, 0xa6 },
	{ 21, 0x01 },
	{ 22, 0x01 },
};

static bool wm8776_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8776_RESET:
		return true;
	default:
		return false;
	}
}

static int wm8776_reset(struct snd_soc_component *component)
{
	return snd_soc_component_write(component, WM8776_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(hp_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -10350, 50, 1);

static const struct snd_kcontrol_new wm8776_snd_controls[] = {
SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8776_HPLVOL, WM8776_HPRVOL,
		 0, 127, 0, hp_tlv),
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8776_DACLVOL, WM8776_DACRVOL,
		 0, 255, 0, dac_tlv),
SOC_SINGLE("Digital Playback ZC Switch", WM8776_DACCTRL1, 0, 1, 0),

SOC_SINGLE("Deemphasis Switch", WM8776_DACCTRL2, 0, 1, 0),

SOC_DOUBLE_R_TLV("Capture Volume", WM8776_ADCLVOL, WM8776_ADCRVOL,
		 0, 255, 0, adc_tlv),
SOC_DOUBLE("Capture Switch", WM8776_ADCMUX, 7, 6, 1, 1),
SOC_DOUBLE_R("Capture ZC Switch", WM8776_ADCLVOL, WM8776_ADCRVOL, 8, 1, 0),
SOC_SINGLE("Capture HPF Switch", WM8776_ADCIFCTRL, 8, 1, 1),
};

static const struct snd_kcontrol_new inmix_controls[] = {
SOC_DAPM_SINGLE("AIN1 Switch", WM8776_ADCMUX, 0, 1, 0),
SOC_DAPM_SINGLE("AIN2 Switch", WM8776_ADCMUX, 1, 1, 0),
SOC_DAPM_SINGLE("AIN3 Switch", WM8776_ADCMUX, 2, 1, 0),
SOC_DAPM_SINGLE("AIN4 Switch", WM8776_ADCMUX, 3, 1, 0),
SOC_DAPM_SINGLE("AIN5 Switch", WM8776_ADCMUX, 4, 1, 0),
};

static const struct snd_kcontrol_new outmix_controls[] = {
SOC_DAPM_SINGLE("DAC Switch", WM8776_OUTMUX, 0, 1, 0),
SOC_DAPM_SINGLE("AUX Switch", WM8776_OUTMUX, 1, 1, 0),
SOC_DAPM_SINGLE("Bypass Switch", WM8776_OUTMUX, 2, 1, 0),
};

static const struct snd_soc_dapm_widget wm8776_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("AUX"),

SND_SOC_DAPM_INPUT("AIN1"),
SND_SOC_DAPM_INPUT("AIN2"),
SND_SOC_DAPM_INPUT("AIN3"),
SND_SOC_DAPM_INPUT("AIN4"),
SND_SOC_DAPM_INPUT("AIN5"),

SND_SOC_DAPM_MIXER("Input Mixer", WM8776_PWRDOWN, 6, 1,
		   inmix_controls, ARRAY_SIZE(inmix_controls)),

SND_SOC_DAPM_ADC("ADC", "Capture", WM8776_PWRDOWN, 1, 1),
SND_SOC_DAPM_DAC("DAC", "Playback", WM8776_PWRDOWN, 2, 1),

SND_SOC_DAPM_MIXER("Output Mixer", SND_SOC_NOPM, 0, 0,
		   outmix_controls, ARRAY_SIZE(outmix_controls)),

SND_SOC_DAPM_PGA("Headphone PGA", WM8776_PWRDOWN, 3, 1, NULL, 0),

SND_SOC_DAPM_OUTPUT("VOUT"),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
};

static const struct snd_soc_dapm_route routes[] = {
	{ "Input Mixer", "AIN1 Switch", "AIN1" },
	{ "Input Mixer", "AIN2 Switch", "AIN2" },
	{ "Input Mixer", "AIN3 Switch", "AIN3" },
	{ "Input Mixer", "AIN4 Switch", "AIN4" },
	{ "Input Mixer", "AIN5 Switch", "AIN5" },

	{ "ADC", NULL, "Input Mixer" },

	{ "Output Mixer", "DAC Switch", "DAC" },
	{ "Output Mixer", "AUX Switch", "AUX" },
	{ "Output Mixer", "Bypass Switch", "Input Mixer" },

	{ "VOUT", NULL, "Output Mixer" },

	{ "Headphone PGA", NULL, "Output Mixer" },

	{ "HPOUTL", NULL, "Headphone PGA" },
	{ "HPOUTR", NULL, "Headphone PGA" },
};

static int wm8776_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	int reg, iface, master;

	switch (dai->driver->id) {
	case WM8776_DAI_DAC:
		reg = WM8776_DACIFCTRL;
		master = 0x80;
		break;
	case WM8776_DAI_ADC:
		reg = WM8776_ADCIFCTRL;
		master = 0x100;
		break;
	default:
		return -EINVAL;
	}

	iface = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		master = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x00c;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x008;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x004;
		break;
	default:
		return -EINVAL;
	}

	/* Finally, write out the values */
	snd_soc_component_update_bits(component, reg, 0xf, iface);
	snd_soc_component_update_bits(component, WM8776_MSTRCTRL, 0x180, master);

	return 0;
}

static int mclk_ratios[] = {
	128,
	192,
	256,
	384,
	512,
	768,
};

static int wm8776_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8776_priv *wm8776 = snd_soc_component_get_drvdata(component);
	int iface_reg, iface;
	int ratio_shift, master;
	int i;

	switch (dai->driver->id) {
	case WM8776_DAI_DAC:
		iface_reg = WM8776_DACIFCTRL;
		master = 0x80;
		ratio_shift = 4;
		break;
	case WM8776_DAI_ADC:
		iface_reg = WM8776_ADCIFCTRL;
		master = 0x100;
		ratio_shift = 0;
		break;
	default:
		return -EINVAL;
	}

	/* Set word length */
	switch (params_width(params)) {
	case 16:
		iface = 0;
		break;
	case 20:
		iface = 0x10;
		break;
	case 24:
		iface = 0x20;
		break;
	case 32:
		iface = 0x30;
		break;
	default:
		dev_err(component->dev, "Unsupported sample size: %i\n",
			params_width(params));
		return -EINVAL;
	}

	/* Only need to set MCLK/LRCLK ratio if we're master */
	if (snd_soc_component_read32(component, WM8776_MSTRCTRL) & master) {
		for (i = 0; i < ARRAY_SIZE(mclk_ratios); i++) {
			if (wm8776->sysclk[dai->driver->id] / params_rate(params)
			    == mclk_ratios[i])
				break;
		}

		if (i == ARRAY_SIZE(mclk_ratios)) {
			dev_err(component->dev,
				"Unable to configure MCLK ratio %d/%d\n",
				wm8776->sysclk[dai->driver->id], params_rate(params));
			return -EINVAL;
		}

		dev_dbg(component->dev, "MCLK is %dfs\n", mclk_ratios[i]);

		snd_soc_component_update_bits(component, WM8776_MSTRCTRL,
				    0x7 << ratio_shift, i << ratio_shift);
	} else {
		dev_dbg(component->dev, "DAI in slave mode\n");
	}

	snd_soc_component_update_bits(component, iface_reg, 0x30, iface);

	return 0;
}

static int wm8776_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;

	return snd_soc_component_write(component, WM8776_DACMUTE, !!mute);
}

static int wm8776_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wm8776_priv *wm8776 = snd_soc_component_get_drvdata(component);

	if (WARN_ON(dai->driver->id >= ARRAY_SIZE(wm8776->sysclk)))
		return -EINVAL;

	wm8776->sysclk[dai->driver->id] = freq;

	return 0;
}

static int wm8776_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8776_priv *wm8776 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_sync(wm8776->regmap);

			/* Disable the global powerdown; DAPM does the rest */
			snd_soc_component_update_bits(component, WM8776_PWRDOWN, 1, 0);
		}

		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, WM8776_PWRDOWN, 1, 1);
		break;
	}

	return 0;
}

#define WM8776_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8776_dac_ops = {
	.digital_mute	= wm8776_mute,
	.hw_params      = wm8776_hw_params,
	.set_fmt        = wm8776_set_fmt,
	.set_sysclk     = wm8776_set_sysclk,
};

static const struct snd_soc_dai_ops wm8776_adc_ops = {
	.hw_params      = wm8776_hw_params,
	.set_fmt        = wm8776_set_fmt,
	.set_sysclk     = wm8776_set_sysclk,
};

static struct snd_soc_dai_driver wm8776_dai[] = {
	{
		.name = "wm8776-hifi-playback",
		.id	= WM8776_DAI_DAC,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min = 32000,
			.rate_max = 192000,
			.formats = WM8776_FORMATS,
		},
		.ops = &wm8776_dac_ops,
	},
	{
		.name = "wm8776-hifi-capture",
		.id	= WM8776_DAI_ADC,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min = 32000,
			.rate_max = 96000,
			.formats = WM8776_FORMATS,
		},
		.ops = &wm8776_adc_ops,
	},
};

static int wm8776_probe(struct snd_soc_component *component)
{
	int ret = 0;

	ret = wm8776_reset(component);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	/* Latch the update bits; right channel only since we always
	 * update both. */
	snd_soc_component_update_bits(component, WM8776_HPRVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8776_DACRVOL, 0x100, 0x100);

	return ret;
}

static const struct snd_soc_component_driver soc_component_dev_wm8776 = {
	.probe			= wm8776_probe,
	.set_bias_level		= wm8776_set_bias_level,
	.controls		= wm8776_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8776_snd_controls),
	.dapm_widgets		= wm8776_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8776_dapm_widgets),
	.dapm_routes		= routes,
	.num_dapm_routes	= ARRAY_SIZE(routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct of_device_id wm8776_of_match[] = {
	{ .compatible = "wlf,wm8776", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8776_of_match);

static const struct regmap_config wm8776_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8776_RESET,

	.reg_defaults = wm8776_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8776_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm8776_volatile,
};

#if defined(CONFIG_SPI_MASTER)
static int wm8776_spi_probe(struct spi_device *spi)
{
	struct wm8776_priv *wm8776;
	int ret;

	wm8776 = devm_kzalloc(&spi->dev, sizeof(struct wm8776_priv),
			      GFP_KERNEL);
	if (wm8776 == NULL)
		return -ENOMEM;

	wm8776->regmap = devm_regmap_init_spi(spi, &wm8776_regmap);
	if (IS_ERR(wm8776->regmap))
		return PTR_ERR(wm8776->regmap);

	spi_set_drvdata(spi, wm8776);

	ret = devm_snd_soc_register_component(&spi->dev,
			&soc_component_dev_wm8776, wm8776_dai, ARRAY_SIZE(wm8776_dai));

	return ret;
}

static struct spi_driver wm8776_spi_driver = {
	.driver = {
		.name	= "wm8776",
		.of_match_table = wm8776_of_match,
	},
	.probe		= wm8776_spi_probe,
};
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
static int wm8776_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8776_priv *wm8776;
	int ret;

	wm8776 = devm_kzalloc(&i2c->dev, sizeof(struct wm8776_priv),
			      GFP_KERNEL);
	if (wm8776 == NULL)
		return -ENOMEM;

	wm8776->regmap = devm_regmap_init_i2c(i2c, &wm8776_regmap);
	if (IS_ERR(wm8776->regmap))
		return PTR_ERR(wm8776->regmap);

	i2c_set_clientdata(i2c, wm8776);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8776, wm8776_dai, ARRAY_SIZE(wm8776_dai));

	return ret;
}

static const struct i2c_device_id wm8776_i2c_id[] = {
	{ "wm8775", WM8775 },
	{ "wm8776", WM8776 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8776_i2c_id);

static struct i2c_driver wm8776_i2c_driver = {
	.driver = {
		.name = "wm8776",
		.of_match_table = wm8776_of_match,
	},
	.probe =    wm8776_i2c_probe,
	.id_table = wm8776_i2c_id,
};
#endif

static int __init wm8776_modinit(void)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8776_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8776 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8776_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8776 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8776_modinit);

static void __exit wm8776_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8776_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8776_spi_driver);
#endif
}
module_exit(wm8776_exit);

MODULE_DESCRIPTION("ASoC WM8776 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
