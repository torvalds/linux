/*
 * stac9766.c  --  ALSA SoC STAC9766 codec support
 *
 * Copyright 2009 Jon Smirl, Digispeaker
 * Author: Jon Smirl <jonsmirl@gmail.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Features:-
 *
 *   o Support for AC97 Codec, S/PDIF
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "stac9766.h"

/*
 * STAC9766 register cache
 */
static const u16 stac9766_reg[] = {
	0x6A90, 0x8000, 0x8000, 0x8000, /* 6 */
	0x0000, 0x0000, 0x8008, 0x8008, /* e */
	0x8808, 0x8808, 0x8808, 0x8808, /* 16 */
	0x8808, 0x0000, 0x8000, 0x0000, /* 1e */
	0x0000, 0x0000, 0x0000, 0x000f, /* 26 */
	0x0a05, 0x0400, 0xbb80, 0x0000, /* 2e */
	0x0000, 0xbb80, 0x0000, 0x0000, /* 36 */
	0x0000, 0x2000, 0x0000, 0x0100, /* 3e */
	0x0000, 0x0000, 0x0080, 0x0000, /* 46 */
	0x0000, 0x0000, 0x0003, 0xffff, /* 4e */
	0x0000, 0x0000, 0x0000, 0x0000, /* 56 */
	0x4000, 0x0000, 0x0000, 0x0000, /* 5e */
	0x1201, 0xFFFF, 0xFFFF, 0x0000, /* 66 */
	0x0000, 0x0000, 0x0000, 0x0000, /* 6e */
	0x0000, 0x0000, 0x0000, 0x0006, /* 76 */
	0x0000, 0x0000, 0x0000, 0x0000, /* 7e */
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

static const DECLARE_TLV_DB_LINEAR(master_tlv, -4600, 0);
static const DECLARE_TLV_DB_LINEAR(record_tlv, 0, 2250);
static const DECLARE_TLV_DB_LINEAR(beep_tlv, -4500, 0);
static const DECLARE_TLV_DB_LINEAR(mix_tlv, -3450, 1200);

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

static int stac9766_ac97_write(struct snd_soc_codec *codec, unsigned int reg,
			       unsigned int val)
{
	struct snd_ac97 *ac97 = snd_soc_codec_get_drvdata(codec);
	u16 *cache = codec->reg_cache;

	if (reg > AC97_STAC_PAGE0) {
		stac9766_ac97_write(codec, AC97_INT_PAGING, 0);
		soc_ac97_ops->write(ac97, reg, val);
		stac9766_ac97_write(codec, AC97_INT_PAGING, 1);
		return 0;
	}
	if (reg / 2 >= ARRAY_SIZE(stac9766_reg))
		return -EIO;

	soc_ac97_ops->write(ac97, reg, val);
	cache[reg / 2] = val;
	return 0;
}

static unsigned int stac9766_ac97_read(struct snd_soc_codec *codec,
				       unsigned int reg)
{
	struct snd_ac97 *ac97 = snd_soc_codec_get_drvdata(codec);
	u16 val = 0, *cache = codec->reg_cache;

	if (reg > AC97_STAC_PAGE0) {
		stac9766_ac97_write(codec, AC97_INT_PAGING, 0);
		val = soc_ac97_ops->read(ac97, reg - AC97_STAC_PAGE0);
		stac9766_ac97_write(codec, AC97_INT_PAGING, 1);
		return val;
	}
	if (reg / 2 >= ARRAY_SIZE(stac9766_reg))
		return -EIO;

	if (reg == AC97_RESET || reg == AC97_GPIO_STATUS ||
		reg == AC97_INT_PAGING || reg == AC97_VENDOR_ID1 ||
		reg == AC97_VENDOR_ID2) {

		val = soc_ac97_ops->read(ac97, reg);
		return val;
	}
	return cache[reg / 2];
}

static int ac97_analog_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short reg, vra;

	vra = stac9766_ac97_read(codec, AC97_EXTENDED_STATUS);

	vra |= 0x1; /* enable variable rate audio */
	vra &= ~0x4; /* disable SPDIF output */

	stac9766_ac97_write(codec, AC97_EXTENDED_STATUS, vra);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = AC97_PCM_FRONT_DAC_RATE;
	else
		reg = AC97_PCM_LR_ADC_RATE;

	return stac9766_ac97_write(codec, reg, runtime->rate);
}

