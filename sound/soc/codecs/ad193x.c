/*
 * AD193X Audio Codec driver supporting AD1936/7/8/9
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "ad193x.h"

/* codec private data */
struct ad193x_priv {
	struct regmap *regmap;
	int sysclk;
};

/*
 * AD193X volume/mute/de-emphasis etc. controls
 */
static const char * const ad193x_deemp[] = {"None", "48kHz", "44.1kHz", "32kHz"};

static const struct soc_enum ad193x_deemp_enum =
	SOC_ENUM_SINGLE(AD193X_DAC_CTRL2, 1, 4, ad193x_deemp);

static const DECLARE_TLV_DB_MINMAX(adau193x_tlv, -9563, 0);

static const struct snd_kcontrol_new ad193x_snd_controls[] = {
	/* DAC volume control */
	SOC_DOUBLE_R_TLV("DAC1 Volume", AD193X_DAC_L1_VOL,
			AD193X_DAC_R1_VOL, 0, 0xFF, 1, adau193x_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Volume", AD193X_DAC_L2_VOL,
			AD193X_DAC_R2_VOL, 0, 0xFF, 1, adau193x_tlv),
	SOC_DOUBLE_R_TLV("DAC3 Volume", AD193X_DAC_L3_VOL,
			AD193X_DAC_R3_VOL, 0, 0xFF, 1, adau193x_tlv),
	SOC_DOUBLE_R_TLV("DAC4 Volume", AD193X_DAC_L4_VOL,
			AD193X_DAC_R4_VOL, 0, 0xFF, 1, adau193x_tlv),

	/* ADC switch control */
	SOC_DOUBLE("ADC1 Switch", AD193X_ADC_CTRL0, AD193X_ADCL1_MUTE,
		AD193X_ADCR1_MUTE, 1, 1),
	SOC_DOUBLE("ADC2 Switch", AD193X_ADC_CTRL0, AD193X_ADCL2_MUTE,
		AD193X_ADCR2_MUTE, 1, 1),

	/* DAC switch control */
	SOC_DOUBLE("DAC1 Switch", AD193X_DAC_CHNL_MUTE, AD193X_DACL1_MUTE,
		AD193X_DACR1_MUTE, 1, 1),
	SOC_DOUBLE("DAC2 Switch", AD193X_DAC_CHNL_MUTE, AD193X_DACL2_MUTE,
		AD193X_DACR2_MUTE, 1, 1),
	SOC_DOUBLE("DAC3 Switch", AD193X_DAC_CHNL_MUTE, AD193X_DACL3_MUTE,
		AD193X_DACR3_MUTE, 1, 1),
	SOC_DOUBLE("DAC4 Switch", AD193X_DAC_CHNL_MUTE, AD193X_DACL4_MUTE,
		AD193X_DACR4_MUTE, 1, 1),

	/* ADC high-pass filter */
	SOC_SINGLE("ADC High Pass Filter Switch", AD193X_ADC_CTRL0,
			AD193X_ADC_HIGHPASS_FILTER, 1, 0),

	/* DAC de-emphasis */
	SOC_ENUM("Playback Deemphasis", ad193x_deemp_enum),
};

static const struct snd_soc_dapm_widget ad193x_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", AD193X_DAC_CTRL0, 0, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("PLL_PWR", AD193X_PLL_CLK_CTRL0, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_PWR", AD193X_ADC_CTRL0, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SYSCLK", AD193X_PLL_CLK_CTRL0, 7, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("DAC1OUT"),
	SND_SOC_DAPM_OUTPUT("DAC2OUT"),
	SND_SOC_DAPM_OUTPUT("DAC3OUT"),
	SND_SOC_DAPM_OUTPUT("DAC4OUT"),
	SND_SOC_DAPM_INPUT("ADC1IN"),
	SND_SOC_DAPM_INPUT("ADC2IN"),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "DAC", NULL, "SYSCLK" },
	{ "ADC", NULL, "SYSCLK" },
	{ "DAC", NULL, "ADC_PWR" },
	{ "ADC", NULL, "ADC_PWR" },
	{ "DAC1OUT", NULL, "DAC" },
	{ "DAC2OUT", NULL, "DAC" },
	{ "DAC3OUT", NULL, "DAC" },
	{ "DAC4OUT", NULL, "DAC" },
	{ "ADC", NULL, "ADC1IN" },
	{ "ADC", NULL, "ADC2IN" },
	{ "SYSCLK", NULL, "PLL_PWR" },
};

/*
 * DAI ops entries
 */

static int ad193x_mute(struct snd_soc_dai *dai, int mute)
{
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(dai->codec);

	if (mute)
		regmap_update_bits(ad193x->regmap, AD193X_DAC_CTRL2,
				    AD193X_DAC_MASTER_MUTE,
				    AD193X_DAC_MASTER_MUTE);
	else
		regmap_update_bits(ad193x->regmap, AD193X_DAC_CTRL2,
				    AD193X_DAC_MASTER_MUTE, 0);

	return 0;
}

