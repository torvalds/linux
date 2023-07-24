// SPDX-License-Identifier: GPL-2.0-or-later
//
// File:         sound/soc/codecs/ssm2602.c
// Author:       Cliff Cai <Cliff.Cai@analog.com>
//
// Created:      Tue June 06 2008
// Description:  Driver for ssm2602 sound chip
//
// Modified:
//               Copyright 2008 Analog Devices Inc.
//
// Bugs:         Enter bugs at http://blackfin.uclinux.org/

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "ssm2602.h"

/* codec private data */
struct ssm2602_priv {
	unsigned int sysclk;
	const struct snd_pcm_hw_constraint_list *sysclk_constraints;

	struct regmap *regmap;

	enum ssm2602_type type;
	unsigned int clk_out_pwr;
};

/*
 * ssm2602 register cache
 * We can't read the ssm2602 register space when we are
 * using 2 wire for device control, so we cache them instead.
 * There is no point in caching the reset register
 */
static const struct reg_default ssm2602_reg[SSM2602_CACHEREGNUM] = {
	{ .reg = 0x00, .def = 0x0097 },
	{ .reg = 0x01, .def = 0x0097 },
	{ .reg = 0x02, .def = 0x0079 },
	{ .reg = 0x03, .def = 0x0079 },
	{ .reg = 0x04, .def = 0x000a },
	{ .reg = 0x05, .def = 0x0008 },
	{ .reg = 0x06, .def = 0x009f },
	{ .reg = 0x07, .def = 0x000a },
	{ .reg = 0x08, .def = 0x0000 },
	{ .reg = 0x09, .def = 0x0000 }
};

/*
 * ssm2602 register patch
 * Workaround for playback distortions after power up: activates digital
 * core, and then powers on output, DAC, and whole chip at the same time
 */

static const struct reg_sequence ssm2602_patch[] = {
	{ SSM2602_ACTIVE, 0x01 },
	{ SSM2602_PWR,    0x07 },
	{ SSM2602_RESET,  0x00 },
};


/*Appending several "None"s just for OSS mixer use*/
static const char *ssm2602_input_select[] = {
	"Line", "Mic",
};

static const char *ssm2602_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};

static const struct soc_enum ssm2602_enum[] = {
	SOC_ENUM_SINGLE(SSM2602_APANA, 2, ARRAY_SIZE(ssm2602_input_select),
			ssm2602_input_select),
	SOC_ENUM_SINGLE(SSM2602_APDIGI, 1, ARRAY_SIZE(ssm2602_deemph),
			ssm2602_deemph),
};

static const DECLARE_TLV_DB_RANGE(ssm260x_outmix_tlv,
	0, 47, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	48, 127, TLV_DB_SCALE_ITEM(-7400, 100, 0)
);

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
};

/* Output Mixer */
static const struct snd_kcontrol_new ssm260x_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", SSM2602_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", SSM2602_APANA, 4, 1, 0),
SOC_DAPM_SINGLE("Mic Sidetone Switch", SSM2602_APANA, 5, 1, 0),
};

static const struct snd_kcontrol_new mic_ctl =
	SOC_DAPM_SINGLE("Switch", SSM2602_APANA, 1, 1, 1);

/* Input mux */
static const struct snd_kcontrol_new ssm2602_input_mux_controls =
SOC_DAPM_ENUM("Input Select", ssm2602_enum[0]);

static int ssm2602_mic_switch_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	/*
	 * According to the ssm2603 data sheet (control register sequencing),
	 * the digital core should be activated only after all necessary bits
	 * in the power register are enabled, and a delay determined by the
	 * decoupling capacitor on the VMID pin has passed. If the digital core
	 * is activated too early, or even before the ADC is powered up, audible
	 * artifacts appear at the beginning and end of the recorded signal.
	 *
	 * In practice, audible artifacts disappear well over 500 ms.
	 */
	msleep(500);

	return 0;
}

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

SND_SOC_DAPM_SWITCH_E("Mic Switch", SSM2602_APANA, 1, 1, &mic_ctl,
		ssm2602_mic_switch_event, SND_SOC_DAPM_PRE_PMU),

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
	{"Input Mux", "Mic", "Mic Switch"},
	{"ADC", NULL, "Input Mux"},

	{"Mic Switch", NULL, "Mic Bias"},

	{"Mic Bias", NULL, "MICIN"},
};

static const struct snd_soc_dapm_route ssm2604_routes[] = {
	{"ADC", NULL, "Line Input"},
};

static const unsigned int ssm2602_rates_12288000[] = {
	8000, 16000, 32000, 48000, 96000,
};

