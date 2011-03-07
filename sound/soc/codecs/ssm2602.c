/*
 * File:         sound/soc/codecs/ssm2602.c
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 * Description:  Driver for ssm2602 sound chip
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "ssm2602.h"

#define SSM2602_VERSION "0.1"

/* codec private data */
struct ssm2602_priv {
	unsigned int sysclk;
	enum snd_soc_control_type control_type;
	void *control_data;
	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

/*
 * ssm2602 register cache
 * We can't read the ssm2602 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const u16 ssm2602_reg[SSM2602_CACHEREGNUM] = {
	0x0017, 0x0017, 0x0079, 0x0079,
	0x0000, 0x0000, 0x0000, 0x000a,
	0x0000, 0x0000
};

/*
 * read ssm2602 register cache
 */
static inline unsigned int ssm2602_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == SSM2602_RESET)
		return 0;
	if (reg >= SSM2602_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write ssm2602 register cache
 */
static inline void ssm2602_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= SSM2602_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the ssm2602 register space
 */
static int ssm2602_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 ssm2602 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	ssm2602_write_reg_cache(codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define ssm2602_reset(c)	ssm2602_write(c, SSM2602_RESET, 0)

/*Appending several "None"s just for OSS mixer use*/
static const char *ssm2602_input_select[] = {
	"Line", "Mic", "None", "None", "None",
	"None", "None", "None",
};

static const char *ssm2602_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};

static const struct soc_enum ssm2602_enum[] = {
	SOC_ENUM_SINGLE(SSM2602_APANA, 2, 2, ssm2602_input_select),
	SOC_ENUM_SINGLE(SSM2602_APDIGI, 1, 4, ssm2602_deemph),
};

static const struct snd_kcontrol_new ssm2602_snd_controls[] = {

SOC_DOUBLE_R("Master Playback Volume", SSM2602_LOUT1V, SSM2602_ROUT1V,
	0, 127, 0),
SOC_DOUBLE_R("Master Playback ZC Switch", SSM2602_LOUT1V, SSM2602_ROUT1V,
	7, 1, 0),

SOC_DOUBLE_R("Capture Volume", SSM2602_LINVOL, SSM2602_RINVOL, 0, 31, 0),
SOC_DOUBLE_R("Capture Switch", SSM2602_LINVOL, SSM2602_RINVOL, 7, 1, 1),

SOC_SINGLE("Mic Boost (+20dB)", SSM2602_APANA, 0, 1, 0),
SOC_SINGLE("Mic Boost2 (+20dB)", SSM2602_APANA, 7, 1, 0),
SOC_SINGLE("Mic Switch", SSM2602_APANA, 1, 1, 1),

SOC_SINGLE("Sidetone Playback Volume", SSM2602_APANA, 6, 3, 1),

SOC_SINGLE("ADC High Pass Filter Switch", SSM2602_APDIGI, 0, 1, 1),
SOC_SINGLE("Store DC Offset Switch", SSM2602_APDIGI, 4, 1, 0),

SOC_ENUM("Capture Source", ssm2602_enum[0]),

SOC_ENUM("Playback De-emphasis", ssm2602_enum[1]),
};

/* Output Mixer */
static const struct snd_kcontrol_new ssm2602_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", SSM2602_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("Mic Sidetone Switch", SSM2602_APANA, 5, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", SSM2602_APANA, 4, 1, 0),
};

/* Input mux */
static const struct snd_kcontrol_new ssm2602_input_mux_controls =
SOC_DAPM_ENUM("Input Select", ssm2602_enum[0]);

static const struct snd_soc_dapm_widget ssm2602_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Output Mixer", SSM2602_PWR, 4, 1,
	&ssm2602_output_mixer_controls[0],
	ARRAY_SIZE(ssm2602_output_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", SSM2602_PWR, 3, 1),
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", SSM2602_PWR, 2, 1),
SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0, &ssm2602_input_mux_controls),
SND_SOC_DAPM_PGA("Line Input", SSM2602_PWR, 0, 1, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias", SSM2602_PWR, 1, 1),
SND_SOC_DAPM_INPUT("MICIN"),
SND_SOC_DAPM_INPUT("RLINEIN"),
SND_SOC_DAPM_INPUT("LLINEIN"),
};

