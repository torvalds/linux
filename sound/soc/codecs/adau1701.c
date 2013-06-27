/*
 * Driver for ADAU1701 SigmaDSP processor
 *
 * Copyright 2011 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *	based on an inital version by Cliff Cai <cliff.cai@analog.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "sigmadsp.h"
#include "adau1701.h"

#define ADAU1701_DSPCTRL	0x081c
#define ADAU1701_SEROCTL	0x081e
#define ADAU1701_SERICTL	0x081f

#define ADAU1701_AUXNPOW	0x0822
#define ADAU1701_PINCONF_0	0x0820
#define ADAU1701_PINCONF_1	0x0821
#define ADAU1701_AUXNPOW	0x0822

#define ADAU1701_OSCIPOW	0x0826
#define ADAU1701_DACSET		0x0827

#define ADAU1701_MAX_REGISTER	0x0828

#define ADAU1701_DSPCTRL_CR		(1 << 2)
#define ADAU1701_DSPCTRL_DAM		(1 << 3)
#define ADAU1701_DSPCTRL_ADM		(1 << 4)
#define ADAU1701_DSPCTRL_SR_48		0x00
#define ADAU1701_DSPCTRL_SR_96		0x01
#define ADAU1701_DSPCTRL_SR_192		0x02
#define ADAU1701_DSPCTRL_SR_MASK	0x03

#define ADAU1701_SEROCTL_INV_LRCLK	0x2000
#define ADAU1701_SEROCTL_INV_BCLK	0x1000
#define ADAU1701_SEROCTL_MASTER		0x0800

#define ADAU1701_SEROCTL_OBF16		0x0000
#define ADAU1701_SEROCTL_OBF8		0x0200
#define ADAU1701_SEROCTL_OBF4		0x0400
#define ADAU1701_SEROCTL_OBF2		0x0600
#define ADAU1701_SEROCTL_OBF_MASK	0x0600

#define ADAU1701_SEROCTL_OLF1024	0x0000
#define ADAU1701_SEROCTL_OLF512		0x0080
#define ADAU1701_SEROCTL_OLF256		0x0100
#define ADAU1701_SEROCTL_OLF_MASK	0x0180

#define ADAU1701_SEROCTL_MSB_DEALY1	0x0000
#define ADAU1701_SEROCTL_MSB_DEALY0	0x0004
#define ADAU1701_SEROCTL_MSB_DEALY8	0x0008
#define ADAU1701_SEROCTL_MSB_DEALY12	0x000c
#define ADAU1701_SEROCTL_MSB_DEALY16	0x0010
#define ADAU1701_SEROCTL_MSB_DEALY_MASK	0x001c

#define ADAU1701_SEROCTL_WORD_LEN_24	0x0000
#define ADAU1701_SEROCTL_WORD_LEN_20	0x0001
#define ADAU1701_SEROCTL_WORD_LEN_16	0x0010
#define ADAU1701_SEROCTL_WORD_LEN_MASK	0x0003

#define ADAU1701_AUXNPOW_VBPD		0x40
#define ADAU1701_AUXNPOW_VRPD		0x20

#define ADAU1701_SERICTL_I2S		0
#define ADAU1701_SERICTL_LEFTJ		1
#define ADAU1701_SERICTL_TDM		2
#define ADAU1701_SERICTL_RIGHTJ_24	3
#define ADAU1701_SERICTL_RIGHTJ_20	4
#define ADAU1701_SERICTL_RIGHTJ_18	5
#define ADAU1701_SERICTL_RIGHTJ_16	6
#define ADAU1701_SERICTL_MODE_MASK	7
#define ADAU1701_SERICTL_INV_BCLK	BIT(3)
#define ADAU1701_SERICTL_INV_LRCLK	BIT(4)

#define ADAU1701_OSCIPOW_OPD		0x04
#define ADAU1701_DACSET_DACINIT		1

#define ADAU1707_CLKDIV_UNSET		(-1UL)

#define ADAU1701_FIRMWARE "adau1701.bin"

struct adau1701 {
	int gpio_nreset;
	int gpio_pll_mode[2];
	unsigned int dai_fmt;
	unsigned int pll_clkdiv;
	unsigned int sysclk;
	struct regmap *regmap;
	u8 pin_config[12];
};

static const struct snd_kcontrol_new adau1701_controls[] = {
	SOC_SINGLE("Master Capture Switch", ADAU1701_DSPCTRL, 4, 1, 0),
};

static const struct snd_soc_dapm_widget adau1701_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC0", "Playback", ADAU1701_AUXNPOW, 3, 1),
	SND_SOC_DAPM_DAC("DAC1", "Playback", ADAU1701_AUXNPOW, 2, 1),
	SND_SOC_DAPM_DAC("DAC2", "Playback", ADAU1701_AUXNPOW, 1, 1),
	SND_SOC_DAPM_DAC("DAC3", "Playback", ADAU1701_AUXNPOW, 0, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", ADAU1701_AUXNPOW, 7, 1),

	SND_SOC_DAPM_OUTPUT("OUT0"),
	SND_SOC_DAPM_OUTPUT("OUT1"),
	SND_SOC_DAPM_OUTPUT("OUT2"),
	SND_SOC_DAPM_OUTPUT("OUT3"),
	SND_SOC_DAPM_INPUT("IN0"),
	SND_SOC_DAPM_INPUT("IN1"),
};

static const struct snd_soc_dapm_route adau1701_dapm_routes[] = {
	{ "OUT0", NULL, "DAC0" },
	{ "OUT1", NULL, "DAC1" },
	{ "OUT2", NULL, "DAC2" },
	{ "OUT3", NULL, "DAC3" },

	{ "ADC", NULL, "IN0" },
	{ "ADC", NULL, "IN1" },
};

static unsigned int adau1701_register_size(struct device *dev,
		unsigned int reg)
{
	switch (reg) {
	case ADAU1701_PINCONF_0:
	case ADAU1701_PINCONF_1:
		return 3;
	case ADAU1701_DSPCTRL:
	case ADAU1701_SEROCTL:
	case ADAU1701_AUXNPOW:
	case ADAU1701_OSCIPOW:
	case ADAU1701_DACSET:
		return 2;
	case ADAU1701_SERICTL:
		return 1;
	}

	dev_err(dev, "Unsupported register address: %d\n", reg);
	return 0;
}

static bool adau1701_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADAU1701_DACSET:
		return true;
	default:
		return false;
	}
}

static int adau1701_reg_write(void *context, unsigned int reg,
			      unsigned int value)
{
	struct i2c_client *client = context;
	unsigned int i;
	unsigned int size;
	uint8_t buf[5];
	int ret;

	size = adau1701_register_size(&client->dev, reg);
	if (size == 0)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	for (i = size + 1; i >= 2; --i) {
		buf[i] = value;
		value >>= 8;
	}

	ret = i2c_master_send(client, buf, size + 2);
	if (ret == size + 2)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int adau1701_reg_read(void *context, unsigned int reg,
			     unsigned int *value)
{
	int ret;
	unsigned int i;
	unsigned int size;
	uint8_t send_buf[2], recv_buf[3];
	struct i2c_client *client = context;
	struct i2c_msg msgs[2];

	size = adau1701_register_size(&client->dev, reg);
	if (size == 0)
		return -EINVAL;

	send_buf[0] = reg >> 8;
	send_buf[1] = reg & 0xff;

	msgs[0].addr = client->addr;
	msgs[0].len = sizeof(send_buf);
	msgs[0].buf = send_buf;
	msgs[0].flags = 0;

	msgs[1].addr = client->addr;
	msgs[1].len = size;
	msgs[1].buf = recv_buf;
	msgs[1].flags = I2C_M_RD;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	else if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*value = 0;

	for (i = 0; i < size; i++)
		*value |= recv_buf[i] << (i * 8);

	return 0;
}

static int adau1701_reset(struct snd_soc_codec *codec, unsigned int clkdiv)
{
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = to_i2c_client(codec->dev);
	int ret;

	if (clkdiv != ADAU1707_CLKDIV_UNSET &&
	    gpio_is_valid(adau1701->gpio_pll_mode[0]) &&
	    gpio_is_valid(adau1701->gpio_pll_mode[1])) {
		switch (clkdiv) {
		case 64:
			gpio_set_value(adau1701->gpio_pll_mode[0], 0);
			gpio_set_value(adau1701->gpio_pll_mode[1], 0);
			break;
		case 256:
			gpio_set_value(adau1701->gpio_pll_mode[0], 0);
			gpio_set_value(adau1701->gpio_pll_mode[1], 1);
			break;
		case 384:
			gpio_set_value(adau1701->gpio_pll_mode[0], 1);
			gpio_set_value(adau1701->gpio_pll_mode[1], 0);
			break;
		case 0:	/* fallback */
		case 512:
			gpio_set_value(adau1701->gpio_pll_mode[0], 1);
			gpio_set_value(adau1701->gpio_pll_mode[1], 1);
			break;
		}
	}

	adau1701->pll_clkdiv = clkdiv;

	if (gpio_is_valid(adau1701->gpio_nreset)) {
		gpio_set_value(adau1701->gpio_nreset, 0);
		/* minimum reset time is 20ns */
		udelay(1);
		gpio_set_value(adau1701->gpio_nreset, 1);
		/* power-up time may be as long as 85ms */
		mdelay(85);
	}

	/*
	 * Postpone the firmware download to a point in time when we
	 * know the correct PLL setup
	 */
	if (clkdiv != ADAU1707_CLKDIV_UNSET) {
		ret = process_sigma_firmware(client, ADAU1701_FIRMWARE);
		if (ret) {
			dev_warn(codec->dev, "Failed to load firmware\n");
			return ret;
		}
	}

	regmap_write(adau1701->regmap, ADAU1701_DACSET, ADAU1701_DACSET_DACINIT);
	regmap_write(adau1701->regmap, ADAU1701_DSPCTRL, ADAU1701_DSPCTRL_CR);

	regcache_mark_dirty(adau1701->regmap);
	regcache_sync(adau1701->regmap);

	return 0;
}