static int ac97_digital_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short reg, vra;

	stac9766_ac97_write(codec, AC97_SPDIF, 0x2002);

	vra = stac9766_ac97_read(codec, AC97_EXTENDED_STATUS);
	vra |= 0x5; /* Enable VRA and SPDIF out */

	stac9766_ac97_write(codec, AC97_EXTENDED_STATUS, vra);

	reg = AC97_PCM_FRONT_DAC_RATE;

	return stac9766_ac97_write(codec, reg, runtime->rate);
}

static int stac9766_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON: /* full On */
	case SND_SOC_BIAS_PREPARE: /* partial On */
	case SND_SOC_BIAS_STANDBY: /* Off, with power */
		stac9766_ac97_write(codec, AC97_POWERDOWN, 0x0000);
		break;
	case SND_SOC_BIAS_OFF: /* Off, without power */
		/* disable everything including AC link */
		stac9766_ac97_write(codec, AC97_POWERDOWN, 0xffff);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int stac9766_reset(struct snd_soc_codec *codec, int try_warm)
{
	struct snd_ac97 *ac97 = snd_soc_codec_get_drvdata(codec);

	if (try_warm && soc_ac97_ops->warm_reset) {
		soc_ac97_ops->warm_reset(ac97);
		if (stac9766_ac97_read(codec, 0) == stac9766_reg[0])
			return 1;
	}

	soc_ac97_ops->reset(ac97);
	if (soc_ac97_ops->warm_reset)
		soc_ac97_ops->warm_reset(ac97);
	if (stac9766_ac97_read(codec, 0) != stac9766_reg[0])
		return -EIO;
	return 0;
}

static int stac9766_codec_resume(struct snd_soc_codec *codec)
{
	struct snd_ac97 *ac97 = snd_soc_codec_get_drvdata(codec);
	u16 id, reset;

	reset = 0;
	/* give the codec an AC97 warm reset to start the link */
reset:
	if (reset > 5) {
		dev_err(codec->dev, "Failed to resume\n");
		return -EIO;
	}
	ac97->bus->ops->warm_reset(ac97);
	id = soc_ac97_ops->read(ac97, AC97_VENDOR_ID2);
	if (id != 0x4c13) {
		stac9766_reset(codec, 0);
		reset++;
		goto reset;
	}

	return 0;
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
		.formats = SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE,
	},
	/* alsa ops */
	.ops = &stac9766_dai_ops_digital,
}
};

static int stac9766_codec_probe(struct snd_soc_codec *codec)
{
	struct snd_ac97 *ac97;
	int ret = 0;

	ac97 = snd_soc_new_ac97_codec(codec);
	if (IS_ERR(ac97))
		return PTR_ERR(ac97);

	snd_soc_codec_set_drvdata(codec, ac97);

	/* do a cold reset for the controller and then try
	 * a warm reset followed by an optional cold reset for codec */
	stac9766_reset(codec, 0);
	ret = stac9766_reset(codec, 1);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset: AC97 link error\n");
		goto codec_err;
	}

	return 0;

codec_err:
	snd_soc_free_ac97_codec(ac97);
	return ret;
}

static int stac9766_codec_remove(struct snd_soc_codec *codec)
{
	struct snd_ac97 *ac97 = snd_soc_codec_get_drvdata(codec);

	snd_soc_free_ac97_codec(ac97);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_stac9766 = {
	.controls = stac9766_snd_ac97_controls,
	.num_controls = ARRAY_SIZE(stac9766_snd_ac97_controls),
	.write = stac9766_ac97_write,
	.read = stac9766_ac97_read,
	.set_bias_level = stac9766_set_bias_level,
	.suspend_bias_off = true,
	.probe = stac9766_codec_probe,
	.remove = stac9766_codec_remove,
	.resume = stac9766_codec_resume,
	.reg_cache_size = ARRAY_SIZE(stac9766_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 2,
	.reg_cache_default = stac9766_reg,
};

static int stac9766_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_stac9766, stac9766_dai, ARRAY_SIZE(stac9766_dai));
}

static int stac9766_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver stac9766_codec_driver = {
	.driver = {
			.name = "stac9766-codec",
			.owner = THIS_MODULE,
	},

	.probe = stac9766_probe,
	.remove = stac9766_remove,
};

module_platform_driver(stac9766_codec_driver);

MODULE_DESCRIPTION("ASoC stac9766 driver");
MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_LICENSE("GPL");