static const struct snd_soc_dapm_route audio_conn[] = {
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
	{"Input Mux", "Line", "Line Input"},
	{"Input Mux", "Mic", "Mic Bias"},
	{"ADC", NULL, "Input Mux"},

	/* inputs */
	{"Line Input", NULL, "LLINEIN"},
	{"Line Input", NULL, "RLINEIN"},
	{"Mic Bias", NULL, "MICIN"},
};

static int ssm2602_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, ssm2602_dapm_widgets,
				  ARRAY_SIZE(ssm2602_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_conn, ARRAY_SIZE(audio_conn));

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
	return i;
}

static int ssm2602_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	u16 srate;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = codec->control_data;
	u16 iface = ssm2602_read_reg_cache(codec, SSM2602_IFACE) & 0xfff3;
	int i = get_coeff(ssm2602->sysclk, params_rate(params));

	if (substream == ssm2602->slave_substream) {
		dev_dbg(&i2c->dev, "Ignoring hw_params for slave substream\n");
		return 0;
	}

	/*no match is found*/
	if (i == ARRAY_SIZE(coeff_div))
		return -EINVAL;

	srate = (coeff_div[i].sr << 2) |
		(coeff_div[i].bosr << 1) | coeff_div[i].usb;

	ssm2602_write(codec, SSM2602_ACTIVE, 0);
	ssm2602_write(codec, SSM2602_SRATE, srate);

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
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x000c;
		break;
	}
	ssm2602_write(codec, SSM2602_IFACE, iface);
	ssm2602_write(codec, SSM2602_ACTIVE, ACTIVE_ACTIVATE_CODEC);
	return 0;
}

static int ssm2602_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = codec->control_data;
	struct snd_pcm_runtime *master_runtime;

	/* The DAI has shared clocks so if we already have a playback or
	 * capture going then constrain this substream to match it.
	 * TODO: the ssm2602 allows pairs of non-matching PB/REC rates
	 */
	if (ssm2602->master_substream) {
		master_runtime = ssm2602->master_substream->runtime;
		dev_dbg(&i2c->dev, "Constraining to %d bits at %dHz\n",
			master_runtime->sample_bits,
			master_runtime->rate);

		if (master_runtime->rate != 0)
			snd_pcm_hw_constraint_minmax(substream->runtime,
						     SNDRV_PCM_HW_PARAM_RATE,
						     master_runtime->rate,
						     master_runtime->rate);

		if (master_runtime->sample_bits != 0)
			snd_pcm_hw_constraint_minmax(substream->runtime,
						     SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
						     master_runtime->sample_bits,
						     master_runtime->sample_bits);

		ssm2602->slave_substream = substream;
	} else
		ssm2602->master_substream = substream;

	return 0;
}

static int ssm2602_pcm_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	/* set active */
	ssm2602_write(codec, SSM2602_ACTIVE, ACTIVE_ACTIVATE_CODEC);

	return 0;
}

static void ssm2602_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);

	/* deactivate */
	if (!codec->active)
		ssm2602_write(codec, SSM2602_ACTIVE, 0);

	if (ssm2602->master_substream == substream)
		ssm2602->master_substream = ssm2602->slave_substream;

	ssm2602->slave_substream = NULL;
}

static int ssm2602_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = ssm2602_read_reg_cache(codec, SSM2602_APDIGI) & ~APDIGI_ENABLE_DAC_MUTE;
	if (mute)
		ssm2602_write(codec, SSM2602_APDIGI,
				mute_reg | APDIGI_ENABLE_DAC_MUTE);
	else
		ssm2602_write(codec, SSM2602_APDIGI, mute_reg);
	return 0;
}

static int ssm2602_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		ssm2602->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int ssm2602_set_dai_fmt(struct snd_soc_dai *codec_dai,
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
		iface |= 0x0013;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0003;
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
	ssm2602_write(codec, SSM2602_IFACE, iface);
	return 0;
}

static int ssm2602_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 reg = ssm2602_read_reg_cache(codec, SSM2602_PWR) & 0xff7f;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* vref/mid, osc on, dac unmute */
		ssm2602_write(codec, SSM2602_PWR, reg);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		ssm2602_write(codec, SSM2602_PWR, reg | PWR_CLK_OUT_PDN);
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		ssm2602_write(codec, SSM2602_ACTIVE, 0);
		ssm2602_write(codec, SSM2602_PWR, 0xffff);
		break;

	}
	codec->dapm.bias_level = level;
	return 0;
}