static int adau1701_set_capture_pcm_format(struct snd_soc_codec *codec,
		snd_pcm_format_t format)
{
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	unsigned int mask = ADAU1701_SEROCTL_WORD_LEN_MASK;
	unsigned int val;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = ADAU1701_SEROCTL_WORD_LEN_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = ADAU1701_SEROCTL_WORD_LEN_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = ADAU1701_SEROCTL_WORD_LEN_24;
		break;
	default:
		return -EINVAL;
	}

	if (adau1701->dai_fmt == SND_SOC_DAIFMT_RIGHT_J) {
		switch (format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			val |= ADAU1701_SEROCTL_MSB_DEALY16;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			val |= ADAU1701_SEROCTL_MSB_DEALY12;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			val |= ADAU1701_SEROCTL_MSB_DEALY8;
			break;
		}
		mask |= ADAU1701_SEROCTL_MSB_DEALY_MASK;
	}

	regmap_update_bits(adau1701->regmap, ADAU1701_SEROCTL, mask, val);

	return 0;
}

static int adau1701_set_playback_pcm_format(struct snd_soc_codec *codec,
			snd_pcm_format_t format)
{
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	if (adau1701->dai_fmt != SND_SOC_DAIFMT_RIGHT_J)
		return 0;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = ADAU1701_SERICTL_RIGHTJ_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = ADAU1701_SERICTL_RIGHTJ_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = ADAU1701_SERICTL_RIGHTJ_24;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(adau1701->regmap, ADAU1701_SERICTL,
		ADAU1701_SERICTL_MODE_MASK, val);

	return 0;
}

