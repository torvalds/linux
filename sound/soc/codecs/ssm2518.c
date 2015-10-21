/*
 * SSM2518 amplifier audio driver
 *
 * Copyright 2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/ssm2518.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "ssm2518.h"

#define SSM2518_REG_POWER1		0x00
#define SSM2518_REG_CLOCK		0x01
#define SSM2518_REG_SAI_CTRL1		0x02
#define SSM2518_REG_SAI_CTRL2		0x03
#define SSM2518_REG_CHAN_MAP		0x04
#define SSM2518_REG_LEFT_VOL		0x05
#define SSM2518_REG_RIGHT_VOL		0x06
#define SSM2518_REG_MUTE_CTRL		0x07
#define SSM2518_REG_FAULT_CTRL		0x08
#define SSM2518_REG_POWER2		0x09
#define SSM2518_REG_DRC_1		0x0a
#define SSM2518_REG_DRC_2		0x0b
#define SSM2518_REG_DRC_3		0x0c
#define SSM2518_REG_DRC_4		0x0d
#define SSM2518_REG_DRC_5		0x0e
#define SSM2518_REG_DRC_6		0x0f
#define SSM2518_REG_DRC_7		0x10
#define SSM2518_REG_DRC_8		0x11
#define SSM2518_REG_DRC_9		0x12

#define SSM2518_POWER1_RESET			BIT(7)
#define SSM2518_POWER1_NO_BCLK			BIT(5)
#define SSM2518_POWER1_MCS_MASK			(0xf << 1)
#define SSM2518_POWER1_MCS_64FS			(0x0 << 1)
#define SSM2518_POWER1_MCS_128FS		(0x1 << 1)
#define SSM2518_POWER1_MCS_256FS		(0x2 << 1)
#define SSM2518_POWER1_MCS_384FS		(0x3 << 1)
#define SSM2518_POWER1_MCS_512FS		(0x4 << 1)
#define SSM2518_POWER1_MCS_768FS		(0x5 << 1)
#define SSM2518_POWER1_MCS_100FS		(0x6 << 1)
#define SSM2518_POWER1_MCS_200FS		(0x7 << 1)
#define SSM2518_POWER1_MCS_400FS		(0x8 << 1)
#define SSM2518_POWER1_SPWDN			BIT(0)

#define SSM2518_CLOCK_ASR			BIT(0)

#define SSM2518_SAI_CTRL1_FMT_MASK		(0x3 << 5)
#define SSM2518_SAI_CTRL1_FMT_I2S		(0x0 << 5)
#define SSM2518_SAI_CTRL1_FMT_LJ		(0x1 << 5)
#define SSM2518_SAI_CTRL1_FMT_RJ_24BIT		(0x2 << 5)
#define SSM2518_SAI_CTRL1_FMT_RJ_16BIT		(0x3 << 5)

#define SSM2518_SAI_CTRL1_SAI_MASK		(0x7 << 2)
#define SSM2518_SAI_CTRL1_SAI_I2S		(0x0 << 2)
#define SSM2518_SAI_CTRL1_SAI_TDM_2		(0x1 << 2)
#define SSM2518_SAI_CTRL1_SAI_TDM_4		(0x2 << 2)
#define SSM2518_SAI_CTRL1_SAI_TDM_8		(0x3 << 2)
#define SSM2518_SAI_CTRL1_SAI_TDM_16		(0x4 << 2)
#define SSM2518_SAI_CTRL1_SAI_MONO		(0x5 << 2)

#define SSM2518_SAI_CTRL1_FS_MASK		(0x3)
#define SSM2518_SAI_CTRL1_FS_8000_12000		(0x0)
#define SSM2518_SAI_CTRL1_FS_16000_24000	(0x1)
#define SSM2518_SAI_CTRL1_FS_32000_48000	(0x2)
#define SSM2518_SAI_CTRL1_FS_64000_96000	(0x3)

#define SSM2518_SAI_CTRL2_BCLK_INTERAL		BIT(7)
#define SSM2518_SAI_CTRL2_LRCLK_PULSE		BIT(6)
#define SSM2518_SAI_CTRL2_LRCLK_INVERT		BIT(5)
#define SSM2518_SAI_CTRL2_MSB			BIT(4)
#define SSM2518_SAI_CTRL2_SLOT_WIDTH_MASK	(0x3 << 2)
#define SSM2518_SAI_CTRL2_SLOT_WIDTH_32		(0x0 << 2)
#define SSM2518_SAI_CTRL2_SLOT_WIDTH_24		(0x1 << 2)
#define SSM2518_SAI_CTRL2_SLOT_WIDTH_16		(0x2 << 2)
#define SSM2518_SAI_CTRL2_BCLK_INVERT		BIT(1)

#define SSM2518_CHAN_MAP_RIGHT_SLOT_OFFSET	4
#define SSM2518_CHAN_MAP_RIGHT_SLOT_MASK	0xf0
#define SSM2518_CHAN_MAP_LEFT_SLOT_OFFSET	0
#define SSM2518_CHAN_MAP_LEFT_SLOT_MASK		0x0f

#define SSM2518_MUTE_CTRL_ANA_GAIN		BIT(5)
#define SSM2518_MUTE_CTRL_MUTE_MASTER		BIT(0)

#define SSM2518_POWER2_APWDN			BIT(0)

#define SSM2518_DAC_MUTE			BIT(6)
#define SSM2518_DAC_FS_MASK			0x07
#define SSM2518_DAC_FS_8000			0x00
#define SSM2518_DAC_FS_16000			0x01
#define SSM2518_DAC_FS_32000			0x02
#define SSM2518_DAC_FS_64000			0x03
#define SSM2518_DAC_FS_128000			0x04

struct ssm2518 {
	struct regmap *regmap;
	bool right_j;

	unsigned int sysclk;
	const struct snd_pcm_hw_constraint_list *constraints;

	int enable_gpio;
};

static const struct reg_default ssm2518_reg_defaults[] = {
	{ 0x00, 0x05 },
	{ 0x01, 0x00 },
	{ 0x02, 0x02 },
	{ 0x03, 0x00 },
	{ 0x04, 0x10 },
	{ 0x05, 0x40 },
	{ 0x06, 0x40 },
	{ 0x07, 0x81 },
	{ 0x08, 0x0c },
	{ 0x09, 0x99 },
	{ 0x0a, 0x7c },
	{ 0x0b, 0x5b },
	{ 0x0c, 0x57 },
	{ 0x0d, 0x89 },
	{ 0x0e, 0x8c },
	{ 0x0f, 0x77 },
	{ 0x10, 0x26 },
	{ 0x11, 0x1c },
	{ 0x12, 0x97 },
};

static const DECLARE_TLV_DB_MINMAX_MUTE(ssm2518_vol_tlv, -7125, 2400);
static const DECLARE_TLV_DB_SCALE(ssm2518_compressor_tlv, -3400, 200, 0);
static const DECLARE_TLV_DB_SCALE(ssm2518_expander_tlv, -8100, 300, 0);
static const DECLARE_TLV_DB_SCALE(ssm2518_noise_gate_tlv, -9600, 300, 0);
static const DECLARE_TLV_DB_SCALE(ssm2518_post_drc_tlv, -2400, 300, 0);

static const DECLARE_TLV_DB_RANGE(ssm2518_limiter_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-2200, 200, 0),
	7, 15, TLV_DB_SCALE_ITEM(-800, 100, 0),
);

static const char * const ssm2518_drc_peak_detector_attack_time_text[] = {
	"0 ms", "0.1 ms", "0.19 ms", "0.37 ms", "0.75 ms", "1.5 ms", "3 ms",
	"6 ms", "12 ms", "24 ms", "48 ms", "96 ms", "192 ms", "384 ms",
	"768 ms", "1536 ms",
};

static const char * const ssm2518_drc_peak_detector_release_time_text[] = {
	"0 ms", "1.5 ms", "3 ms", "6 ms", "12 ms", "24 ms", "48 ms", "96 ms",
	"192 ms", "384 ms", "768 ms", "1536 ms", "3072 ms", "6144 ms",
	"12288 ms", "24576 ms"
};

static const char * const ssm2518_drc_hold_time_text[] = {
	"0 ms", "0.67 ms", "1.33 ms", "2.67 ms", "5.33 ms", "10.66 ms",
	"21.32 ms", "42.64 ms", "85.28 ms", "170.56 ms", "341.12 ms",
	"682.24 ms", "1364 ms",
};

static SOC_ENUM_SINGLE_DECL(ssm2518_drc_peak_detector_attack_time_enum,
	SSM2518_REG_DRC_2, 4, ssm2518_drc_peak_detector_attack_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_peak_detector_release_time_enum,
	SSM2518_REG_DRC_2, 0, ssm2518_drc_peak_detector_release_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_attack_time_enum,
	SSM2518_REG_DRC_6, 4, ssm2518_drc_peak_detector_attack_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_decay_time_enum,
	SSM2518_REG_DRC_6, 0, ssm2518_drc_peak_detector_release_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_hold_time_enum,
	SSM2518_REG_DRC_7, 4, ssm2518_drc_hold_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_noise_gate_hold_time_enum,
	SSM2518_REG_DRC_7, 0, ssm2518_drc_hold_time_text);
static SOC_ENUM_SINGLE_DECL(ssm2518_drc_rms_averaging_time_enum,
	SSM2518_REG_DRC_9, 0, ssm2518_drc_peak_detector_release_time_text);

static const struct snd_kcontrol_new ssm2518_snd_controls[] = {
	SOC_SINGLE("Playback De-emphasis Switch", SSM2518_REG_MUTE_CTRL,
			4, 1, 0),
	SOC_DOUBLE_R_TLV("Master Playback Volume", SSM2518_REG_LEFT_VOL,
			SSM2518_REG_RIGHT_VOL, 0, 0xff, 1, ssm2518_vol_tlv),
	SOC_DOUBLE("Master Playback Switch", SSM2518_REG_MUTE_CTRL, 2, 1, 1, 1),

	SOC_SINGLE("Amp Low Power Mode Switch", SSM2518_REG_POWER2, 4, 1, 0),
	SOC_SINGLE("DAC Low Power Mode Switch", SSM2518_REG_POWER2, 3, 1, 0),

	SOC_SINGLE("DRC Limiter Switch", SSM2518_REG_DRC_1, 5, 1, 0),
	SOC_SINGLE("DRC Compressor Switch", SSM2518_REG_DRC_1, 4, 1, 0),
	SOC_SINGLE("DRC Expander Switch", SSM2518_REG_DRC_1, 3, 1, 0),
	SOC_SINGLE("DRC Noise Gate Switch", SSM2518_REG_DRC_1, 2, 1, 0),
	SOC_DOUBLE("DRC Switch", SSM2518_REG_DRC_1, 0, 1, 1, 0),

	SOC_SINGLE_TLV("DRC Limiter Threshold Volume",
			SSM2518_REG_DRC_3, 4, 15, 1, ssm2518_limiter_tlv),
	SOC_SINGLE_TLV("DRC Compressor Lower Threshold Volume",
			SSM2518_REG_DRC_3, 0, 15, 1, ssm2518_compressor_tlv),
	SOC_SINGLE_TLV("DRC Expander Upper Threshold Volume", SSM2518_REG_DRC_4,
			4, 15, 1, ssm2518_expander_tlv),
	SOC_SINGLE_TLV("DRC Noise Gate Threshold Volume",
			SSM2518_REG_DRC_4, 0, 15, 1, ssm2518_noise_gate_tlv),
	SOC_SINGLE_TLV("DRC Upper Output Threshold Volume",
			SSM2518_REG_DRC_5, 4, 15, 1, ssm2518_limiter_tlv),
	SOC_SINGLE_TLV("DRC Lower Output Threshold Volume",
			SSM2518_REG_DRC_5, 0, 15, 1, ssm2518_noise_gate_tlv),
	SOC_SINGLE_TLV("DRC Post Volume", SSM2518_REG_DRC_8,
			2, 15, 1, ssm2518_post_drc_tlv),

	SOC_ENUM("DRC Peak Detector Attack Time",
		ssm2518_drc_peak_detector_attack_time_enum),
	SOC_ENUM("DRC Peak Detector Release Time",
		ssm2518_drc_peak_detector_release_time_enum),
	SOC_ENUM("DRC Attack Time", ssm2518_drc_attack_time_enum),
	SOC_ENUM("DRC Decay Time", ssm2518_drc_decay_time_enum),
	SOC_ENUM("DRC Hold Time", ssm2518_drc_hold_time_enum),
	SOC_ENUM("DRC Noise Gate Hold Time",
		ssm2518_drc_noise_gate_hold_time_enum),
	SOC_ENUM("DRC RMS Averaging Time", ssm2518_drc_rms_averaging_time_enum),
};

static const struct snd_soc_dapm_widget ssm2518_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DACL", "HiFi Playback", SSM2518_REG_POWER2, 1, 1),
	SND_SOC_DAPM_DAC("DACR", "HiFi Playback", SSM2518_REG_POWER2, 2, 1),

	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route ssm2518_routes[] = {
	{ "OUTL", NULL, "DACL" },
	{ "OUTR", NULL, "DACR" },
};

struct ssm2518_mcs_lut {
	unsigned int rate;
	const unsigned int *sysclks;
};

static const unsigned int ssm2518_sysclks_2048000[] = {
	2048000, 4096000, 8192000, 12288000, 16384000, 24576000,
	3200000, 6400000, 12800000, 0
};

static const unsigned int ssm2518_sysclks_2822000[] = {
	2822000, 5644800, 11289600, 16934400, 22579200, 33868800,
	4410000, 8820000, 17640000, 0
};

static const unsigned int ssm2518_sysclks_3072000[] = {
	3072000, 6144000, 12288000, 16384000, 24576000, 38864000,
	4800000, 9600000, 19200000, 0
};

static const struct ssm2518_mcs_lut ssm2518_mcs_lut[] = {
	{ 8000,  ssm2518_sysclks_2048000, },
	{ 11025, ssm2518_sysclks_2822000, },
	{ 12000, ssm2518_sysclks_3072000, },
	{ 16000, ssm2518_sysclks_2048000, },
	{ 24000, ssm2518_sysclks_3072000, },
	{ 22050, ssm2518_sysclks_2822000, },
	{ 32000, ssm2518_sysclks_2048000, },
	{ 44100, ssm2518_sysclks_2822000, },
	{ 48000, ssm2518_sysclks_3072000, },
	{ 96000, ssm2518_sysclks_3072000, },
};

static const unsigned int ssm2518_rates_2048000[] = {
	8000, 16000, 32000,
};

static const struct snd_pcm_hw_constraint_list ssm2518_constraints_2048000 = {
	.list = ssm2518_rates_2048000,
	.count = ARRAY_SIZE(ssm2518_rates_2048000),
};

static const unsigned int ssm2518_rates_2822000[] = {
	11025, 22050, 44100,
};

static const struct snd_pcm_hw_constraint_list ssm2518_constraints_2822000 = {
	.list = ssm2518_rates_2822000,
	.count = ARRAY_SIZE(ssm2518_rates_2822000),
};

static const unsigned int ssm2518_rates_3072000[] = {
	12000, 24000, 48000, 96000,
};

static const struct snd_pcm_hw_constraint_list ssm2518_constraints_3072000 = {
	.list = ssm2518_rates_3072000,
	.count = ARRAY_SIZE(ssm2518_rates_3072000),
};

static const unsigned int ssm2518_rates_12288000[] = {
	8000, 12000, 16000, 24000, 32000, 48000, 96000,
};

static const struct snd_pcm_hw_constraint_list ssm2518_constraints_12288000 = {
	.list = ssm2518_rates_12288000,
	.count = ARRAY_SIZE(ssm2518_rates_12288000),
};

static unsigned int ssm2518_lookup_mcs(struct ssm2518 *ssm2518,
	unsigned int rate)
{
	const unsigned int *sysclks = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ssm2518_mcs_lut); i++) {
		if (ssm2518_mcs_lut[i].rate == rate) {
			sysclks = ssm2518_mcs_lut[i].sysclks;
			break;
		}
	}

	if (!sysclks)
		return -EINVAL;

	for (i = 0; sysclks[i]; i++) {
		if (sysclks[i] == ssm2518->sysclk)
			return i;
	}

	return -EINVAL;
}

static int ssm2518_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(codec);
	unsigned int rate = params_rate(params);
	unsigned int ctrl1, ctrl1_mask;
	int mcs;
	int ret;

	mcs = ssm2518_lookup_mcs(ssm2518, rate);
	if (mcs < 0)
		return mcs;

	ctrl1_mask = SSM2518_SAI_CTRL1_FS_MASK;

	if (rate >= 8000 && rate <= 12000)
		ctrl1 = SSM2518_SAI_CTRL1_FS_8000_12000;
	else if (rate >= 16000 && rate <= 24000)
		ctrl1 = SSM2518_SAI_CTRL1_FS_16000_24000;
	else if (rate >= 32000 && rate <= 48000)
		ctrl1 = SSM2518_SAI_CTRL1_FS_32000_48000;
	else if (rate >= 64000 && rate <= 96000)
		ctrl1 = SSM2518_SAI_CTRL1_FS_64000_96000;
	else
		return -EINVAL;

	if (ssm2518->right_j) {
		switch (params_width(params)) {
		case 16:
			ctrl1 |= SSM2518_SAI_CTRL1_FMT_RJ_16BIT;
			break;
		case 24:
			ctrl1 |= SSM2518_SAI_CTRL1_FMT_RJ_24BIT;
			break;
		default:
			return -EINVAL;
		}
		ctrl1_mask |= SSM2518_SAI_CTRL1_FMT_MASK;
	}

	/* Disable auto samplerate detection */
	ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_CLOCK,
				SSM2518_CLOCK_ASR, SSM2518_CLOCK_ASR);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_SAI_CTRL1,
				ctrl1_mask, ctrl1);
	if (ret < 0)
		return ret;

	return regmap_update_bits(ssm2518->regmap, SSM2518_REG_POWER1,
				SSM2518_POWER1_MCS_MASK, mcs << 1);
}

