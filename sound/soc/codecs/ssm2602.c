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
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "ssm2602.h"

#define SSM2602_VERSION "0.1"

enum ssm2602_type {
	SSM2602,
	SSM2604,
};

/* codec private data */
struct ssm2602_priv {
	unsigned int sysclk;
	enum snd_soc_control_type control_type;
	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;

	enum ssm2602_type type;
};

/*
 * ssm2602 register cache
 * We can't read the ssm2602 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const u16 ssm2602_reg[SSM2602_CACHEREGNUM] = {
	0x0097, 0x0097, 0x0079, 0x0079,
	0x000a, 0x0008, 0x009f, 0x000a,
	0x0000, 0x0000
};

#define ssm2602_reset(c)	snd_soc_write(c, SSM2602_RESET, 0)

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

static const unsigned int ssm260x_outmix_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 47, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	48, 127, TLV_DB_SCALE_ITEM(-7400, 100, 0),
};

static const DECLARE_TLV_DB_SCALE(ssm260x_inpga_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(ssm260x_sidetone_tlv, -1500, 300, 0);

static const struct snd_kcontrol_new ssm260x_snd_controls[] = {
SOC_DOUBLE_R_TLV("Capture Volume", SSM2602_LINVOL, SSM2602_RINVOL, 0, 45, 0,
	ssm260x_inpga_tlv),
SOC_DOUBLE_R("Capture Switch", SSM2602_LINVOL, SSM2602_RINVOL, 7, 1, 1),

SOC_SINGLE("ADC High Pass Filter Switch", SSM2602_APDIGI, 0, 1, 1),
SOC_SINGLE("Store DC Offset Switch", SSM2602_APDIGI, 4, 1, 0),

SOC_ENUM("Playback De-emphasis", ssm2602_enum[1]),
};

static const struct snd_kcontrol_new ssm2602_snd_controls[] = {
SOC_DOUBLE_R_TLV("Master Playback Volume", SSM2602_LOUT1V, SSM2602_ROUT1V,
	0, 127, 0, ssm260x_outmix_tlv),
SOC_DOUBLE_R("Master Playback ZC Switch", SSM2602_LOUT1V, SSM2602_ROUT1V,
	7, 1, 0),
SOC_SINGLE_TLV("Sidetone Playback Volume", SSM2602_APANA, 6, 3, 1,
	ssm260x_sidetone_tlv),

SOC_SINGLE("Mic Boost (+20dB)", SSM2602_APANA, 0, 1, 0),
SOC_SINGLE("Mic Boost2 (+20dB)", SSM2602_APANA, 8, 1, 0),
SOC_SINGLE("Mic Switch", SSM2602_APANA, 1, 1, 1),
};

/* Output Mixer */
static const struct snd_kcontrol_new ssm260x_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", SSM2602_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", SSM2602_APANA, 4, 1, 0),
SOC_DAPM_SINGLE("Mic Sidetone Switch", SSM2602_APANA, 5, 1, 0),
};

/* Input mux */
static const struct snd_kcontrol_new ssm2602_input_mux_controls =
SOC_DAPM_ENUM("Input Select", ssm2602_enum[0]);

static const struct snd_soc_dapm_widget ssm260x_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", SSM2602_PWR, 3, 1),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", SSM2602_PWR, 2, 1),
SND_SOC_DAPM_PGA("Line Input", SSM2602_PWR, 0, 1, NULL, 0),

SND_SOC_DAPM_SUPPLY("Digital Core Power", SSM2602_ACTIVE, 0, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_INPUT("RLINEIN"),
SND_SOC_DAPM_INPUT("LLINEIN"),
};

static const struct snd_soc_dapm_widget ssm2602_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Output Mixer", SSM2602_PWR, 4, 1,
	ssm260x_output_mixer_controls,
	ARRAY_SIZE(ssm260x_output_mixer_controls)),

SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0, &ssm2602_input_mux_controls),
SND_SOC_DAPM_MICBIAS("Mic Bias", SSM2602_PWR, 1, 1),

SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
SND_SOC_DAPM_INPUT("MICIN"),
};

static const struct snd_soc_dapm_widget ssm2604_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Output Mixer", SND_SOC_NOPM, 0, 0,
	ssm260x_output_mixer_controls,
	ARRAY_SIZE(ssm260x_output_mixer_controls) - 1), /* Last element is the mic */
};

static const struct snd_soc_dapm_route ssm260x_routes[] = {
	{"DAC", NULL, "Digital Core Power"},
	{"ADC", NULL, "Digital Core Power"},

	{"Output Mixer", "Line Bypass Switch", "Line Input"},
	{"Output Mixer", "HiFi Playback Switch", "DAC"},

	{"ROUT", NULL, "Output Mixer"},
	{"LOUT", NULL, "Output Mixer"},

	{"Line Input", NULL, "LLINEIN"},
	{"Line Input", NULL, "RLINEIN"},
};

static const struct snd_soc_dapm_route ssm2602_routes[] = {
	{"Output Mixer", "Mic Sidetone Switch", "Mic Bias"},

	{"RHPOUT", NULL, "Output Mixer"},
	{"LHPOUT", NULL, "Output Mixer"},

	{"Input Mux", "Line", "Line Input"},
	{"Input Mux", "Mic", "Mic Bias"},
	{"ADC", NULL, "Input Mux"},

	{"Mic Bias", NULL, "MICIN"},
};

static const struct snd_soc_dapm_route ssm2604_routes[] = {
	{"ADC", NULL, "Line Input"},
};

struct ssm2602_coeff {
	u32 mclk;
	u32 rate;
	u8 srate;
};

#define SSM2602_COEFF_SRATE(sr, bosr, usb) (((sr) << 2) | ((bosr) << 1) | (usb))

/* codec mclk clock coefficients */
static const struct ssm2602_coeff ssm2602_coeff_table[] = {
	/* 48k */
	{12288000, 48000, SSM2602_COEFF_SRATE(0x0, 0x0, 0x0)},
	{18432000, 48000, SSM2602_COEFF_SRATE(0x0, 0x1, 0x0)},
	{12000000, 48000, SSM2602_COEFF_SRATE(0x0, 0x0, 0x1)},

	/* 32k */
	{12288000, 32000, SSM2602_COEFF_SRATE(0x6, 0x0, 0x0)},
	{18432000, 32000, SSM2602_COEFF_SRATE(0x6, 0x1, 0x0)},
	{12000000, 32000, SSM2602_COEFF_SRATE(0x6, 0x0, 0x1)},

	/* 8k */
	{12288000, 8000, SSM2602_COEFF_SRATE(0x3, 0x0, 0x0)},
	{18432000, 8000, SSM2602_COEFF_SRATE(0x3, 0x1, 0x0)},
	{11289600, 8000, SSM2602_COEFF_SRATE(0xb, 0x0, 0x0)},
	{16934400, 8000, SSM2602_COEFF_SRATE(0xb, 0x1, 0x0)},
	{12000000, 8000, SSM2602_COEFF_SRATE(0x3, 0x0, 0x1)},

	/* 96k */
	{12288000, 96000, SSM2602_COEFF_SRATE(0x7, 0x0, 0x0)},
	{18432000, 96000, SSM2602_COEFF_SRATE(0x7, 0x1, 0x0)},
	{12000000, 96000, SSM2602_COEFF_SRATE(0x7, 0x0, 0x1)},

	/* 44.1k */
	{11289600, 44100, SSM2602_COEFF_SRATE(0x8, 0x0, 0x0)},
	{16934400, 44100, SSM2602_COEFF_SRATE(0x8, 0x1, 0x0)},
	{12000000, 44100, SSM2602_COEFF_SRATE(0x8, 0x1, 0x1)},

	/* 88.2k */
	{11289600, 88200, SSM2602_COEFF_SRATE(0xf, 0x0, 0x0)},
	{16934400, 88200, SSM2602_COEFF_SRATE(0xf, 0x1, 0x0)},
	{12000000, 88200, SSM2602_COEFF_SRATE(0xf, 0x1, 0x1)},
};