#define SSM2602_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_32000 |\
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
		SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define SSM2602_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops ssm2602_dai_ops = {
	.startup	= ssm2602_startup,
	.prepare	= ssm2602_pcm_prepare,
	.hw_params	= ssm2602_hw_params,
	.shutdown	= ssm2602_shutdown,
	.digital_mute	= ssm2602_mute,
	.set_sysclk	= ssm2602_set_dai_sysclk,
	.set_fmt	= ssm2602_set_dai_fmt,
};

static struct snd_soc_dai_driver ssm2602_dai = {
	.name = "ssm2602-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SSM2602_RATES,
		.formats = SSM2602_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SSM2602_RATES,
		.formats = SSM2602_FORMATS,},
	.ops = &ssm2602_dai_ops,
};

static int ssm2602_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	ssm2602_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int ssm2602_resume(struct snd_soc_codec *codec)
{
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(ssm2602_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	ssm2602_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int ssm2602_probe(struct snd_soc_codec *codec)
{
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	int ret = 0, reg;

	pr_info("ssm2602 Audio Codec %s", SSM2602_VERSION);

	codec->control_data = ssm2602->control_data;

	ssm2602_reset(codec);

	/*power on device*/
	ssm2602_write(codec, SSM2602_ACTIVE, 0);
	/* set the update bits */
	reg = ssm2602_read_reg_cache(codec, SSM2602_LINVOL);
	ssm2602_write(codec, SSM2602_LINVOL, reg | LINVOL_LRIN_BOTH);
	reg = ssm2602_read_reg_cache(codec, SSM2602_RINVOL);
	ssm2602_write(codec, SSM2602_RINVOL, reg | RINVOL_RLIN_BOTH);
	reg = ssm2602_read_reg_cache(codec, SSM2602_LOUT1V);
	ssm2602_write(codec, SSM2602_LOUT1V, reg | LOUT1V_LRHP_BOTH);
	reg = ssm2602_read_reg_cache(codec, SSM2602_ROUT1V);
	ssm2602_write(codec, SSM2602_ROUT1V, reg | ROUT1V_RLHP_BOTH);
	/*select Line in as default input*/
	ssm2602_write(codec, SSM2602_APANA, APANA_SELECT_DAC |
			APANA_ENABLE_MIC_BOOST);
	ssm2602_write(codec, SSM2602_PWR, 0);

	snd_soc_add_controls(codec, ssm2602_snd_controls,
				ARRAY_SIZE(ssm2602_snd_controls));
	ssm2602_add_widgets(codec);

	return ret;
}

/* remove everything here */
static int ssm2602_remove(struct snd_soc_codec *codec)
{
	ssm2602_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_ssm2602 = {
	.probe =	ssm2602_probe,
	.remove =	ssm2602_remove,
	.suspend =	ssm2602_suspend,
	.resume =	ssm2602_resume,
	.read = ssm2602_read_reg_cache,
	.write = ssm2602_write,
	.set_bias_level = ssm2602_set_bias_level,
	.reg_cache_size = sizeof(ssm2602_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = ssm2602_reg,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
/*
 * ssm2602 2 wire address is determined by GPIO5
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static int ssm2602_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct ssm2602_priv *ssm2602;
	int ret;

	ssm2602 = kzalloc(sizeof(struct ssm2602_priv), GFP_KERNEL);
	if (ssm2602 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ssm2602);
	ssm2602->control_data = i2c;
	ssm2602->control_type = SND_SOC_I2C;

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ssm2602, &ssm2602_dai, 1);
	if (ret < 0)
		kfree(ssm2602);
	return ret;
}

static int ssm2602_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id ssm2602_i2c_id[] = {
	{ "ssm2602", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssm2602_i2c_id);

/* corgi i2c codec control layer */
static struct i2c_driver ssm2602_i2c_driver = {
	.driver = {
		.name = "ssm2602-codec",
		.owner = THIS_MODULE,
	},
	.probe = ssm2602_i2c_probe,
	.remove = ssm2602_i2c_remove,
	.id_table = ssm2602_i2c_id,
};
#endif


static int __init ssm2602_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&ssm2602_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register SSM2602 I2C driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(ssm2602_modinit);

static void __exit ssm2602_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&ssm2602_i2c_driver);
#endif
}
module_exit(ssm2602_exit);

MODULE_DESCRIPTION("ASoC ssm2602 driver");
MODULE_AUTHOR("Cliff Cai");
MODULE_LICENSE("GPL");