static int adau1701_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	unsigned int clkdiv = adau1701->sysclk / params_rate(params);
	snd_pcm_format_t format;
	unsigned int val;
	int ret;

	/*
	 * If the mclk/lrclk ratio changes, the chip needs updated PLL
	 * mode GPIO settings, and a full reset cycle, including a new
	 * firmware upload.
	 */
	if (clkdiv != adau1701->pll_clkdiv) {
		ret = adau1701_reset(codec, clkdiv);
		if (ret < 0)
			return ret;
	}

	switch (params_rate(params)) {
	case 192000:
		val = ADAU1701_DSPCTRL_SR_192;
		break;
	case 96000:
		val = ADAU1701_DSPCTRL_SR_96;
		break;
	case 48000:
		val = ADAU1701_DSPCTRL_SR_48;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(adau1701->regmap, ADAU1701_DSPCTRL,
		ADAU1701_DSPCTRL_SR_MASK, val);

	format = params_format(params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return adau1701_set_playback_pcm_format(codec, format);
	else
		return adau1701_set_capture_pcm_format(codec, format);
}

static int adau1701_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	unsigned int serictl = 0x00, seroctl = 0x00;
	bool invert_lrclk;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* master, 64-bits per sample, 1 frame per sample */
		seroctl |= ADAU1701_SEROCTL_MASTER | ADAU1701_SEROCTL_OBF16
				| ADAU1701_SEROCTL_OLF1024;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		invert_lrclk = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		invert_lrclk = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert_lrclk = false;
		serictl |= ADAU1701_SERICTL_INV_BCLK;
		seroctl |= ADAU1701_SEROCTL_INV_BCLK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		invert_lrclk = true;
		serictl |= ADAU1701_SERICTL_INV_BCLK;
		seroctl |= ADAU1701_SEROCTL_INV_BCLK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		serictl |= ADAU1701_SERICTL_LEFTJ;
		seroctl |= ADAU1701_SEROCTL_MSB_DEALY0;
		invert_lrclk = !invert_lrclk;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		serictl |= ADAU1701_SERICTL_RIGHTJ_24;
		seroctl |= ADAU1701_SEROCTL_MSB_DEALY8;
		invert_lrclk = !invert_lrclk;
		break;
	default:
		return -EINVAL;
	}

	if (invert_lrclk) {
		seroctl |= ADAU1701_SEROCTL_INV_LRCLK;
		serictl |= ADAU1701_SERICTL_INV_LRCLK;
	}

	adau1701->dai_fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	regmap_write(adau1701->regmap, ADAU1701_SERICTL, serictl);
	regmap_update_bits(adau1701->regmap, ADAU1701_SEROCTL,
		~ADAU1701_SEROCTL_WORD_LEN_MASK, seroctl);

	return 0;
}