static int ssm2518_mute(struct snd_soc_dai *dai, int mute)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(dai->codec);
	unsigned int val;

	if (mute)
		val = SSM2518_MUTE_CTRL_MUTE_MASTER;
	else
		val = 0;

	return regmap_update_bits(ssm2518->regmap, SSM2518_REG_MUTE_CTRL,
			SSM2518_MUTE_CTRL_MUTE_MASTER, val);
}

static int ssm2518_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(dai->codec);
	unsigned int ctrl1 = 0, ctrl2 = 0;
	bool invert_fclk;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		invert_fclk = false;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ctrl2 |= SSM2518_SAI_CTRL2_BCLK_INVERT;
		invert_fclk = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		invert_fclk = true;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		ctrl2 |= SSM2518_SAI_CTRL2_BCLK_INVERT;
		invert_fclk = true;
		break;
	default:
		return -EINVAL;
	}

	ssm2518->right_j = false;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl1 |= SSM2518_SAI_CTRL1_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl1 |= SSM2518_SAI_CTRL1_FMT_LJ;
		invert_fclk = !invert_fclk;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl1 |= SSM2518_SAI_CTRL1_FMT_RJ_24BIT;
		ssm2518->right_j = true;
		invert_fclk = !invert_fclk;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ctrl2 |= SSM2518_SAI_CTRL2_LRCLK_PULSE;
		ctrl1 |= SSM2518_SAI_CTRL1_FMT_I2S;
		invert_fclk = false;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ctrl2 |= SSM2518_SAI_CTRL2_LRCLK_PULSE;
		ctrl1 |= SSM2518_SAI_CTRL1_FMT_LJ;
		invert_fclk = false;
		break;
	default:
		return -EINVAL;
	}

	if (invert_fclk)
		ctrl2 |= SSM2518_SAI_CTRL2_LRCLK_INVERT;

	ret = regmap_write(ssm2518->regmap, SSM2518_REG_SAI_CTRL1, ctrl1);
	if (ret)
		return ret;

	return regmap_write(ssm2518->regmap, SSM2518_REG_SAI_CTRL2, ctrl2);
}

