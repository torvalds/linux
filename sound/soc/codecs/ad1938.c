/*
 * File:         sound/soc/codecs/ad1938.c
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      June 04 2009
 * Description:  Driver for AD1938 sound chip
 *
 * Modified:
 *               Copyright 2009 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/soc-dapm.h>
#include <linux/spi/spi.h>
#include "ad1938.h"

/* codec private data */
struct ad1938_priv {
	struct snd_soc_codec codec;
	u8 reg_cache[AD1938_NUM_REGS];
};

static struct snd_soc_codec *ad1938_codec;
struct snd_soc_codec_device soc_codec_dev_ad1938;
static int ad1938_register(struct ad1938_priv *ad1938);
static void ad1938_unregister(struct ad1938_priv *ad1938);

/*
 * AD1938 volume/mute/de-emphasis etc. controls
 */
static const char *ad1938_deemp[] = {"None", "48kHz", "44.1kHz", "32kHz"};

static const struct soc_enum ad1938_deemp_enum =
	SOC_ENUM_SINGLE(AD1938_DAC_CTRL2, 1, 4, ad1938_deemp);

static const struct snd_kcontrol_new ad1938_snd_controls[] = {
	/* DAC volume control */
	SOC_DOUBLE_R("DAC1  Volume", AD1938_DAC_L1_VOL,
			AD1938_DAC_R1_VOL, 0, 0xFF, 1),
	SOC_DOUBLE_R("DAC2  Volume", AD1938_DAC_L2_VOL,
			AD1938_DAC_R2_VOL, 0, 0xFF, 1),
	SOC_DOUBLE_R("DAC3  Volume", AD1938_DAC_L3_VOL,
			AD1938_DAC_R3_VOL, 0, 0xFF, 1),
	SOC_DOUBLE_R("DAC4  Volume", AD1938_DAC_L4_VOL,
			AD1938_DAC_R4_VOL, 0, 0xFF, 1),

	/* ADC switch control */
	SOC_DOUBLE("ADC1 Switch", AD1938_ADC_CTRL0, AD1938_ADCL1_MUTE,
		AD1938_ADCR1_MUTE, 1, 1),
	SOC_DOUBLE("ADC2 Switch", AD1938_ADC_CTRL0, AD1938_ADCL2_MUTE,
		AD1938_ADCR2_MUTE, 1, 1),

	/* DAC switch control */
	SOC_DOUBLE("DAC1 Switch", AD1938_DAC_CHNL_MUTE, AD1938_DACL1_MUTE,
		AD1938_DACR1_MUTE, 1, 1),
	SOC_DOUBLE("DAC2 Switch", AD1938_DAC_CHNL_MUTE, AD1938_DACL2_MUTE,
		AD1938_DACR2_MUTE, 1, 1),
	SOC_DOUBLE("DAC3 Switch", AD1938_DAC_CHNL_MUTE, AD1938_DACL3_MUTE,
		AD1938_DACR3_MUTE, 1, 1),
	SOC_DOUBLE("DAC4 Switch", AD1938_DAC_CHNL_MUTE, AD1938_DACL4_MUTE,
		AD1938_DACR4_MUTE, 1, 1),

	/* ADC high-pass filter */
	SOC_SINGLE("ADC High Pass Filter Switch", AD1938_ADC_CTRL0,
			AD1938_ADC_HIGHPASS_FILTER, 1, 0),

	/* DAC de-emphasis */
	SOC_ENUM("Playback Deemphasis", ad1938_deemp_enum),
};

static const struct snd_soc_dapm_widget ad1938_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", AD1938_DAC_CTRL0, 0, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC_PWR", AD1938_ADC_CTRL0, 0, 1, NULL, 0),
	SND_SOC_DAPM_OUTPUT("DAC1OUT"),
	SND_SOC_DAPM_OUTPUT("DAC2OUT"),
	SND_SOC_DAPM_OUTPUT("DAC3OUT"),
	SND_SOC_DAPM_OUTPUT("DAC4OUT"),
	SND_SOC_DAPM_INPUT("ADC1IN"),
	SND_SOC_DAPM_INPUT("ADC2IN"),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "DAC", NULL, "ADC_PWR" },
	{ "ADC", NULL, "ADC_PWR" },
	{ "DAC1OUT", "DAC1 Switch", "DAC" },
	{ "DAC2OUT", "DAC2 Switch", "DAC" },
	{ "DAC3OUT", "DAC3 Switch", "DAC" },
	{ "DAC4OUT", "DAC4 Switch", "DAC" },
	{ "ADC", "ADC1 Switch", "ADC1IN" },
	{ "ADC", "ADC2 Switch", "ADC2IN" },
};

/*
 * DAI ops entries
 */

