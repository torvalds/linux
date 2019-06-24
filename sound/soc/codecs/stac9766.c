// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * stac9766.c  --  ALSA SoC STAC9766 codec support
 *
 * Copyright 2009 Jon Smirl, Digispeaker
 * Author: Jon Smirl <jonsmirl@gmail.com>
 *
 *  Features:-
 *
 *   o Support for AC97 Codec, S/PDIF
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define STAC9766_VENDOR_ID 0x83847666
#define STAC9766_VENDOR_ID_MASK 0xffffffff

#define AC97_STAC_DA_CONTROL 0x6A
#define AC97_STAC_ANALOG_SPECIAL 0x6E
#define AC97_STAC_STEREO_MIC 0x78

static const struct reg_default stac9766_reg_defaults[] = {
	{ 0x02, 0x8000 },
	{ 0x04, 0x8000 },
	{ 0x06, 0x8000 },
	{ 0x0a, 0x0000 },
	{ 0x0c, 0x8008 },
	{ 0x0e, 0x8008 },
	{ 0x10, 0x8808 },
	{ 0x12, 0x8808 },
	{ 0x14, 0x8808 },
	{ 0x16, 0x8808 },
	{ 0x18, 0x8808 },
	{ 0x1a, 0x0000 },
	{ 0x1c, 0x8000 },
	{ 0x20, 0x0000 },
	{ 0x22, 0x0000 },
	{ 0x28, 0x0a05 },
	{ 0x2c, 0xbb80 },
	{ 0x32, 0xbb80 },
	{ 0x3a, 0x2000 },
	{ 0x3e, 0x0100 },
	{ 0x4c, 0x0300 },
	{ 0x4e, 0xffff },
	{ 0x50, 0x0000 },
	{ 0x52, 0x0000 },
	{ 0x54, 0x0000 },
	{ 0x6a, 0x0000 },
	{ 0x6e, 0x1000 },
	{ 0x72, 0x0000 },
	{ 0x78, 0x0000 },
};

static const struct regmap_config stac9766_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x78,
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = regmap_ac97_default_volatile,

	.reg_defaults = stac9766_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(stac9766_reg_defaults),
};

static const char *stac9766_record_mux[] = {"Mic", "CD", "Video", "AUX",
			"Line", "Stereo Mix", "Mono Mix", "Phone"};
static const char *stac9766_mono_mux[] = {"Mix", "Mic"};
static const char *stac9766_mic_mux[] = {"Mic1", "Mic2"};
static const char *stac9766_SPDIF_mux[] = {"PCM", "ADC Record"};
static const char *stac9766_popbypass_mux[] = {"Normal", "Bypass Mixer"};
static const char *stac9766_record_all_mux[] = {"All analog",
	"Analog plus DAC"};
static const char *stac9766_boost1[] = {"0dB", "10dB"};
static const char *stac9766_boost2[] = {"0dB", "20dB"};
static const char *stac9766_stereo_mic[] = {"Off", "On"};

static SOC_ENUM_DOUBLE_DECL(stac9766_record_enum,
			    AC97_REC_SEL, 8, 0, stac9766_record_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_mono_enum,
			    AC97_GENERAL_PURPOSE, 9, stac9766_mono_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_mic_enum,
			    AC97_GENERAL_PURPOSE, 8, stac9766_mic_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_SPDIF_enum,
			    AC97_STAC_DA_CONTROL, 1, stac9766_SPDIF_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_popbypass_enum,
			    AC97_GENERAL_PURPOSE, 15, stac9766_popbypass_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_record_all_enum,
			    AC97_STAC_ANALOG_SPECIAL, 12,
			    stac9766_record_all_mux);
static SOC_ENUM_SINGLE_DECL(stac9766_boost1_enum,
			    AC97_MIC, 6, stac9766_boost1); /* 0/10dB */
static SOC_ENUM_SINGLE_DECL(stac9766_boost2_enum,
			    AC97_STAC_ANALOG_SPECIAL, 2, stac9766_boost2); /* 0/20dB */
