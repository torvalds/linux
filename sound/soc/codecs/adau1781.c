/*
 * Driver for ADAU1381/ADAU1781 codec
 *
 * Copyright 2011-2013 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/platform_data/adau17x1.h>

#include "adau17x1.h"
#include "adau1781.h"

#define ADAU1781_DMIC_BEEP_CTRL		0x4008
#define ADAU1781_LEFT_PGA		0x400e
#define ADAU1781_RIGHT_PGA		0x400f
#define ADAU1781_LEFT_PLAYBACK_MIXER	0x401c
#define ADAU1781_RIGHT_PLAYBACK_MIXER	0x401e
#define ADAU1781_MONO_PLAYBACK_MIXER	0x401f
#define ADAU1781_LEFT_LINEOUT		0x4025
#define ADAU1781_RIGHT_LINEOUT		0x4026
#define ADAU1781_SPEAKER		0x4027
#define ADAU1781_BEEP_ZC		0x4028
#define ADAU1781_DEJITTER		0x4032
#define ADAU1781_DIG_PWDN0		0x4080
#define ADAU1781_DIG_PWDN1		0x4081

#define ADAU1781_INPUT_DIFFERNTIAL BIT(3)

#define ADAU1381_FIRMWARE "adau1381.bin"
#define ADAU1781_FIRMWARE "adau1781.bin"

static const struct reg_default adau1781_reg_defaults[] = {
	{ ADAU1781_DMIC_BEEP_CTRL,		0x00 },
	{ ADAU1781_LEFT_PGA,			0xc7 },
	{ ADAU1781_RIGHT_PGA,			0xc7 },
	{ ADAU1781_LEFT_PLAYBACK_MIXER,		0x00 },
	{ ADAU1781_RIGHT_PLAYBACK_MIXER,	0x00 },
	{ ADAU1781_MONO_PLAYBACK_MIXER,		0x00 },
	{ ADAU1781_LEFT_LINEOUT,		0x00 },
	{ ADAU1781_RIGHT_LINEOUT,		0x00 },
	{ ADAU1781_SPEAKER,			0x00 },
	{ ADAU1781_BEEP_ZC,			0x19 },
	{ ADAU1781_DEJITTER,			0x60 },
	{ ADAU1781_DIG_PWDN1,			0x0c },
	{ ADAU1781_DIG_PWDN1,			0x00 },
	{ ADAU17X1_CLOCK_CONTROL,		0x00 },
	{ ADAU17X1_PLL_CONTROL,			0x00 },
	{ ADAU17X1_REC_POWER_MGMT,		0x00 },
	{ ADAU17X1_MICBIAS,			0x04 },
	{ ADAU17X1_SERIAL_PORT0,		0x00 },
	{ ADAU17X1_SERIAL_PORT1,		0x00 },
	{ ADAU17X1_CONVERTER0,			0x00 },
	{ ADAU17X1_CONVERTER1,			0x00 },
	{ ADAU17X1_LEFT_INPUT_DIGITAL_VOL,	0x00 },
	{ ADAU17X1_RIGHT_INPUT_DIGITAL_VOL,	0x00 },
	{ ADAU17X1_ADC_CONTROL,			0x00 },
	{ ADAU17X1_PLAY_POWER_MGMT,		0x00 },
	{ ADAU17X1_DAC_CONTROL0,		0x00 },
	{ ADAU17X1_DAC_CONTROL1,		0x00 },
	{ ADAU17X1_DAC_CONTROL2,		0x00 },
	{ ADAU17X1_SERIAL_PORT_PAD,		0x00 },
	{ ADAU17X1_CONTROL_PORT_PAD0,		0x00 },
	{ ADAU17X1_CONTROL_PORT_PAD1,		0x00 },
	{ ADAU17X1_DSP_SAMPLING_RATE,		0x01 },
	{ ADAU17X1_SERIAL_INPUT_ROUTE,		0x00 },
	{ ADAU17X1_SERIAL_OUTPUT_ROUTE,		0x00 },
	{ ADAU17X1_DSP_ENABLE,			0x00 },
	{ ADAU17X1_DSP_RUN,			0x00 },
	{ ADAU17X1_SERIAL_SAMPLING_RATE,	0x00 },
};

static const DECLARE_TLV_DB_SCALE(adau1781_speaker_tlv, 0, 200, 0);

static const DECLARE_TLV_DB_RANGE(adau1781_pga_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 600, 0),
	2, 3, TLV_DB_SCALE_ITEM(1000, 400, 0),
	4, 4, TLV_DB_SCALE_ITEM(1700, 0, 0),
	5, 7, TLV_DB_SCALE_ITEM(2000, 600, 0)
);

static const DECLARE_TLV_DB_RANGE(adau1781_beep_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 600, 0),
	2, 3, TLV_DB_SCALE_ITEM(1000, 400, 0),
	4, 4, TLV_DB_SCALE_ITEM(-2300, 0, 0),
	5, 7, TLV_DB_SCALE_ITEM(2000, 600, 0)
);

static const DECLARE_TLV_DB_SCALE(adau1781_sidetone_tlv, -1800, 300, 1);

static const char * const adau1781_speaker_bias_select_text[] = {
	"Normal operation", "Power saving", "Enhanced performance",
};

static const char * const adau1781_bias_select_text[] = {
	"Normal operation", "Extreme power saving", "Power saving",
	"Enhanced performance",
};

static SOC_ENUM_SINGLE_DECL(adau1781_adc_bias_enum,
		ADAU17X1_REC_POWER_MGMT, 3, adau1781_bias_select_text);
static SOC_ENUM_SINGLE_DECL(adau1781_speaker_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 6, adau1781_speaker_bias_select_text);
static SOC_ENUM_SINGLE_DECL(adau1781_dac_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 4, adau1781_bias_select_text);
static SOC_ENUM_SINGLE_DECL(adau1781_playback_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 2, adau1781_bias_select_text);
static SOC_ENUM_SINGLE_DECL(adau1781_capture_bias_enum,
		ADAU17X1_REC_POWER_MGMT, 1, adau1781_bias_select_text);

static const struct snd_kcontrol_new adau1781_controls[] = {
	SOC_SINGLE_TLV("Beep Capture Volume", ADAU1781_DMIC_BEEP_CTRL, 0, 7, 0,
		adau1781_beep_tlv),
	SOC_DOUBLE_R_TLV("PGA Capture Volume", ADAU1781_LEFT_PGA,
		ADAU1781_RIGHT_PGA, 5, 7, 0, adau1781_pga_tlv),
	SOC_DOUBLE_R("PGA Capture Switch", ADAU1781_LEFT_PGA,
		ADAU1781_RIGHT_PGA, 1, 1, 0),

	SOC_DOUBLE_R("Lineout Playback Switch", ADAU1781_LEFT_LINEOUT,
		ADAU1781_RIGHT_LINEOUT, 1, 1, 0),
	SOC_SINGLE("Beep ZC Switch", ADAU1781_BEEP_ZC, 0, 1, 0),

	SOC_SINGLE("Mono Playback Switch", ADAU1781_MONO_PLAYBACK_MIXER,
		0, 1, 0),
	SOC_SINGLE_TLV("Mono Playback Volume", ADAU1781_SPEAKER, 6, 3, 0,
		adau1781_speaker_tlv),

	SOC_ENUM("ADC Bias", adau1781_adc_bias_enum),
	SOC_ENUM("DAC Bias", adau1781_dac_bias_enum),
	SOC_ENUM("Capture Bias", adau1781_capture_bias_enum),
	SOC_ENUM("Playback Bias", adau1781_playback_bias_enum),
	SOC_ENUM("Speaker Bias", adau1781_speaker_bias_enum),
};

static const struct snd_kcontrol_new adau1781_beep_mixer_controls[] = {
	SOC_DAPM_SINGLE("Beep Capture Switch", ADAU1781_DMIC_BEEP_CTRL,
		3, 1, 0),
};

static const struct snd_kcontrol_new adau1781_left_mixer_controls[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Switch",
		ADAU1781_LEFT_PLAYBACK_MIXER, 5, 1, 0),
	SOC_DAPM_SINGLE_TLV("Beep Playback Volume",
		ADAU1781_LEFT_PLAYBACK_MIXER, 1, 8, 0, adau1781_sidetone_tlv),
};

static const struct snd_kcontrol_new adau1781_right_mixer_controls[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Switch",
		ADAU1781_RIGHT_PLAYBACK_MIXER, 6, 1, 0),
	SOC_DAPM_SINGLE_TLV("Beep Playback Volume",
		ADAU1781_LEFT_PLAYBACK_MIXER, 1, 8, 0, adau1781_sidetone_tlv),
};

static const struct snd_kcontrol_new adau1781_mono_mixer_controls[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Left Switch",
		ADAU1781_MONO_PLAYBACK_MIXER, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("Right Switch",
		 ADAU1781_MONO_PLAYBACK_MIXER, 6, 1, 0),
	SOC_DAPM_SINGLE_TLV("Beep Playback Volume",
		ADAU1781_MONO_PLAYBACK_MIXER, 2, 8, 0, adau1781_sidetone_tlv),
};

static int adau1781_dejitter_fixup(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct adau *adau = snd_soc_codec_get_drvdata(codec);

	/* After any power changes have been made the dejitter circuit
	 * has to be reinitialized. */
	regmap_write(adau->regmap, ADAU1781_DEJITTER, 0);
	if (!adau->master)
		regmap_write(adau->regmap, ADAU1781_DEJITTER, 5);

	return 0;
}