static const struct snd_pcm_hw_constraint_list ssm2602_constraints_12288000 = {
	.list = ssm2602_rates_12288000,
	.count = ARRAY_SIZE(ssm2602_rates_12288000),
};

static const unsigned int ssm2602_rates_11289600[] = {
	8000, 11025, 22050, 44100, 88200,
};

static const struct snd_pcm_hw_constraint_list ssm2602_constraints_11289600 = {
	.list = ssm2602_rates_11289600,
	.count = ARRAY_SIZE(ssm2602_rates_11289600),
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

	/* 16k */
	{12288000, 16000, SSM2602_COEFF_SRATE(0x5, 0x0, 0x0)},
	{18432000, 16000, SSM2602_COEFF_SRATE(0x5, 0x1, 0x0)},
	{12000000, 16000, SSM2602_COEFF_SRATE(0xa, 0x0, 0x1)},

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

	/* 11.025k */
	{11289600, 11025, SSM2602_COEFF_SRATE(0xc, 0x0, 0x0)},
	{16934400, 11025, SSM2602_COEFF_SRATE(0xc, 0x1, 0x0)},
	{12000000, 11025, SSM2602_COEFF_SRATE(0xc, 0x1, 0x1)},

	/* 22.05k */
	{11289600, 22050, SSM2602_COEFF_SRATE(0xd, 0x0, 0x0)},
	{16934400, 22050, SSM2602_COEFF_SRATE(0xd, 0x1, 0x0)},
	{12000000, 22050, SSM2602_COEFF_SRATE(0xd, 0x1, 0x1)},

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
	struct snd_soc_component *component = dai->component;
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);
	int srate = ssm2602_get_coeff(ssm2602->sysclk, params_rate(params));
	unsigned int iface;

	if (srate < 0)
		return srate;

	regmap_write(ssm2602->regmap, SSM2602_SRATE, srate);

	/* bit size */
	switch (params_width(params)) {
	case 16:
		iface = 0x0;
		break;
	case 20:
		iface = 0x4;
		break;
	case 24:
		iface = 0x8;
		break;
	case 32:
		iface = 0xc;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(ssm2602->regmap, SSM2602_IFACE,
		IFACE_AUDIO_DATA_LEN, iface);
	return 0;
}

static int ssm2602_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);

	if (ssm2602->sysclk_constraints) {
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   ssm2602->sysclk_constraints);
	}

	return 0;
}

static int ssm2602_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(dai->component);

	if (mute)
		regmap_update_bits(ssm2602->regmap, SSM2602_APDIGI,
				    APDIGI_ENABLE_DAC_MUTE,
				    APDIGI_ENABLE_DAC_MUTE);
	else
		regmap_update_bits(ssm2602->regmap, SSM2602_APDIGI,
				    APDIGI_ENABLE_DAC_MUTE, 0);
	return 0;
}

static int ssm2602_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);

	if (dir == SND_SOC_CLOCK_IN) {
		if (clk_id != SSM2602_SYSCLK)
			return -EINVAL;

		switch (freq) {
		case 12288000:
		case 18432000:
			ssm2602->sysclk_constraints = &ssm2602_constraints_12288000;
			break;
		case 11289600:
		case 16934400:
			ssm2602->sysclk_constraints = &ssm2602_constraints_11289600;
			break;
		case 12000000:
			ssm2602->sysclk_constraints = NULL;
			break;
		default:
			return -EINVAL;
		}
		ssm2602->sysclk = freq;
	} else {
		unsigned int mask;

		switch (clk_id) {
		case SSM2602_CLK_CLKOUT:
			mask = PWR_CLK_OUT_PDN;
			break;
		case SSM2602_CLK_XTO:
			mask = PWR_OSC_PDN;
			break;
		default:
			return -EINVAL;
		}

		if (freq == 0)
			ssm2602->clk_out_pwr |= mask;
		else
			ssm2602->clk_out_pwr &= ~mask;

		regmap_update_bits(ssm2602->regmap, SSM2602_PWR,
			PWR_CLK_OUT_PDN | PWR_OSC_PDN, ssm2602->clk_out_pwr);
	}

	return 0;
}

static int ssm2602_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int iface = 0;

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
	regmap_write(ssm2602->regmap, SSM2602_IFACE, iface);
	return 0;
}

static int ssm2602_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* vref/mid on, osc and clkout on if enabled */
		regmap_update_bits(ssm2602->regmap, SSM2602_PWR,
			PWR_POWER_OFF | PWR_CLK_OUT_PDN | PWR_OSC_PDN,
			ssm2602->clk_out_pwr);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		regmap_update_bits(ssm2602->regmap, SSM2602_PWR,
			PWR_POWER_OFF | PWR_CLK_OUT_PDN | PWR_OSC_PDN,
			PWR_CLK_OUT_PDN | PWR_OSC_PDN);
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off */
		regmap_update_bits(ssm2602->regmap, SSM2602_PWR,
			PWR_POWER_OFF, PWR_POWER_OFF);
		break;

	}
	return 0;
}