static int ssm2518_set_power(struct ssm2518 *ssm2518, bool enable)
{
	int ret = 0;

	if (!enable) {
		ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_POWER1,
			SSM2518_POWER1_SPWDN, SSM2518_POWER1_SPWDN);
		regcache_mark_dirty(ssm2518->regmap);
	}

	if (gpio_is_valid(ssm2518->enable_gpio))
		gpio_set_value(ssm2518->enable_gpio, enable);

	regcache_cache_only(ssm2518->regmap, !enable);

	if (enable) {
		ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_POWER1,
			SSM2518_POWER1_SPWDN | SSM2518_POWER1_RESET, 0x00);
		regcache_sync(ssm2518->regmap);
	}

	return ret;
}

static int ssm2518_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF)
			ret = ssm2518_set_power(ssm2518, true);
		break;
	case SND_SOC_BIAS_OFF:
		ret = ssm2518_set_power(ssm2518, false);
		break;
	}

	return ret;
}

static int ssm2518_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
	unsigned int rx_mask, int slots, int width)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(dai->codec);
	unsigned int ctrl1, ctrl2;
	int left_slot, right_slot;
	int ret;

	if (slots == 0)
		return regmap_update_bits(ssm2518->regmap,
			SSM2518_REG_SAI_CTRL1, SSM2518_SAI_CTRL1_SAI_MASK,
			SSM2518_SAI_CTRL1_SAI_I2S);

	if (tx_mask == 0 || rx_mask != 0)
		return -EINVAL;

	if (slots == 1) {
		if (tx_mask != 1)
			return -EINVAL;
		left_slot = 0;
		right_slot = 0;
	} else {
		/* We assume the left channel < right channel */
		left_slot = __ffs(tx_mask);
		tx_mask &= ~(1 << left_slot);
		if (tx_mask == 0) {
			right_slot = left_slot;
		} else {
			right_slot = __ffs(tx_mask);
			tx_mask &= ~(1 << right_slot);
		}
	}

	if (tx_mask != 0 || left_slot >= slots || right_slot >= slots)
		return -EINVAL;

	switch (width) {
	case 16:
		ctrl2 = SSM2518_SAI_CTRL2_SLOT_WIDTH_16;
		break;
	case 24:
		ctrl2 = SSM2518_SAI_CTRL2_SLOT_WIDTH_24;
		break;
	case 32:
		ctrl2 = SSM2518_SAI_CTRL2_SLOT_WIDTH_32;
		break;
	default:
		return -EINVAL;
	}

	switch (slots) {
	case 1:
		ctrl1 = SSM2518_SAI_CTRL1_SAI_MONO;
		break;
	case 2:
		ctrl1 = SSM2518_SAI_CTRL1_SAI_TDM_2;
		break;
	case 4:
		ctrl1 = SSM2518_SAI_CTRL1_SAI_TDM_4;
		break;
	case 8:
		ctrl1 = SSM2518_SAI_CTRL1_SAI_TDM_8;
		break;
	case 16:
		ctrl1 = SSM2518_SAI_CTRL1_SAI_TDM_16;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_write(ssm2518->regmap, SSM2518_REG_CHAN_MAP,
		(left_slot << SSM2518_CHAN_MAP_LEFT_SLOT_OFFSET) |
		(right_slot << SSM2518_CHAN_MAP_RIGHT_SLOT_OFFSET));
	if (ret)
		return ret;

	ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_SAI_CTRL1,
		SSM2518_SAI_CTRL1_SAI_MASK, ctrl1);
	if (ret)
		return ret;

	return regmap_update_bits(ssm2518->regmap, SSM2518_REG_SAI_CTRL2,
		SSM2518_SAI_CTRL2_SLOT_WIDTH_MASK, ctrl2);
}