static const struct snd_soc_dapm_widget adau1781_dapm_widgets[] = {
	SND_SOC_DAPM_PGA("Left PGA", ADAU1781_LEFT_PGA, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right PGA", ADAU1781_RIGHT_PGA, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("Speaker", ADAU1781_SPEAKER, 0, 0, NULL, 0),

	SOC_MIXER_NAMED_CTL_ARRAY("Beep Mixer", ADAU17X1_MICBIAS, 4, 0,
		adau1781_beep_mixer_controls),

	SOC_MIXER_ARRAY("Left Lineout Mixer", SND_SOC_NOPM, 0, 0,
		adau1781_left_mixer_controls),
	SOC_MIXER_ARRAY("Right Lineout Mixer", SND_SOC_NOPM, 0, 0,
		adau1781_right_mixer_controls),
	SOC_MIXER_ARRAY("Mono Mixer", SND_SOC_NOPM, 0, 0,
		adau1781_mono_mixer_controls),

	SND_SOC_DAPM_SUPPLY("Serial Input Routing", ADAU1781_DIG_PWDN0,
		2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Serial Output Routing", ADAU1781_DIG_PWDN0,
		3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Clock Domain Transfer", ADAU1781_DIG_PWDN0,
		5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Serial Ports", ADAU1781_DIG_PWDN0, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Engine", ADAU1781_DIG_PWDN0, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Engine", ADAU1781_DIG_PWDN1, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Digital Mic", ADAU1781_DIG_PWDN1, 1, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Sound Engine", ADAU1781_DIG_PWDN0, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("SYSCLK", 1, ADAU1781_DIG_PWDN0, 1, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Zero Crossing Detector", ADAU1781_DIG_PWDN1, 2, 0,
		NULL, 0),

	SND_SOC_DAPM_POST("Dejitter fixup", adau1781_dejitter_fixup),

	SND_SOC_DAPM_INPUT("BEEP"),

	SND_SOC_DAPM_OUTPUT("AOUTL"),
	SND_SOC_DAPM_OUTPUT("AOUTR"),
	SND_SOC_DAPM_OUTPUT("SP"),
	SND_SOC_DAPM_INPUT("LMIC"),
	SND_SOC_DAPM_INPUT("RMIC"),
};

static const struct snd_soc_dapm_route adau1781_dapm_routes[] = {
	{ "Left Lineout Mixer", NULL, "Left Playback Enable" },
	{ "Right Lineout Mixer", NULL, "Right Playback Enable" },

	{ "Left Lineout Mixer", "Beep Playback Volume", "Beep Mixer" },
	{ "Left Lineout Mixer", "Switch", "Left DAC" },

	{ "Right Lineout Mixer", "Beep Playback Volume", "Beep Mixer" },
	{ "Right Lineout Mixer", "Switch", "Right DAC" },

	{ "Mono Mixer", "Beep Playback Volume", "Beep Mixer" },
	{ "Mono Mixer", "Right Switch", "Right DAC" },
	{ "Mono Mixer", "Left Switch", "Left DAC" },
	{ "Speaker", NULL, "Mono Mixer" },

	{ "Mono Mixer", NULL, "SYSCLK" },
	{ "Left Lineout Mixer", NULL, "SYSCLK" },
	{ "Left Lineout Mixer", NULL, "SYSCLK" },

	{ "Beep Mixer", "Beep Capture Switch", "BEEP" },
	{ "Beep Mixer", NULL, "Zero Crossing Detector" },

	{ "Left DAC", NULL, "DAC Engine" },
	{ "Right DAC", NULL, "DAC Engine" },

	{ "Sound Engine", NULL, "SYSCLK" },
	{ "DSP", NULL, "Sound Engine" },

	{ "Left Decimator", NULL, "ADC Engine" },
	{ "Right Decimator", NULL, "ADC Engine" },

	{ "AIFCLK", NULL, "SYSCLK" },

	{ "Playback", NULL, "Serial Input Routing" },
	{ "Playback", NULL, "Serial Ports" },
	{ "Playback", NULL, "Clock Domain Transfer" },
	{ "Capture", NULL, "Serial Output Routing" },
	{ "Capture", NULL, "Serial Ports" },
	{ "Capture", NULL, "Clock Domain Transfer" },

	{ "AOUTL", NULL, "Left Lineout Mixer" },
	{ "AOUTR", NULL, "Right Lineout Mixer" },
	{ "SP", NULL, "Speaker" },
};

static const struct snd_soc_dapm_route adau1781_adc_dapm_routes[] = {
	{ "Left PGA", NULL, "LMIC" },
	{ "Right PGA", NULL, "RMIC" },

	{ "Left Decimator", NULL, "Left PGA" },
	{ "Right Decimator", NULL, "Right PGA" },
};

static const char * const adau1781_dmic_select_text[] = {
	"DMIC1", "DMIC2",
};

static SOC_ENUM_SINGLE_VIRT_DECL(adau1781_dmic_select_enum,
	adau1781_dmic_select_text);

static const struct snd_kcontrol_new adau1781_dmic_mux =
	SOC_DAPM_ENUM("DMIC Select", adau1781_dmic_select_enum);

static const struct snd_soc_dapm_widget adau1781_dmic_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("DMIC Select", SND_SOC_NOPM, 0, 0, &adau1781_dmic_mux),

	SND_SOC_DAPM_ADC("DMIC1", NULL, ADAU1781_DMIC_BEEP_CTRL, 4, 0),
	SND_SOC_DAPM_ADC("DMIC2", NULL, ADAU1781_DMIC_BEEP_CTRL, 5, 0),
};