static int adau1701_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	unsigned int mask = ADAU1701_AUXNPOW_VBPD | ADAU1701_AUXNPOW_VRPD;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* Enable VREF and VREF buffer */
		regmap_update_bits(adau1701->regmap,
				   ADAU1701_AUXNPOW, mask, 0x00);
		break;
	case SND_SOC_BIAS_OFF:
		/* Disable VREF and VREF buffer */
		regmap_update_bits(adau1701->regmap,
				   ADAU1701_AUXNPOW, mask, mask);
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

static int adau1701_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int mask = ADAU1701_DSPCTRL_DAM;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	if (mute)
		val = 0;
	else
		val = mask;

	regmap_update_bits(adau1701->regmap, ADAU1701_DSPCTRL, mask, val);

	return 0;
}

static int adau1701_set_sysclk(struct snd_soc_codec *codec, int clk_id,
	int source, unsigned int freq, int dir)
{
	unsigned int val;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case ADAU1701_CLK_SRC_OSC:
		val = 0x0;
		break;
	case ADAU1701_CLK_SRC_MCLK:
		val = ADAU1701_OSCIPOW_OPD;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(adau1701->regmap, ADAU1701_OSCIPOW,
			   ADAU1701_OSCIPOW_OPD, val);
	adau1701->sysclk = freq;

	return 0;
}

#define ADAU1701_RATES (SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
	SNDRV_PCM_RATE_192000)

#define ADAU1701_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops adau1701_dai_ops = {
	.set_fmt	= adau1701_set_dai_fmt,
	.hw_params	= adau1701_hw_params,
	.digital_mute	= adau1701_digital_mute,
};

static struct snd_soc_dai_driver adau1701_dai = {
	.name = "adau1701",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = ADAU1701_RATES,
		.formats = ADAU1701_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = ADAU1701_RATES,
		.formats = ADAU1701_FORMATS,
	},
	.ops = &adau1701_dai_ops,
	.symmetric_rates = 1,
};

#ifdef CONFIG_OF
static const struct of_device_id adau1701_dt_ids[] = {
	{ .compatible = "adi,adau1701", },
	{ }
};
MODULE_DEVICE_TABLE(of, adau1701_dt_ids);
#endif

static int adau1701_probe(struct snd_soc_codec *codec)
{
	int i, ret;
	unsigned int val;
	struct adau1701 *adau1701 = snd_soc_codec_get_drvdata(codec);

	codec->control_data = to_i2c_client(codec->dev);

	/*
	 * Let the pll_clkdiv variable default to something that won't happen
	 * at runtime. That way, we can postpone the firmware download from
	 * adau1701_reset() to a point in time when we know the correct PLL
	 * mode parameters.
	 */
	adau1701->pll_clkdiv = ADAU1707_CLKDIV_UNSET;

	/* initalize with pre-configured pll mode settings */
	ret = adau1701_reset(codec, adau1701->pll_clkdiv);
	if (ret < 0)
		return ret;

	/* set up pin config */
	val = 0;
	for (i = 0; i < 6; i++)
		val |= adau1701->pin_config[i] << (i * 4);

	regmap_write(adau1701->regmap, ADAU1701_PINCONF_0, val);

	val = 0;
	for (i = 0; i < 6; i++)
		val |= adau1701->pin_config[i + 6] << (i * 4);

	regmap_write(adau1701->regmap, ADAU1701_PINCONF_1, val);

	return 0;
}