static inline int ssm2602_get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ssm2602_coeff_table); i++) {
		if (ssm2602_coeff_table[i].rate == rate &&
			ssm2602_coeff_table[i].mclk == mclk)
			return ssm2602_coeff_table[i].srate;
	}
	return -EINVAL;
}

static int ssm2602_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, SSM2602_IFACE) & 0xfff3;
	int srate = ssm2602_get_coeff(ssm2602->sysclk, params_rate(params));

	if (substream == ssm2602->slave_substream) {
		dev_dbg(codec->dev, "Ignoring hw_params for slave substream\n");
		return 0;
	}

	if (srate < 0)
		return srate;

	snd_soc_write(codec, SSM2602_SRATE, srate);

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
	snd_soc_write(codec, SSM2602_IFACE, iface);
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

static void ssm2602_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);

	if (ssm2602->master_substream == substream)
		ssm2602->master_substream = ssm2602->slave_substream;

	ssm2602->slave_substream = NULL;
}


static int ssm2602_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, SSM2602_APDIGI) & ~APDIGI_ENABLE_DAC_MUTE;
	if (mute)
		snd_soc_write(codec, SSM2602_APDIGI,
				mute_reg | APDIGI_ENABLE_DAC_MUTE);
	else
		snd_soc_write(codec, SSM2602_APDIGI, mute_reg);
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
	snd_soc_write(codec, SSM2602_IFACE, iface);
	return 0;
}

static int ssm2602_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 reg = snd_soc_read(codec, SSM2602_PWR) & 0xff7f;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* vref/mid, osc on, dac unmute */
		snd_soc_write(codec, SSM2602_PWR, reg);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		snd_soc_write(codec, SSM2602_PWR, reg | PWR_CLK_OUT_PDN);
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		snd_soc_write(codec, SSM2602_PWR, 0xffff);
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
	snd_soc_cache_sync(codec);

	ssm2602_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static int ssm2602_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret, reg;

	reg = snd_soc_read(codec, SSM2602_LOUT1V);
	snd_soc_write(codec, SSM2602_LOUT1V, reg | LOUT1V_LRHP_BOTH);
	reg = snd_soc_read(codec, SSM2602_ROUT1V);
	snd_soc_write(codec, SSM2602_ROUT1V, reg | ROUT1V_RLHP_BOTH);

	ret = snd_soc_add_controls(codec, ssm2602_snd_controls,
			ARRAY_SIZE(ssm2602_snd_controls));
	if (ret)
		return ret;

	ret = snd_soc_dapm_new_controls(dapm, ssm2602_dapm_widgets,
			ARRAY_SIZE(ssm2602_dapm_widgets));
	if (ret)
		return ret;

	return snd_soc_dapm_add_routes(dapm, ssm2602_routes,
			ARRAY_SIZE(ssm2602_routes));
}

static int ssm2604_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, ssm2604_dapm_widgets,
			ARRAY_SIZE(ssm2604_dapm_widgets));
	if (ret)
		return ret;

	return snd_soc_dapm_add_routes(dapm, ssm2604_routes,
			ARRAY_SIZE(ssm2604_routes));
}