static const struct snd_soc_dapm_route adau1781_dmic_dapm_routes[] = {
	{ "DMIC1", NULL, "LMIC" },
	{ "DMIC2", NULL, "RMIC" },

	{ "DMIC1", NULL, "Digital Mic" },
	{ "DMIC2", NULL, "Digital Mic" },

	{ "DMIC Select", "DMIC1", "DMIC1" },
	{ "DMIC Select", "DMIC2", "DMIC2" },

	{ "Left Decimator", NULL, "DMIC Select" },
	{ "Right Decimator", NULL, "DMIC Select" },
};

static int adau1781_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	struct adau *adau = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN);

		/* Precharge */
		regmap_update_bits(adau->regmap, ADAU1781_DIG_PWDN1, 0x8, 0x8);
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(adau->regmap, ADAU1781_DIG_PWDN1, 0xc, 0x0);
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN, 0);
		break;
	}

	return 0;
}

static bool adau1781_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADAU1781_DMIC_BEEP_CTRL:
	case ADAU1781_LEFT_PGA:
	case ADAU1781_RIGHT_PGA:
	case ADAU1781_LEFT_PLAYBACK_MIXER:
	case ADAU1781_RIGHT_PLAYBACK_MIXER:
	case ADAU1781_MONO_PLAYBACK_MIXER:
	case ADAU1781_LEFT_LINEOUT:
	case ADAU1781_RIGHT_LINEOUT:
	case ADAU1781_SPEAKER:
	case ADAU1781_BEEP_ZC:
	case ADAU1781_DEJITTER:
	case ADAU1781_DIG_PWDN0:
	case ADAU1781_DIG_PWDN1:
		return true;
	default:
		break;
	}

	return adau17x1_readable_register(dev, reg);
}