static int ssm2518_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(dai->codec);

	if (ssm2518->constraints)
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, ssm2518->constraints);

	return 0;
}

#define SSM2518_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32)

static const struct snd_soc_dai_ops ssm2518_dai_ops = {
	.startup = ssm2518_startup,
	.hw_params	= ssm2518_hw_params,
	.digital_mute	= ssm2518_mute,
	.set_fmt	= ssm2518_set_dai_fmt,
	.set_tdm_slot	= ssm2518_set_tdm_slot,
};

static struct snd_soc_dai_driver ssm2518_dai = {
	.name = "ssm2518-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SSM2518_FORMATS,
	},
	.ops = &ssm2518_dai_ops,
};

static int ssm2518_set_sysclk(struct snd_soc_codec *codec, int clk_id,
	int source, unsigned int freq, int dir)
{
	struct ssm2518 *ssm2518 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;

	if (clk_id != SSM2518_SYSCLK)
		return -EINVAL;

	switch (source) {
	case SSM2518_SYSCLK_SRC_MCLK:
		val = 0;
		break;
	case SSM2518_SYSCLK_SRC_BCLK:
		/* In this case the bitclock is used as the system clock, and
		 * the bitclock signal needs to be connected to the MCLK pin and
		 * the BCLK pin is left unconnected */
		val = SSM2518_POWER1_NO_BCLK;
		break;
	default:
		return -EINVAL;
	}

	switch (freq) {
	case 0:
		ssm2518->constraints = NULL;
		break;
	case 2048000:
	case 4096000:
	case 8192000:
	case 3200000:
	case 6400000:
	case 12800000:
		ssm2518->constraints = &ssm2518_constraints_2048000;
		break;
	case 2822000:
	case 5644800:
	case 11289600:
	case 16934400:
	case 22579200:
	case 33868800:
	case 4410000:
	case 8820000:
	case 17640000:
		ssm2518->constraints = &ssm2518_constraints_2822000;
		break;
	case 3072000:
	case 6144000:
	case 38864000:
	case 4800000:
	case 9600000:
	case 19200000:
		ssm2518->constraints = &ssm2518_constraints_3072000;
		break;
	case 12288000:
	case 16384000:
	case 24576000:
		ssm2518->constraints = &ssm2518_constraints_12288000;
		break;
	default:
		return -EINVAL;
	}

	ssm2518->sysclk = freq;

	return regmap_update_bits(ssm2518->regmap, SSM2518_REG_POWER1,
			SSM2518_POWER1_NO_BCLK, val);
}