#define SSM2602_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
		SNDRV_PCM_RATE_96000)

#define SSM2602_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops ssm2602_dai_ops = {
	.startup	= ssm2602_startup,
	.hw_params	= ssm2602_hw_params,
	.mute_stream	= ssm2602_mute,
	.set_sysclk	= ssm2602_set_dai_sysclk,
	.set_fmt	= ssm2602_set_dai_fmt,
	.no_capture_mute = 1,
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
	.symmetric_rates = 1,
	.symmetric_samplebits = 1,
};

static int ssm2602_resume(struct snd_soc_component *component)
{
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);

	regcache_sync(ssm2602->regmap);

	return 0;
}

static int ssm2602_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);
	int ret;

	regmap_update_bits(ssm2602->regmap, SSM2602_LOUT1V,
			    LOUT1V_LRHP_BOTH, LOUT1V_LRHP_BOTH);
	regmap_update_bits(ssm2602->regmap, SSM2602_ROUT1V,
			    ROUT1V_RLHP_BOTH, ROUT1V_RLHP_BOTH);

	ret = snd_soc_add_component_controls(component, ssm2602_snd_controls,
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

static int ssm2604_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, ssm2604_dapm_widgets,
			ARRAY_SIZE(ssm2604_dapm_widgets));
	if (ret)
		return ret;

	return snd_soc_dapm_add_routes(dapm, ssm2604_routes,
			ARRAY_SIZE(ssm2604_routes));
}

static int ssm260x_component_probe(struct snd_soc_component *component)
{
	struct ssm2602_priv *ssm2602 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = regmap_write(ssm2602->regmap, SSM2602_RESET, 0);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	regmap_register_patch(ssm2602->regmap, ssm2602_patch,
			      ARRAY_SIZE(ssm2602_patch));

	/* set the update bits */
	regmap_update_bits(ssm2602->regmap, SSM2602_LINVOL,
			    LINVOL_LRIN_BOTH, LINVOL_LRIN_BOTH);
	regmap_update_bits(ssm2602->regmap, SSM2602_RINVOL,
			    RINVOL_RLIN_BOTH, RINVOL_RLIN_BOTH);
	/*select Line in as default input*/
	regmap_write(ssm2602->regmap, SSM2602_APANA, APANA_SELECT_DAC |
			APANA_ENABLE_MIC_BOOST);

	switch (ssm2602->type) {
	case SSM2602:
		ret = ssm2602_component_probe(component);
		break;
	case SSM2604:
		ret = ssm2604_component_probe(component);
		break;
	}

	return ret;
}

static const struct snd_soc_component_driver soc_component_dev_ssm2602 = {
	.probe			= ssm260x_component_probe,
	.resume			= ssm2602_resume,
	.set_bias_level		= ssm2602_set_bias_level,
	.controls		= ssm260x_snd_controls,
	.num_controls		= ARRAY_SIZE(ssm260x_snd_controls),
	.dapm_widgets		= ssm260x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ssm260x_dapm_widgets),
	.dapm_routes		= ssm260x_routes,
	.num_dapm_routes	= ARRAY_SIZE(ssm260x_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static bool ssm2602_register_volatile(struct device *dev, unsigned int reg)
{
	return reg == SSM2602_RESET;
}

const struct regmap_config ssm2602_regmap_config = {
	.val_bits = 9,
	.reg_bits = 7,

	.max_register = SSM2602_RESET,
	.volatile_reg = ssm2602_register_volatile,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = ssm2602_reg,
	.num_reg_defaults = ARRAY_SIZE(ssm2602_reg),
};
EXPORT_SYMBOL_GPL(ssm2602_regmap_config);

int ssm2602_probe(struct device *dev, enum ssm2602_type type,
	struct regmap *regmap)
{
	struct ssm2602_priv *ssm2602;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ssm2602 = devm_kzalloc(dev, sizeof(*ssm2602), GFP_KERNEL);
	if (ssm2602 == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, ssm2602);
	ssm2602->type = type;
	ssm2602->regmap = regmap;

	return devm_snd_soc_register_component(dev, &soc_component_dev_ssm2602,
		&ssm2602_dai, 1);
}
EXPORT_SYMBOL_GPL(ssm2602_probe);

MODULE_DESCRIPTION("ASoC SSM2602/SSM2603/SSM2604 driver");
MODULE_AUTHOR("Cliff Cai");
MODULE_LICENSE("GPL");