static int adau1781_set_input_mode(struct adau *adau, unsigned int reg,
	bool differential)
{
	unsigned int val;

	if (differential)
		val = ADAU1781_INPUT_DIFFERNTIAL;
	else
		val = 0;

	return regmap_update_bits(adau->regmap, reg,
		ADAU1781_INPUT_DIFFERNTIAL, val);
}

static int adau1781_codec_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct adau1781_platform_data *pdata = dev_get_platdata(codec->dev);
	struct adau *adau = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = adau17x1_add_widgets(codec);
	if (ret)
		return ret;

	if (pdata) {
		ret = adau1781_set_input_mode(adau, ADAU1781_LEFT_PGA,
			pdata->left_input_differential);
		if (ret)
			return ret;
		ret = adau1781_set_input_mode(adau, ADAU1781_RIGHT_PGA,
			pdata->right_input_differential);
		if (ret)
			return ret;
	}

	if (pdata && pdata->use_dmic) {
		ret = snd_soc_dapm_new_controls(dapm,
			adau1781_dmic_dapm_widgets,
			ARRAY_SIZE(adau1781_dmic_dapm_widgets));
		if (ret)
			return ret;
		ret = snd_soc_dapm_add_routes(dapm, adau1781_dmic_dapm_routes,
			ARRAY_SIZE(adau1781_dmic_dapm_routes));
		if (ret)
			return ret;
	} else {
		ret = snd_soc_dapm_add_routes(dapm, adau1781_adc_dapm_routes,
			ARRAY_SIZE(adau1781_adc_dapm_routes));
		if (ret)
			return ret;
	}

	ret = adau17x1_add_routes(codec);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_codec_driver adau1781_codec_driver = {
	.probe = adau1781_codec_probe,
	.resume = adau17x1_resume,
	.set_bias_level = adau1781_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls		= adau1781_controls,
		.num_controls		= ARRAY_SIZE(adau1781_controls),
		.dapm_widgets		= adau1781_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(adau1781_dapm_widgets),
		.dapm_routes		= adau1781_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(adau1781_dapm_routes),
	},
};