static struct snd_soc_codec_driver ssm2518_codec_driver = {
	.set_bias_level = ssm2518_set_bias_level,
	.set_sysclk = ssm2518_set_sysclk,
	.idle_bias_off = true,

	.controls = ssm2518_snd_controls,
	.num_controls = ARRAY_SIZE(ssm2518_snd_controls),
	.dapm_widgets = ssm2518_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ssm2518_dapm_widgets),
	.dapm_routes = ssm2518_routes,
	.num_dapm_routes = ARRAY_SIZE(ssm2518_routes),
};

static bool ssm2518_register_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static const struct regmap_config ssm2518_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,

	.max_register = SSM2518_REG_DRC_9,
	.volatile_reg = ssm2518_register_volatile,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = ssm2518_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ssm2518_reg_defaults),
};

static int ssm2518_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct ssm2518_platform_data *pdata = i2c->dev.platform_data;
	struct ssm2518 *ssm2518;
	int ret;

	ssm2518 = devm_kzalloc(&i2c->dev, sizeof(*ssm2518), GFP_KERNEL);
	if (ssm2518 == NULL)
		return -ENOMEM;

	if (pdata) {
		ssm2518->enable_gpio = pdata->enable_gpio;
	} else if (i2c->dev.of_node) {
		ssm2518->enable_gpio = of_get_gpio(i2c->dev.of_node, 0);
		if (ssm2518->enable_gpio < 0 && ssm2518->enable_gpio != -ENOENT)
			return ssm2518->enable_gpio;
	} else {
		ssm2518->enable_gpio = -1;
	}

	if (gpio_is_valid(ssm2518->enable_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, ssm2518->enable_gpio,
				GPIOF_OUT_INIT_HIGH, "SSM2518 nSD");
		if (ret)
			return ret;
	}

	i2c_set_clientdata(i2c, ssm2518);

	ssm2518->regmap = devm_regmap_init_i2c(i2c, &ssm2518_regmap_config);
	if (IS_ERR(ssm2518->regmap))
		return PTR_ERR(ssm2518->regmap);

	/*
	 * The reset bit is obviously volatile, but we need to be able to cache
	 * the other bits in the register, so we can't just mark the whole
	 * register as volatile. Since this is the only place where we'll ever
	 * touch the reset bit just bypass the cache for this operation.
	 */
	regcache_cache_bypass(ssm2518->regmap, true);
	ret = regmap_write(ssm2518->regmap, SSM2518_REG_POWER1,
			SSM2518_POWER1_RESET);
	regcache_cache_bypass(ssm2518->regmap, false);
	if (ret)
		return ret;

	ret = regmap_update_bits(ssm2518->regmap, SSM2518_REG_POWER2,
				SSM2518_POWER2_APWDN, 0x00);
	if (ret)
		return ret;

	ret = ssm2518_set_power(ssm2518, false);
	if (ret)
		return ret;

	return snd_soc_register_codec(&i2c->dev, &ssm2518_codec_driver,
			&ssm2518_dai, 1);
}

static int ssm2518_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ssm2518_dt_ids[] = {
	{ .compatible = "adi,ssm2518", },
	{ }
};
MODULE_DEVICE_TABLE(of, ssm2518_dt_ids);
#endif

static const struct i2c_device_id ssm2518_i2c_ids[] = {
	{ "ssm2518", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssm2518_i2c_ids);

static struct i2c_driver ssm2518_driver = {
	.driver = {
		.name = "ssm2518",
		.of_match_table = of_match_ptr(ssm2518_dt_ids),
	},
	.probe = ssm2518_i2c_probe,
	.remove = ssm2518_i2c_remove,
	.id_table = ssm2518_i2c_ids,
};
module_i2c_driver(ssm2518_driver);

MODULE_DESCRIPTION("ASoC SSM2518 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