static int ad1938_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int reg;

	reg = codec->read(codec, AD1938_DAC_CTRL2);
	reg = (mute > 0) ? reg | AD1938_DAC_MASTER_MUTE : reg &
		(~AD1938_DAC_MASTER_MUTE);
	codec->write(codec, AD1938_DAC_CTRL2, reg);

	return 0;
}

static inline int ad1938_pll_powerctrl(struct snd_soc_codec *codec, int cmd)
{
	int reg = codec->read(codec, AD1938_PLL_CLK_CTRL0);
	reg = (cmd > 0) ? reg & (~AD1938_PLL_POWERDOWN) : reg |
		AD1938_PLL_POWERDOWN;
	codec->write(codec, AD1938_PLL_CLK_CTRL0, reg);

	return 0;
}

static int ad1938_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int mask, int slots, int width)
{
	struct snd_soc_codec *codec = dai->codec;
	int dac_reg = codec->read(codec, AD1938_DAC_CTRL1);
	int adc_reg = codec->read(codec, AD1938_ADC_CTRL2);

	dac_reg &= ~AD1938_DAC_CHAN_MASK;
	adc_reg &= ~AD1938_ADC_CHAN_MASK;

	switch (slots) {
	case 2:
		dac_reg |= AD1938_DAC_2_CHANNELS << AD1938_DAC_CHAN_SHFT;
		adc_reg |= AD1938_ADC_2_CHANNELS << AD1938_ADC_CHAN_SHFT;
		break;
	case 4:
		dac_reg |= AD1938_DAC_4_CHANNELS << AD1938_DAC_CHAN_SHFT;
		adc_reg |= AD1938_ADC_4_CHANNELS << AD1938_ADC_CHAN_SHFT;
		break;
	case 8:
		dac_reg |= AD1938_DAC_8_CHANNELS << AD1938_DAC_CHAN_SHFT;
		adc_reg |= AD1938_ADC_8_CHANNELS << AD1938_ADC_CHAN_SHFT;
		break;
	case 16:
		dac_reg |= AD1938_DAC_16_CHANNELS << AD1938_DAC_CHAN_SHFT;
		adc_reg |= AD1938_ADC_16_CHANNELS << AD1938_ADC_CHAN_SHFT;
		break;
	default:
		return -EINVAL;
	}

	codec->write(codec, AD1938_DAC_CTRL1, dac_reg);
	codec->write(codec, AD1938_ADC_CTRL2, adc_reg);

	return 0;
}

static int ad1938_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int adc_reg, dac_reg;

	adc_reg = codec->read(codec, AD1938_ADC_CTRL2);
	dac_reg = codec->read(codec, AD1938_DAC_CTRL1);

	/* At present, the driver only support AUX ADC mode(SND_SOC_DAIFMT_I2S
	 * with TDM) and ADC&DAC TDM mode(SND_SOC_DAIFMT_DSP_A)
	 */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adc_reg &= ~AD1938_ADC_SERFMT_MASK;
		adc_reg |= AD1938_ADC_SERFMT_TDM;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adc_reg &= ~AD1938_ADC_SERFMT_MASK;
		adc_reg |= AD1938_ADC_SERFMT_AUX;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* normal bit clock + frame */
		adc_reg &= ~AD1938_ADC_LEFT_HIGH;
		adc_reg &= ~AD1938_ADC_BCLK_INV;
		dac_reg &= ~AD1938_DAC_LEFT_HIGH;
		dac_reg &= ~AD1938_DAC_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF: /* normal bclk + invert frm */
		adc_reg |= AD1938_ADC_LEFT_HIGH;
		adc_reg &= ~AD1938_ADC_BCLK_INV;
		dac_reg |= AD1938_DAC_LEFT_HIGH;
		dac_reg &= ~AD1938_DAC_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF: /* invert bclk + normal frm */
		adc_reg &= ~AD1938_ADC_LEFT_HIGH;
		adc_reg |= AD1938_ADC_BCLK_INV;
		dac_reg &= ~AD1938_DAC_LEFT_HIGH;
		dac_reg |= AD1938_DAC_BCLK_INV;
		break;

	case SND_SOC_DAIFMT_IB_IF: /* invert bclk + frm */
		adc_reg |= AD1938_ADC_LEFT_HIGH;
		adc_reg |= AD1938_ADC_BCLK_INV;
		dac_reg |= AD1938_DAC_LEFT_HIGH;
		dac_reg |= AD1938_DAC_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* codec clk & frm master */
		adc_reg |= AD1938_ADC_LCR_MASTER;
		adc_reg |= AD1938_ADC_BCLK_MASTER;
		dac_reg |= AD1938_DAC_LCR_MASTER;
		dac_reg |= AD1938_DAC_BCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFM: /* codec clk slave & frm master */
		adc_reg |= AD1938_ADC_LCR_MASTER;
		adc_reg &= ~AD1938_ADC_BCLK_MASTER;
		dac_reg |= AD1938_DAC_LCR_MASTER;
		dac_reg &= ~AD1938_DAC_BCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFS: /* codec clk master & frame slave */
		adc_reg &= ~AD1938_ADC_LCR_MASTER;
		adc_reg |= AD1938_ADC_BCLK_MASTER;
		dac_reg &= ~AD1938_DAC_LCR_MASTER;
		dac_reg |= AD1938_DAC_BCLK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* codec clk & frm slave */
		adc_reg &= ~AD1938_ADC_LCR_MASTER;
		adc_reg &= ~AD1938_ADC_BCLK_MASTER;
		dac_reg &= ~AD1938_DAC_LCR_MASTER;
		dac_reg &= ~AD1938_DAC_BCLK_MASTER;
		break;
	default:
		return -EINVAL;
	}

	codec->write(codec, AD1938_ADC_CTRL2, adc_reg);
	codec->write(codec, AD1938_DAC_CTRL1, dac_reg);

	return 0;
}