static struct snd_soc_codec_driver adau1701_codec_drv = {
	.probe			= adau1701_probe,
	.set_bias_level		= adau1701_set_bias_level,
	.idle_bias_off		= true,

	.controls		= adau1701_controls,
	.num_controls		= ARRAY_SIZE(adau1701_controls),
	.dapm_widgets		= adau1701_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adau1701_dapm_widgets),
	.dapm_routes		= adau1701_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(adau1701_dapm_routes),

	.set_sysclk		= adau1701_set_sysclk,
};

static const struct regmap_config adau1701_regmap = {
	.reg_bits		= 16,
	.val_bits		= 32,
	.max_register		= ADAU1701_MAX_REGISTER,
	.cache_type		= REGCACHE_RBTREE,
	.volatile_reg		= adau1701_volatile_reg,
	.reg_write		= adau1701_reg_write,
	.reg_read		= adau1701_reg_read,
};

static int adau1701_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct adau1701 *adau1701;
	struct device *dev = &client->dev;
	int gpio_nreset = -EINVAL;
	int gpio_pll_mode[2] = { -EINVAL, -EINVAL };
	int ret;

	adau1701 = devm_kzalloc(dev, sizeof(*adau1701), GFP_KERNEL);
	if (!adau1701)
		return -ENOMEM;

	adau1701->regmap = devm_regmap_init(dev, NULL, client,
					    &adau1701_regmap);
	if (IS_ERR(adau1701->regmap))
		return PTR_ERR(adau1701->regmap);

	if (dev->of_node) {
		gpio_nreset = of_get_named_gpio(dev->of_node, "reset-gpio", 0);
		if (gpio_nreset < 0 && gpio_nreset != -ENOENT)
			return gpio_nreset;

		gpio_pll_mode[0] = of_get_named_gpio(dev->of_node,
						   "adi,pll-mode-gpios", 0);
		if (gpio_pll_mode[0] < 0 && gpio_pll_mode[0] != -ENOENT)
			return gpio_pll_mode[0];

		gpio_pll_mode[1] = of_get_named_gpio(dev->of_node,
						   "adi,pll-mode-gpios", 1);
		if (gpio_pll_mode[1] < 0 && gpio_pll_mode[1] != -ENOENT)
			return gpio_pll_mode[1];

		of_property_read_u32(dev->of_node, "adi,pll-clkdiv",
				     &adau1701->pll_clkdiv);

		of_property_read_u8_array(dev->of_node, "adi,pin-config",
					  adau1701->pin_config,
					  ARRAY_SIZE(adau1701->pin_config));
	}

	if (gpio_is_valid(gpio_nreset)) {
		ret = devm_gpio_request_one(dev, gpio_nreset, GPIOF_OUT_INIT_LOW,
					    "ADAU1701 Reset");
		if (ret < 0)
			return ret;
	}

	if (gpio_is_valid(gpio_pll_mode[0]) &&
	    gpio_is_valid(gpio_pll_mode[1])) {
		ret = devm_gpio_request_one(dev, gpio_pll_mode[0],
					    GPIOF_OUT_INIT_LOW,
					    "ADAU1701 PLL mode 0");
		if (ret < 0)
			return ret;

		ret = devm_gpio_request_one(dev, gpio_pll_mode[1],
					    GPIOF_OUT_INIT_LOW,
					    "ADAU1701 PLL mode 1");
		if (ret < 0)
			return ret;
	}

	adau1701->gpio_nreset = gpio_nreset;
	adau1701->gpio_pll_mode[0] = gpio_pll_mode[0];
	adau1701->gpio_pll_mode[1] = gpio_pll_mode[1];

	i2c_set_clientdata(client, adau1701);
	ret = snd_soc_register_codec(&client->dev, &adau1701_codec_drv,
			&adau1701_dai, 1);
	return ret;
}

static int adau1701_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id adau1701_i2c_id[] = {
	{ "adau1701", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1701_i2c_id);

static struct i2c_driver adau1701_i2c_driver = {
	.driver = {
		.name	= "adau1701",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(adau1701_dt_ids),
	},
	.probe		= adau1701_i2c_probe,
	.remove		= adau1701_i2c_remove,
	.id_table	= adau1701_i2c_id,
};

module_i2c_driver(adau1701_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1701 SigmaDSP driver");
MODULE_AUTHOR("Cliff Cai <cliff.cai@analog.com>");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