static int ad193x_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int width)
{
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(dai->codec);
	unsigned int channels;

	switch (slots) {
	case 2:
		channels = AD193X_2_CHANNELS;
		break;
	case 4:
		channels = AD193X_4_CHANNELS;
		break;
	case 8:
		channels = AD193X_8_CHANNELS;
		break;
	case 16:
		channels = AD193X_16_CHANNELS;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(ad193x->regmap, AD193X_DAC_CTRL1,
		AD193X_DAC_CHAN_MASK, channels << AD193X_DAC_CHAN_SHFT);
	regmap_update_bits(ad193x->regmap, AD193X_ADC_CTRL2,
		AD193X_ADC_CHAN_MASK, channels << AD193X_ADC_CHAN_SHFT);

	return 0;
}

static int ad193x_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(codec_dai->codec);
	unsigned int adc_serfmt = 0;
	unsigned int adc_fmt = 0;
	unsigned int dac_fmt = 0;

	/* At present, the driver only support AUX ADC mode(SND_SOC_DAIFMT_I2S
	 * with TDM) and ADC&DAC TDM mode(SND_SOC_DAIFMT_DSP_A)
	 */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adc_serfmt |= AD193X_ADC_SERFMT_TDM;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adc_serfmt |= AD193X_ADC_SERFMT_AUX;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* normal bit clock + frame */
		break;
	case SND_SOC_DAIFMT_NB_IF: /* normal bclk + invert frm */
		adc_fmt |= AD193X_ADC_LEFT_HIGH;
		dac_fmt |= AD193X_DAC_LEFT_HIGH;
		break;
	case SND_SOC_DAIFMT_IB_NF: /* invert bclk + normal frm */
		adc_fmt |= AD193X_ADC_BCLK_INV;
		dac_fmt |= AD193X_DAC_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_IF: /* invert bclk + frm */
		adc_fmt |= AD193X_ADC_LEFT_HIGH;
		adc_fmt |= AD193X_ADC_BCLK_INV;
		dac_fmt |= AD193X_DAC_LEFT_HIGH;
		dac_fmt |= AD193X_DAC_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* codec clk & frm master */
		adc_fmt |= AD193X_ADC_LCR_MASTER;
		adc_fmt |= AD193X_ADC_BCLK_MASTER;
		dac_fmt |= AD193X_DAC_LCR_MASTER;
		dac_fmt |= AD193X_DAC_BCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFM: /* codec clk slave & frm master */
		adc_fmt |= AD193X_ADC_LCR_MASTER;
		dac_fmt |= AD193X_DAC_LCR_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFS: /* codec clk master & frame slave */
		adc_fmt |= AD193X_ADC_BCLK_MASTER;
		dac_fmt |= AD193X_DAC_BCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* codec clk & frm slave */
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(ad193x->regmap, AD193X_ADC_CTRL1,
		AD193X_ADC_SERFMT_MASK, adc_serfmt);
	regmap_update_bits(ad193x->regmap, AD193X_ADC_CTRL2,
		AD193X_ADC_FMT_MASK, adc_fmt);
	regmap_update_bits(ad193x->regmap, AD193X_DAC_CTRL1,
		AD193X_DAC_FMT_MASK, dac_fmt);

	return 0;
}

static int ad193x_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(codec);
	switch (freq) {
	case 12288000:
	case 18432000:
	case 24576000:
	case 36864000:
		ad193x->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int ad193x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int word_len = 0, master_rate = 0;
	struct snd_soc_codec *codec = dai->codec;
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(codec);

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		word_len = 3;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		word_len = 1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		word_len = 0;
		break;
	}

	switch (ad193x->sysclk) {
	case 12288000:
		master_rate = AD193X_PLL_INPUT_256;
		break;
	case 18432000:
		master_rate = AD193X_PLL_INPUT_384;
		break;
	case 24576000:
		master_rate = AD193X_PLL_INPUT_512;
		break;
	case 36864000:
		master_rate = AD193X_PLL_INPUT_768;
		break;
	}

	regmap_update_bits(ad193x->regmap, AD193X_PLL_CLK_CTRL0,
			    AD193X_PLL_INPUT_MASK, master_rate);

	regmap_update_bits(ad193x->regmap, AD193X_DAC_CTRL2,
			    AD193X_DAC_WORD_LEN_MASK,
			    word_len << AD193X_DAC_WORD_LEN_SHFT);

	regmap_update_bits(ad193x->regmap, AD193X_ADC_CTRL1,
			    AD193X_ADC_WORD_LEN_MASK, word_len);

	return 0;
}

static const struct snd_soc_dai_ops ad193x_dai_ops = {
	.hw_params = ad193x_hw_params,
	.digital_mute = ad193x_mute,
	.set_tdm_slot = ad193x_set_tdm_slot,
	.set_sysclk	= ad193x_set_dai_sysclk,
	.set_fmt = ad193x_set_dai_fmt,
};

/* codec DAI instance */
static struct snd_soc_dai_driver ad193x_dai = {
	.name = "ad193x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &ad193x_dai_ops,
};