#define ADAU1781_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver adau1781_dai_driver = {
	.name = "adau-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ADAU1781_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ADAU1781_FORMATS,
	},
	.ops = &adau17x1_dai_ops,
};

const struct regmap_config adau1781_regmap_config = {
	.val_bits		= 8,
	.reg_bits		= 16,
	.max_register		= 0x40f8,
	.reg_defaults		= adau1781_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(adau1781_reg_defaults),
	.readable_reg		= adau1781_readable_register,
	.volatile_reg		= adau17x1_volatile_register,
	.precious_reg		= adau17x1_precious_register,
	.cache_type		= REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(adau1781_regmap_config);

int adau1781_probe(struct device *dev, struct regmap *regmap,
	enum adau17x1_type type, void (*switch_mode)(struct device *dev))
{
	const char *firmware_name;
	int ret;

	switch (type) {
	case ADAU1381:
		firmware_name = ADAU1381_FIRMWARE;
		break;
	case ADAU1781:
		firmware_name = ADAU1781_FIRMWARE;
		break;
	default:
		return -EINVAL;
	}

	ret = adau17x1_probe(dev, regmap, type, switch_mode, firmware_name);
	if (ret)
		return ret;

	return snd_soc_register_codec(dev, &adau1781_codec_driver,
		&adau1781_dai_driver, 1);
}
EXPORT_SYMBOL_GPL(adau1781_probe);

MODULE_DESCRIPTION("ASoC ADAU1381/ADAU1781 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
