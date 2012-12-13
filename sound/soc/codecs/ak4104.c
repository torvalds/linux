/*
 * AK4104 ALSA SoC (ASoC) driver
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/asoundef.h>

/* AK4104 registers addresses */
#define AK4104_REG_CONTROL1		0x00
#define AK4104_REG_RESERVED		0x01
#define AK4104_REG_CONTROL2		0x02
#define AK4104_REG_TX			0x03
#define AK4104_REG_CHN_STATUS(x)	((x) + 0x04)
#define AK4104_NUM_REGS			10

#define AK4104_REG_MASK			0x1f
#define AK4104_READ			0xc0
#define AK4104_WRITE			0xe0
#define AK4104_RESERVED_VAL		0x5b

/* Bit masks for AK4104 registers */
#define AK4104_CONTROL1_RSTN		(1 << 0)
#define AK4104_CONTROL1_PW		(1 << 1)
#define AK4104_CONTROL1_DIF0		(1 << 2)
#define AK4104_CONTROL1_DIF1		(1 << 3)

#define AK4104_CONTROL2_SEL0		(1 << 0)
#define AK4104_CONTROL2_SEL1		(1 << 1)
#define AK4104_CONTROL2_MODE		(1 << 2)

#define AK4104_TX_TXE			(1 << 0)
#define AK4104_TX_V			(1 << 1)

#define DRV_NAME "ak4104-codec"

struct ak4104_private {
	struct regmap *regmap;
};

static int ak4104_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int val = 0;
	int ret;

	/* set DAI format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= AK4104_CONTROL1_DIF0;
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= AK4104_CONTROL1_DIF0 | AK4104_CONTROL1_DIF1;
		break;
	default:
		dev_err(codec->dev, "invalid dai format\n");
		return -EINVAL;
	}

	/* This device can only be slave */
	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	ret = snd_soc_update_bits(codec, AK4104_REG_CONTROL1,
				  AK4104_CONTROL1_DIF0 | AK4104_CONTROL1_DIF1,
				  val);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4104_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int val = 0;

	/* set the IEC958 bits: consumer mode, no copyright bit */
	val |= IEC958_AES0_CON_NOT_COPYRIGHT;
	snd_soc_write(codec, AK4104_REG_CHN_STATUS(0), val);

	val = 0;

	switch (params_rate(params)) {
	case 22050:
		val |= IEC958_AES3_CON_FS_22050;
		break;
	case 24000:
		val |= IEC958_AES3_CON_FS_24000;
		break;
	case 32000:
		val |= IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		val |= IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		val |= IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		val |= IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		val |= IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		val |= IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		val |= IEC958_AES3_CON_FS_192000;
		break;
	default:
		dev_err(codec->dev, "unsupported sampling rate\n");
		return -EINVAL;
	}

	return snd_soc_write(codec, AK4104_REG_CHN_STATUS(3), val);
}

static const struct snd_soc_dai_ops ak4101_dai_ops = {
	.hw_params = ak4104_hw_params,
	.set_fmt = ak4104_set_dai_fmt,
};

static struct snd_soc_dai_driver ak4104_dai = {
	.name = "ak4104-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE  |
			   SNDRV_PCM_FMTBIT_S24_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE
	},
	.ops = &ak4101_dai_ops,
};

static int ak4104_probe(struct snd_soc_codec *codec)
{
	struct ak4104_private *ak4104 = snd_soc_codec_get_drvdata(codec);
	int ret;

	codec->control_data = ak4104->regmap;
	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_REGMAP);
	if (ret != 0)
		return ret;

	/* set power-up and non-reset bits */
	ret = snd_soc_update_bits(codec, AK4104_REG_CONTROL1,
				  AK4104_CONTROL1_PW | AK4104_CONTROL1_RSTN,
				  AK4104_CONTROL1_PW | AK4104_CONTROL1_RSTN);
	if (ret < 0)
		return ret;

	/* enable transmitter */
	ret = snd_soc_update_bits(codec, AK4104_REG_TX,
				  AK4104_TX_TXE, AK4104_TX_TXE);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4104_remove(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, AK4104_REG_CONTROL1,
			    AK4104_CONTROL1_PW | AK4104_CONTROL1_RSTN, 0);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_device_ak4104 = {
	.probe =	ak4104_probe,
	.remove =	ak4104_remove,
};

static const struct regmap_config ak4104_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AK4104_NUM_REGS - 1,
	.read_flag_mask = AK4104_READ,
	.write_flag_mask = AK4104_WRITE,

	.cache_type = REGCACHE_RBTREE,
};

static int ak4104_spi_probe(struct spi_device *spi)
{
	struct device_node *np = spi->dev.of_node;
	struct ak4104_private *ak4104;
	unsigned int val;
	int ret;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ak4104 = devm_kzalloc(&spi->dev, sizeof(struct ak4104_private),
			      GFP_KERNEL);
	if (ak4104 == NULL)
		return -ENOMEM;

	ak4104->regmap = devm_regmap_init_spi(spi, &ak4104_regmap);
	if (IS_ERR(ak4104->regmap)) {
		ret = PTR_ERR(ak4104->regmap);
		return ret;
	}

	if (np) {
		enum of_gpio_flags flags;
		int gpio = of_get_named_gpio_flags(np, "reset-gpio", 0, &flags);

		if (gpio_is_valid(gpio)) {
			ret = devm_gpio_request_one(&spi->dev, gpio,
				     flags & OF_GPIO_ACTIVE_LOW ?
					GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH,
				     "ak4104 reset");
			if (ret < 0)
				return ret;
		}
	}

	/* read the 'reserved' register - according to the datasheet, it
	 * should contain 0x5b. Not a good way to verify the presence of
	 * the device, but there is no hardware ID register. */
	ret = regmap_read(ak4104->regmap, AK4104_REG_RESERVED, &val);
	if (ret != 0)
		return ret;
	if (val != AK4104_RESERVED_VAL)
		return -ENODEV;

	spi_set_drvdata(spi, ak4104);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_device_ak4104, &ak4104_dai, 1);
	return ret;
}

static int ak4104_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct of_device_id ak4104_of_match[] = {
	{ .compatible = "asahi-kasei,ak4104", },
	{ }
};
MODULE_DEVICE_TABLE(of, ak4104_of_match);

static struct spi_driver ak4104_spi_driver = {
	.driver  = {
		.name   = DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = ak4104_of_match,
	},
	.probe  = ak4104_spi_probe,
	.remove = ak4104_spi_remove,
};

module_spi_driver(ak4104_spi_driver);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("Asahi Kasei AK4104 ALSA SoC driver");
MODULE_LICENSE("GPL");