static int ad193x_probe(struct snd_soc_codec *codec)
{
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(codec);
	int ret;

	codec->control_data = ad193x->regmap;
	ret = snd_soc_codec_set_cache_io(codec, 0, 0, SND_SOC_REGMAP);
	if (ret < 0) {
		dev_err(codec->dev, "failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* default setting for ad193x */

	/* unmute dac channels */
	regmap_write(ad193x->regmap, AD193X_DAC_CHNL_MUTE, 0x0);
	/* de-emphasis: 48kHz, powedown dac */
	regmap_write(ad193x->regmap, AD193X_DAC_CTRL2, 0x1A);
	/* powerdown dac, dac in tdm mode */
	regmap_write(ad193x->regmap, AD193X_DAC_CTRL0, 0x41);
	/* high-pass filter enable */
	regmap_write(ad193x->regmap, AD193X_ADC_CTRL0, 0x3);
	/* sata delay=1, adc aux mode */
	regmap_write(ad193x->regmap, AD193X_ADC_CTRL1, 0x43);
	/* pll input: mclki/xi */
	regmap_write(ad193x->regmap, AD193X_PLL_CLK_CTRL0, 0x99); /* mclk=24.576Mhz: 0x9D; mclk=12.288Mhz: 0x99 */
	regmap_write(ad193x->regmap, AD193X_PLL_CLK_CTRL1, 0x04);

	return ret;
}

static struct snd_soc_codec_driver soc_codec_dev_ad193x = {
	.probe = 	ad193x_probe,
	.controls = ad193x_snd_controls,
	.num_controls = ARRAY_SIZE(ad193x_snd_controls),
	.dapm_widgets = ad193x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ad193x_dapm_widgets),
	.dapm_routes = audio_paths,
	.num_dapm_routes = ARRAY_SIZE(audio_paths),
};

static bool adau193x_reg_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

#if defined(CONFIG_SPI_MASTER)

static const struct regmap_config ad193x_spi_regmap_config = {
	.val_bits = 8,
	.reg_bits = 16,
	.read_flag_mask = 0x09,
	.write_flag_mask = 0x08,

	.max_register = AD193X_NUM_REGS - 1,
	.volatile_reg = adau193x_reg_volatile,
};

static int ad193x_spi_probe(struct spi_device *spi)
{
	struct ad193x_priv *ad193x;

	ad193x = devm_kzalloc(&spi->dev, sizeof(struct ad193x_priv),
			      GFP_KERNEL);
	if (ad193x == NULL)
		return -ENOMEM;

	ad193x->regmap = devm_regmap_init_spi(spi, &ad193x_spi_regmap_config);
	if (IS_ERR(ad193x->regmap))
		return PTR_ERR(ad193x->regmap);

	spi_set_drvdata(spi, ad193x);

	return snd_soc_register_codec(&spi->dev, &soc_codec_dev_ad193x,
			&ad193x_dai, 1);
}

static int ad193x_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static struct spi_driver ad193x_spi_driver = {
	.driver = {
		.name	= "ad193x",
		.owner	= THIS_MODULE,
	},
	.probe		= ad193x_spi_probe,
	.remove		= ad193x_spi_remove,
};
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)

static const struct regmap_config ad193x_i2c_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,

	.max_register = AD193X_NUM_REGS - 1,
	.volatile_reg = adau193x_reg_volatile,
};

static const struct i2c_device_id ad193x_id[] = {
	{ "ad1936", 0 },
	{ "ad1937", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad193x_id);

static int ad193x_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct ad193x_priv *ad193x;

	ad193x = devm_kzalloc(&client->dev, sizeof(struct ad193x_priv),
			      GFP_KERNEL);
	if (ad193x == NULL)
		return -ENOMEM;

	ad193x->regmap = devm_regmap_init_i2c(client, &ad193x_i2c_regmap_config);
	if (IS_ERR(ad193x->regmap))
		return PTR_ERR(ad193x->regmap);

	i2c_set_clientdata(client, ad193x);

	return snd_soc_register_codec(&client->dev, &soc_codec_dev_ad193x,
			&ad193x_dai, 1);
}

static int ad193x_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver ad193x_i2c_driver = {
	.driver = {
		.name = "ad193x",
	},
	.probe    = ad193x_i2c_probe,
	.remove   = ad193x_i2c_remove,
	.id_table = ad193x_id,
};
#endif

static int __init ad193x_modinit(void)
{
	int ret;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret =  i2c_add_driver(&ad193x_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register AD193X I2C driver: %d\n",
				ret);
	}
#endif

#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&ad193x_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register AD193X SPI driver: %d\n",
				ret);
	}
#endif
	return ret;
}
module_init(ad193x_modinit);

static void __exit ad193x_modexit(void)
{
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&ad193x_spi_driver);
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&ad193x_i2c_driver);
#endif
}
module_exit(ad193x_modexit);

MODULE_DESCRIPTION("ASoC ad193x driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