static int ssm260x_probe(struct snd_soc_codec *codec)
{
	struct ssm2602_priv *ssm2602 = snd_soc_codec_get_drvdata(codec);
	int ret, reg;

	pr_info("ssm2602 Audio Codec %s", SSM2602_VERSION);

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, ssm2602->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = ssm2602_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	/* set the update bits */
	reg = snd_soc_read(codec, SSM2602_LINVOL);
	snd_soc_write(codec, SSM2602_LINVOL, reg | LINVOL_LRIN_BOTH);
	reg = snd_soc_read(codec, SSM2602_RINVOL);
	snd_soc_write(codec, SSM2602_RINVOL, reg | RINVOL_RLIN_BOTH);
	/*select Line in as default input*/
	snd_soc_write(codec, SSM2602_APANA, APANA_SELECT_DAC |
			APANA_ENABLE_MIC_BOOST);

	switch (ssm2602->type) {
	case SSM2602:
		ret = ssm2602_probe(codec);
		break;
	case SSM2604:
		ret = ssm2604_probe(codec);
		break;
	}

	return ret;
}

/* remove everything here */
static int ssm2602_remove(struct snd_soc_codec *codec)
{
	ssm2602_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_ssm2602 = {
	.probe =	ssm260x_probe,
	.remove =	ssm2602_remove,
	.suspend =	ssm2602_suspend,
	.resume =	ssm2602_resume,
	.set_bias_level = ssm2602_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(ssm2602_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = ssm2602_reg,

	.controls = ssm260x_snd_controls,
	.num_controls = ARRAY_SIZE(ssm260x_snd_controls),
	.dapm_widgets = ssm260x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ssm260x_dapm_widgets),
	.dapm_routes = ssm260x_routes,
	.num_dapm_routes = ARRAY_SIZE(ssm260x_routes),
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit ssm2602_spi_probe(struct spi_device *spi)
{
	struct ssm2602_priv *ssm2602;
	int ret;

	ssm2602 = kzalloc(sizeof(struct ssm2602_priv), GFP_KERNEL);
	if (ssm2602 == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, ssm2602);
	ssm2602->control_type = SND_SOC_SPI;
	ssm2602->type = SSM2602;

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_ssm2602, &ssm2602_dai, 1);
	if (ret < 0)
		kfree(ssm2602);
	return ret;
}

static int __devexit ssm2602_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver ssm2602_spi_driver = {
	.driver = {
		.name	= "ssm2602",
		.owner	= THIS_MODULE,
	},
	.probe		= ssm2602_spi_probe,
	.remove		= __devexit_p(ssm2602_spi_remove),
};
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
/*
 * ssm2602 2 wire address is determined by GPIO5
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static int __devinit ssm2602_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct ssm2602_priv *ssm2602;
	int ret;

	ssm2602 = kzalloc(sizeof(struct ssm2602_priv), GFP_KERNEL);
	if (ssm2602 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ssm2602);
	ssm2602->control_type = SND_SOC_I2C;
	ssm2602->type = id->driver_data;

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ssm2602, &ssm2602_dai, 1);
	if (ret < 0)
		kfree(ssm2602);
	return ret;
}

static int __devexit ssm2602_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id ssm2602_i2c_id[] = {
	{ "ssm2602", SSM2602 },
	{ "ssm2603", SSM2602 },
	{ "ssm2604", SSM2604 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssm2602_i2c_id);

/* corgi i2c codec control layer */
static struct i2c_driver ssm2602_i2c_driver = {
	.driver = {
		.name = "ssm2602",
		.owner = THIS_MODULE,
	},
	.probe = ssm2602_i2c_probe,
	.remove = __devexit_p(ssm2602_i2c_remove),
	.id_table = ssm2602_i2c_id,
};
#endif


static int __init ssm2602_modinit(void)
{
	int ret = 0;

#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&ssm2602_spi_driver);
	if (ret)
		return ret;
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&ssm2602_i2c_driver);
	if (ret)
		return ret;
#endif

	return ret;
}
module_init(ssm2602_modinit);

static void __exit ssm2602_exit(void)
{
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&ssm2602_spi_driver);
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&ssm2602_i2c_driver);
#endif
}
module_exit(ssm2602_exit);

MODULE_DESCRIPTION("ASoC SSM2602/SSM2603/SSM2604 driver");
MODULE_AUTHOR("Cliff Cai");
MODULE_LICENSE("GPL");