static SOC_ENUM_SINGLE_DECL(stac9766_stereo_mic_enum,
			    AC97_STAC_STEREO_MIC, 2, stac9766_stereo_mic);

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(master_tlv, -4650, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(record_tlv,     0, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(beep_tlv,   -4500, 300, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(mix_tlv,    -3450, 150, 0);

static const struct snd_kcontrol_new stac9766_snd_ac97_controls[] = {
	SOC_DOUBLE_TLV("Speaker Volume", AC97_MASTER, 8, 0, 31, 1, master_tlv),
	SOC_SINGLE("Speaker Switch", AC97_MASTER, 15, 1, 1),
	SOC_DOUBLE_TLV("Headphone Volume", AC97_HEADPHONE, 8, 0, 31, 1,
		       master_tlv),
	SOC_SINGLE("Headphone Switch", AC97_HEADPHONE, 15, 1, 1),
	SOC_SINGLE_TLV("Mono Out Volume", AC97_MASTER_MONO, 0, 31, 1,
		       master_tlv),
	SOC_SINGLE("Mono Out Switch", AC97_MASTER_MONO, 15, 1, 1),

	SOC_DOUBLE_TLV("Record Volume", AC97_REC_GAIN, 8, 0, 15, 0, record_tlv),
	SOC_SINGLE("Record Switch", AC97_REC_GAIN, 15, 1, 1),


	SOC_SINGLE_TLV("Beep Volume", AC97_PC_BEEP, 1, 15, 1, beep_tlv),
	SOC_SINGLE("Beep Switch", AC97_PC_BEEP, 15, 1, 1),
	SOC_SINGLE("Beep Frequency", AC97_PC_BEEP, 5, 127, 1),
	SOC_SINGLE_TLV("Phone Volume", AC97_PHONE, 0, 31, 1, mix_tlv),
	SOC_SINGLE("Phone Switch", AC97_PHONE, 15, 1, 1),

	SOC_ENUM("Mic Boost1", stac9766_boost1_enum),
	SOC_ENUM("Mic Boost2", stac9766_boost2_enum),
	SOC_SINGLE_TLV("Mic Volume", AC97_MIC, 0, 31, 1, mix_tlv),
	SOC_SINGLE("Mic Switch", AC97_MIC, 15, 1, 1),
	SOC_ENUM("Stereo Mic", stac9766_stereo_mic_enum),

	SOC_DOUBLE_TLV("Line Volume", AC97_LINE, 8, 0, 31, 1, mix_tlv),
	SOC_SINGLE("Line Switch", AC97_LINE, 15, 1, 1),
	SOC_DOUBLE_TLV("CD Volume", AC97_CD, 8, 0, 31, 1, mix_tlv),
	SOC_SINGLE("CD Switch", AC97_CD, 15, 1, 1),
	SOC_DOUBLE_TLV("AUX Volume", AC97_AUX, 8, 0, 31, 1, mix_tlv),
	SOC_SINGLE("AUX Switch", AC97_AUX, 15, 1, 1),
	SOC_DOUBLE_TLV("Video Volume", AC97_VIDEO, 8, 0, 31, 1, mix_tlv),
	SOC_SINGLE("Video Switch", AC97_VIDEO, 15, 1, 1),

	SOC_DOUBLE_TLV("DAC Volume", AC97_PCM, 8, 0, 31, 1, mix_tlv),
	SOC_SINGLE("DAC Switch", AC97_PCM, 15, 1, 1),
	SOC_SINGLE("Loopback Test Switch", AC97_GENERAL_PURPOSE, 7, 1, 0),
	SOC_SINGLE("3D Volume", AC97_3D_CONTROL, 3, 2, 1),
	SOC_SINGLE("3D Switch", AC97_GENERAL_PURPOSE, 13, 1, 0),

	SOC_ENUM("SPDIF Mux", stac9766_SPDIF_enum),
	SOC_ENUM("Mic1/2 Mux", stac9766_mic_enum),
	SOC_ENUM("Record All Mux", stac9766_record_all_enum),
	SOC_ENUM("Record Mux", stac9766_record_enum),
	SOC_ENUM("Mono Mux", stac9766_mono_enum),
	SOC_ENUM("Pop Bypass Mux", stac9766_popbypass_enum),
};

static int ac97_analog_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short reg;

	/* enable variable rate audio, disable SPDIF output */
	snd_soc_component_update_bits(component, AC97_EXTENDED_STATUS, 0x5, 0x1);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = AC97_PCM_FRONT_DAC_RATE;
	else
		reg = AC97_PCM_LR_ADC_RATE;

	return snd_soc_component_write(component, reg, runtime->rate);
}

static int ac97_digital_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short reg;

	snd_soc_component_write(component, AC97_SPDIF, 0x2002);

	/* Enable VRA and SPDIF out */
	snd_soc_component_update_bits(component, AC97_EXTENDED_STATUS, 0x5, 0x5);

	reg = AC97_PCM_FRONT_DAC_RATE;

	return snd_soc_component_write(component, reg, runtime->rate);
}

static int stac9766_set_bias_level(struct snd_soc_component *component,
				   enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON: /* full On */
	case SND_SOC_BIAS_PREPARE: /* partial On */
	case SND_SOC_BIAS_STANDBY: /* Off, with power */
		snd_soc_component_write(component, AC97_POWERDOWN, 0x0000);
		break;
	case SND_SOC_BIAS_OFF: /* Off, without power */
		/* disable everything including AC link */
		snd_soc_component_write(component, AC97_POWERDOWN, 0xffff);
		break;
	}
	return 0;
}