static int ad1938_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int word_len = 0, reg = 0;

	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;

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

	reg = codec->read(codec, AD1938_DAC_CTRL2);
	reg = (reg & (~AD1938_DAC_WORD_LEN_MASK)) | word_len;
	codec->write(codec, AD1938_DAC_CTRL2, reg);

	reg = codec->read(codec, AD1938_ADC_CTRL1);
	reg = (reg & (~AD1938_ADC_WORD_LEN_MASK)) | word_len;
	codec->write(codec, AD1938_ADC_CTRL1, reg);

	return 0;
}

static int ad1938_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		ad1938_pll_powerctrl(codec, 1);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_OFF:
		ad1938_pll_powerctrl(codec, 0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

/*
 * interface to read/write ad1938 register
 */

#define AD1938_SPI_ADDR    0x4
#define AD1938_SPI_READ    0x1
#define AD1938_SPI_BUFLEN  3

/*
 * write to the ad1938 register space
 */

static int ad1938_write_reg(struct snd_soc_codec *codec, unsigned int reg,
		unsigned int value)
{
	u8 *reg_cache = codec->reg_cache;
	int ret = 0;

	if (value != reg_cache[reg]) {
		uint8_t buf[AD1938_SPI_BUFLEN];
		struct spi_transfer t = {
			.tx_buf = buf,
			.len = AD1938_SPI_BUFLEN,
		};
		struct spi_message m;

		buf[0] = AD1938_SPI_ADDR << 1;
		buf[1] = reg;
		buf[2] = value;
		spi_message_init(&m);
		spi_message_add_tail(&t, &m);
		ret = spi_sync(codec->control_data, &m);
		if (ret == 0)
			reg_cache[reg] = value;
	}

	return ret;
}

/*
 * read from the ad1938 register space cache
 */

static unsigned int ad1938_read_reg_cache(struct snd_soc_codec *codec,
					  unsigned int reg)
{
	u8 *reg_cache = codec->reg_cache;

	if (reg >= codec->reg_cache_size)
		return -EINVAL;

	return reg_cache[reg];
}

/*
 * read from the ad1938 register space
 */

static unsigned int ad1938_read_reg(struct snd_soc_codec *codec,
						unsigned int reg)
{
	char w_buf[AD1938_SPI_BUFLEN];
	char r_buf[AD1938_SPI_BUFLEN];
	int ret;

	struct spi_transfer t = {
		.tx_buf = w_buf,
		.rx_buf = r_buf,
		.len = AD1938_SPI_BUFLEN,
	};
	struct spi_message m;

	w_buf[0] = (AD1938_SPI_ADDR << 1) | AD1938_SPI_READ;
	w_buf[1] = reg;
	w_buf[2] = 0;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(codec->control_data, &m);
	if (ret == 0)
		return	r_buf[2];
	else
		return -EIO;
}

static int ad1938_fill_cache(struct snd_soc_codec *codec)
{
	int i;
	u8 *reg_cache = codec->reg_cache;
	struct spi_device *spi = codec->control_data;

	for (i = 0; i < codec->reg_cache_size; i++) {
		int ret = ad1938_read_reg(codec, i);
		if (ret == -EIO) {
			dev_err(&spi->dev, "AD1938 SPI read failure\n");
			return ret;
		}
		reg_cache[i] = ret;
	}

	return 0;
}

static int __devinit ad1938_spi_probe(struct spi_device *spi)
{
	struct snd_soc_codec *codec;
	struct ad1938_priv *ad1938;

	ad1938 = kzalloc(sizeof(struct ad1938_priv), GFP_KERNEL);
	if (ad1938 == NULL)
		return -ENOMEM;

	codec = &ad1938->codec;
	codec->control_data = spi;
	codec->dev = &spi->dev;

	dev_set_drvdata(&spi->dev, ad1938);

	return ad1938_register(ad1938);
}

static int __devexit ad1938_spi_remove(struct spi_device *spi)
{
	struct ad1938_priv *ad1938 = dev_get_drvdata(&spi->dev);

	ad1938_unregister(ad1938);
	return 0;
}

static struct spi_driver ad1938_spi_driver = {
	.driver = {
		.name	= "ad1938",
		.owner	= THIS_MODULE,
	},
	.probe		= ad1938_spi_probe,
	.remove		= __devexit_p(ad1938_spi_remove),
};

static struct snd_soc_dai_ops ad1938_dai_ops = {
	.hw_params = ad1938_hw_params,
	.digital_mute = ad1938_mute,
	.set_tdm_slot = ad1938_set_tdm_slot,
	.set_fmt = ad1938_set_dai_fmt,
};

/* codec DAI instance */
struct snd_soc_dai ad1938_dai = {
	.name = "AD1938",
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
	.ops = &ad1938_dai_ops,
};
EXPORT_SYMBOL_GPL(ad1938_dai);

static int ad1938_register(struct ad1938_priv *ad1938)
{
	int ret;
	struct snd_soc_codec *codec = &ad1938->codec;

	if (ad1938_codec) {
		dev_err(codec->dev, "Another ad1938 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	codec->private_data = ad1938;
	codec->reg_cache = ad1938->reg_cache;
	codec->reg_cache_size = AD1938_NUM_REGS;
	codec->name = "AD1938";
	codec->owner = THIS_MODULE;
	codec->dai = &ad1938_dai;
	codec->num_dai = 1;
	codec->write = ad1938_write_reg;
	codec->read = ad1938_read_reg_cache;
	codec->set_bias_level = ad1938_set_bias_level;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	ad1938_dai.dev = codec->dev;
	ad1938_codec = codec;

	/* default setting for ad1938 */

	/* unmute dac channels */
	codec->write(codec, AD1938_DAC_CHNL_MUTE, 0x0);
	/* de-emphasis: 48kHz, powedown dac */
	codec->write(codec, AD1938_DAC_CTRL2, 0x1A);
	/* powerdown dac, dac in tdm mode */
	codec->write(codec, AD1938_DAC_CTRL0, 0x41);
	/* high-pass filter enable */
	codec->write(codec, AD1938_ADC_CTRL0, 0x3);
	/* sata delay=1, adc aux mode */
	codec->write(codec, AD1938_ADC_CTRL1, 0x43);
	/* pll input: mclki/xi */
	codec->write(codec, AD1938_PLL_CLK_CTRL0, 0x9D);
	codec->write(codec, AD1938_PLL_CLK_CTRL1, 0x04);

	ad1938_fill_cache(codec);

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		kfree(ad1938);
		return ret;
	}

	ret = snd_soc_register_dai(&ad1938_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		kfree(ad1938);
		return ret;
	}

	return 0;
}

static void ad1938_unregister(struct ad1938_priv *ad1938)
{
	ad1938_set_bias_level(&ad1938->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&ad1938_dai);
	snd_soc_unregister_codec(&ad1938->codec);
	kfree(ad1938);
	ad1938_codec = NULL;
}

static int ad1938_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (ad1938_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = ad1938_codec;
	codec = ad1938_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, ad1938_snd_controls,
			     ARRAY_SIZE(ad1938_snd_controls));
	snd_soc_dapm_new_controls(codec, ad1938_dapm_widgets,
				  ARRAY_SIZE(ad1938_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_paths, ARRAY_SIZE(audio_paths));
	snd_soc_dapm_new_widgets(codec);

	ad1938_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

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
static int ad1938_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

#ifdef CONFIG_PM
static int ad1938_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	ad1938_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int ad1938_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (codec->suspend_bias_level == SND_SOC_BIAS_ON)
		ad1938_set_bias_level(codec, SND_SOC_BIAS_ON);

	return 0;
}
#else
#define ad1938_suspend NULL
#define ad1938_resume NULL
#endif

struct snd_soc_codec_device soc_codec_dev_ad1938 = {
	.probe = 	ad1938_probe,
	.remove = 	ad1938_remove,
	.suspend =      ad1938_suspend,
	.resume =       ad1938_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ad1938);

static int __init ad1938_init(void)
{
	int ret;

	ret = spi_register_driver(&ad1938_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register ad1938 SPI driver: %d\n",
				ret);
	}

	return ret;
}
module_init(ad1938_init);

static void __exit ad1938_exit(void)
{
	spi_unregister_driver(&ad1938_spi_driver);
}
module_exit(ad1938_exit);

MODULE_DESCRIPTION("ASoC ad1938 driver");
MODULE_AUTHOR("Barry Song ");
MODULE_LICENSE("GPL");