static int stac9766_component_resume(struct snd_soc_component *component)
{
	struct snd_ac97 *ac97 = snd_soc_component_get_drvdata(component);

	return snd_ac97_reset(ac97, true, STAC9766_VENDOR_ID,
		STAC9766_VENDOR_ID_MASK);
}

static const struct snd_soc_dai_ops stac9766_dai_ops_analog = {
	.prepare = ac97_analog_prepare,
};

static const struct snd_soc_dai_ops stac9766_dai_ops_digital = {
	.prepare = ac97_digital_prepare,
};

static struct snd_soc_dai_driver stac9766_dai[] = {
{
	.name = "stac9766-hifi-analog",

	/* stream cababilities */
	.playback = {
		.stream_name = "stac9766 analog",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SND_SOC_STD_AC97_FMTS,
	},
	.capture = {
		.stream_name = "stac9766 analog",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SND_SOC_STD_AC97_FMTS,
	},
	/* alsa ops */
	.ops = &stac9766_dai_ops_analog,
},
{
	.name = "stac9766-hifi-IEC958",

	/* stream cababilities */
	.playback = {
		.stream_name = "stac9766 IEC958",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 | \
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE,
	},
	/* alsa ops */
	.ops = &stac9766_dai_ops_digital,
}
};

static int stac9766_component_probe(struct snd_soc_component *component)
{
	struct snd_ac97 *ac97;
	struct regmap *regmap;
	int ret;

	ac97 = snd_soc_new_ac97_component(component, STAC9766_VENDOR_ID,
			STAC9766_VENDOR_ID_MASK);
	if (IS_ERR(ac97))
		return PTR_ERR(ac97);

	regmap = regmap_init_ac97(ac97, &stac9766_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto err_free_ac97;
	}

	snd_soc_component_init_regmap(component, regmap);
	snd_soc_component_set_drvdata(component, ac97);

	return 0;
err_free_ac97:
	snd_soc_free_ac97_component(ac97);
	return ret;
}

static void stac9766_component_remove(struct snd_soc_component *component)
{
	struct snd_ac97 *ac97 = snd_soc_component_get_drvdata(component);

	snd_soc_component_exit_regmap(component);
	snd_soc_free_ac97_component(ac97);
}

static const struct snd_soc_component_driver soc_component_dev_stac9766 = {
	.controls		= stac9766_snd_ac97_controls,
	.num_controls		= ARRAY_SIZE(stac9766_snd_ac97_controls),
	.set_bias_level		= stac9766_set_bias_level,
	.probe			= stac9766_component_probe,
	.remove			= stac9766_component_remove,
	.resume			= stac9766_component_resume,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,

};

static int stac9766_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_stac9766, stac9766_dai, ARRAY_SIZE(stac9766_dai));
}

static struct platform_driver stac9766_codec_driver = {
	.driver = {
			.name = "stac9766-codec",
	},

	.probe = stac9766_probe,
};

module_platform_driver(stac9766_codec_driver);

MODULE_DESCRIPTION("ASoC stac9766 driver");
MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_LICENSE("GPL");
