/*
 * wm8737.c  --  WM8737 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8737.h"

#define WM8737_NUM_SUPPLIES 4
static const char *wm8737_supply_names[WM8737_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD",
	"MVDD",
};

/* codec private data */
struct wm8737_priv {
	enum snd_soc_control_type control_type;
	struct regulator_bulk_data supplies[WM8737_NUM_SUPPLIES];
	unsigned int mclk;
};

static const u16 wm8737_reg[WM8737_REGISTER_COUNT] = {
	0x00C3,     /* R0  - Left PGA volume */
	0x00C3,     /* R1  - Right PGA volume */
	0x0007,     /* R2  - AUDIO path L */
	0x0007,     /* R3  - AUDIO path R */
	0x0000,     /* R4  - 3D Enhance */
	0x0000,     /* R5  - ADC Control */
	0x0000,     /* R6  - Power Management */
	0x000A,     /* R7  - Audio Format */
	0x0000,     /* R8  - Clocking */
	0x000F,     /* R9  - MIC Preamp Control */
	0x0003,     /* R10 - Misc Bias Control */
	0x0000,     /* R11 - Noise Gate */
	0x007C,     /* R12 - ALC1 */
	0x0000,     /* R13 - ALC2 */
	0x0032,     /* R14 - ALC3 */
};

static int wm8737_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8737_RESET, 0);
}

static const unsigned int micboost_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(1300, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(1800, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2800, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(3300, 0, 0),
};
static const DECLARE_TLV_DB_SCALE(pga_tlv, -9750, 50, 1);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(ng_tlv, -7800, 600, 0);
static const DECLARE_TLV_DB_SCALE(alc_max_tlv, -1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(alc_target_tlv, -1800, 100, 0);

static const char *micbias_enum_text[] = {
	"25%",
	"50%",
	"75%",
	"100%",
};

static const struct soc_enum micbias_enum =
	SOC_ENUM_SINGLE(WM8737_MIC_PREAMP_CONTROL, 0, 4, micbias_enum_text);

static const char *low_cutoff_text[] = {
	"Low", "High"
};

static const struct soc_enum low_3d =
	SOC_ENUM_SINGLE(WM8737_3D_ENHANCE, 6, 2, low_cutoff_text);

static const char *high_cutoff_text[] = {
	"High", "Low"
};

static const struct soc_enum high_3d =
	SOC_ENUM_SINGLE(WM8737_3D_ENHANCE, 5, 2, high_cutoff_text);

static const char *alc_fn_text[] = {
	"Disabled", "Right", "Left", "Stereo"
};

static const struct soc_enum alc_fn =
	SOC_ENUM_SINGLE(WM8737_ALC1, 7, 4, alc_fn_text);

static const char *alc_hold_text[] = {
	"0", "2.67ms", "5.33ms", "10.66ms", "21.32ms", "42.64ms", "85.28ms",
	"170.56ms", "341.12ms", "682.24ms", "1.364s", "2.728s", "5.458s",
	"10.916s", "21.832s", "43.691s"
};

static const struct soc_enum alc_hold =
	SOC_ENUM_SINGLE(WM8737_ALC2, 0, 16, alc_hold_text);

static const char *alc_atk_text[] = {
	"8.4ms", "16.8ms", "33.6ms", "67.2ms", "134.4ms", "268.8ms", "537.6ms",
	"1.075s", "2.15s", "4.3s", "8.6s"
};

static const struct soc_enum alc_atk =
	SOC_ENUM_SINGLE(WM8737_ALC3, 0, 11, alc_atk_text);

static const char *alc_dcy_text[] = {
	"33.6ms", "67.2ms", "134.4ms", "268.8ms", "537.6ms", "1.075s", "2.15s",
	"4.3s", "8.6s", "17.2s", "34.41s"
};

static const struct soc_enum alc_dcy =
	SOC_ENUM_SINGLE(WM8737_ALC3, 4, 11, alc_dcy_text);

static const struct snd_kcontrol_new wm8737_snd_controls[] = {
SOC_DOUBLE_R_TLV("Mic Boost Volume", WM8737_AUDIO_PATH_L, WM8737_AUDIO_PATH_R,
		 6, 3, 0, micboost_tlv),
SOC_DOUBLE_R("Mic Boost Switch", WM8737_AUDIO_PATH_L, WM8737_AUDIO_PATH_R,
	     4, 1, 0),
SOC_DOUBLE("Mic ZC Switch", WM8737_AUDIO_PATH_L, WM8737_AUDIO_PATH_R,
	   3, 1, 0),

SOC_DOUBLE_R_TLV("Capture Volume", WM8737_LEFT_PGA_VOLUME,
		 WM8737_RIGHT_PGA_VOLUME, 0, 255, 0, pga_tlv),
SOC_DOUBLE("Capture ZC Switch", WM8737_AUDIO_PATH_L, WM8737_AUDIO_PATH_R,
	   2, 1, 0),

SOC_DOUBLE("INPUT1 DC Bias Switch", WM8737_MISC_BIAS_CONTROL, 0, 1, 1, 0),

SOC_ENUM("Mic PGA Bias", micbias_enum),
SOC_SINGLE("ADC Low Power Switch", WM8737_ADC_CONTROL, 2, 1, 0),
SOC_SINGLE("High Pass Filter Switch", WM8737_ADC_CONTROL, 0, 1, 1),
SOC_DOUBLE("Polarity Invert Switch", WM8737_ADC_CONTROL, 5, 6, 1, 0),

SOC_SINGLE("3D Switch", WM8737_3D_ENHANCE, 0, 1, 0),
SOC_SINGLE("3D Depth", WM8737_3D_ENHANCE, 1, 15, 0),
SOC_ENUM("3D Low Cut-off", low_3d),
SOC_ENUM("3D High Cut-off", low_3d),
SOC_SINGLE_TLV("3D ADC Volume", WM8737_3D_ENHANCE, 7, 1, 1, adc_tlv),

SOC_SINGLE("Noise Gate Switch", WM8737_NOISE_GATE, 0, 1, 0),
SOC_SINGLE_TLV("Noise Gate Threshold Volume", WM8737_NOISE_GATE, 2, 7, 0,
	       ng_tlv),

SOC_ENUM("ALC", alc_fn),
SOC_SINGLE_TLV("ALC Max Gain Volume", WM8737_ALC1, 4, 7, 0, alc_max_tlv),
SOC_SINGLE_TLV("ALC Target Volume", WM8737_ALC1, 0, 15, 0, alc_target_tlv),
SOC_ENUM("ALC Hold Time", alc_hold),
SOC_SINGLE("ALC ZC Switch", WM8737_ALC2, 4, 1, 0),
SOC_ENUM("ALC Attack Time", alc_atk),
SOC_ENUM("ALC Decay Time", alc_dcy),
};

static const char *linsel_text[] = {
	"LINPUT1", "LINPUT2", "LINPUT3", "LINPUT1 DC",
};

static const struct soc_enum linsel_enum =
	SOC_ENUM_SINGLE(WM8737_AUDIO_PATH_L, 7, 4, linsel_text);

static const struct snd_kcontrol_new linsel_mux =
	SOC_DAPM_ENUM("LINSEL", linsel_enum);


static const char *rinsel_text[] = {
	"RINPUT1", "RINPUT2", "RINPUT3", "RINPUT1 DC",
};

static const struct soc_enum rinsel_enum =
	SOC_ENUM_SINGLE(WM8737_AUDIO_PATH_R, 7, 4, rinsel_text);

static const struct snd_kcontrol_new rinsel_mux =
	SOC_DAPM_ENUM("RINSEL", rinsel_enum);

static const char *bypass_text[] = {
	"Direct", "Preamp"
};

static const struct soc_enum lbypass_enum =
	SOC_ENUM_SINGLE(WM8737_MIC_PREAMP_CONTROL, 2, 2, bypass_text);

static const struct snd_kcontrol_new lbypass_mux =
	SOC_DAPM_ENUM("Left Bypass", lbypass_enum);


static const struct soc_enum rbypass_enum =
	SOC_ENUM_SINGLE(WM8737_MIC_PREAMP_CONTROL, 3, 2, bypass_text);

static const struct snd_kcontrol_new rbypass_mux =
	SOC_DAPM_ENUM("Left Bypass", rbypass_enum);

static const struct snd_soc_dapm_widget wm8737_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("LINPUT1"),
SND_SOC_DAPM_INPUT("LINPUT2"),
SND_SOC_DAPM_INPUT("LINPUT3"),
SND_SOC_DAPM_INPUT("RINPUT1"),
SND_SOC_DAPM_INPUT("RINPUT2"),
SND_SOC_DAPM_INPUT("RINPUT3"),
SND_SOC_DAPM_INPUT("LACIN"),
SND_SOC_DAPM_INPUT("RACIN"),

SND_SOC_DAPM_MUX("LINSEL", SND_SOC_NOPM, 0, 0, &linsel_mux),
SND_SOC_DAPM_MUX("RINSEL", SND_SOC_NOPM, 0, 0, &rinsel_mux),

SND_SOC_DAPM_MUX("Left Preamp Mux", SND_SOC_NOPM, 0, 0, &lbypass_mux),
SND_SOC_DAPM_MUX("Right Preamp Mux", SND_SOC_NOPM, 0, 0, &rbypass_mux),

SND_SOC_DAPM_PGA("PGAL", WM8737_POWER_MANAGEMENT, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("PGAR", WM8737_POWER_MANAGEMENT, 4, 0, NULL, 0),

SND_SOC_DAPM_DAC("ADCL", NULL, WM8737_POWER_MANAGEMENT, 3, 0),
SND_SOC_DAPM_DAC("ADCR", NULL, WM8737_POWER_MANAGEMENT, 2, 0),

SND_SOC_DAPM_AIF_OUT("AIF", "Capture", 0, WM8737_POWER_MANAGEMENT, 6, 0),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "LINSEL", "LINPUT1", "LINPUT1" },
	{ "LINSEL", "LINPUT2", "LINPUT2" },
	{ "LINSEL", "LINPUT3", "LINPUT3" },
	{ "LINSEL", "LINPUT1 DC", "LINPUT1" },

	{ "RINSEL", "RINPUT1", "RINPUT1" },
	{ "RINSEL", "RINPUT2", "RINPUT2" },
	{ "RINSEL", "RINPUT3", "RINPUT3" },
	{ "RINSEL", "RINPUT1 DC", "RINPUT1" },

	{ "Left Preamp Mux", "Preamp", "LINSEL" },
	{ "Left Preamp Mux", "Direct", "LACIN" },

	{ "Right Preamp Mux", "Preamp", "RINSEL" },
	{ "Right Preamp Mux", "Direct", "RACIN" },

	{ "PGAL", NULL, "Left Preamp Mux" },
	{ "PGAR", NULL, "Right Preamp Mux" },

	{ "ADCL", NULL, "PGAL" },
	{ "ADCR", NULL, "PGAR" },

	{ "AIF", NULL, "ADCL" },
	{ "AIF", NULL, "ADCR" },
};

static int wm8737_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8737_dapm_widgets,
				  ARRAY_SIZE(wm8737_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	return 0;
}

/* codec mclk clock divider coefficients */
static const struct {
	u32 mclk;
	u32 rate;
	u8 usb;
	u8 sr;
} coeff_div[] = {
	{ 12288000,  8000, 0,  0x4 },
	{ 12288000, 12000, 0,  0x8 },
	{ 12288000, 16000, 0,  0xa },
	{ 12288000, 24000, 0, 0x1c },
	{ 12288000, 32000, 0,  0xc },
	{ 12288000, 48000, 0,    0 },
	{ 12288000, 96000, 0,  0xe },

	{ 11289600,  8000, 0, 0x14 },
	{ 11289600, 11025, 0, 0x18 },
	{ 11289600, 22050, 0, 0x1a },
	{ 11289600, 44100, 0, 0x10 },
	{ 11289600, 88200, 0, 0x1e },

	{ 18432000,  8000, 0,  0x5 },
	{ 18432000, 12000, 0,  0x9 },
	{ 18432000, 16000, 0,  0xb },
	{ 18432000, 24000, 0, 0x1b },
	{ 18432000, 32000, 0,  0xd },
	{ 18432000, 48000, 0,  0x1 },
	{ 18432000, 96000, 0, 0x1f },

	{ 16934400,  8000, 0, 0x15 },
	{ 16934400, 11025, 0, 0x19 },
	{ 16934400, 22050, 0, 0x1b },
	{ 16934400, 44100, 0, 0x11 },
	{ 16934400, 88200, 0, 0x1f },

	{ 12000000,  8000, 1,  0x4 },
	{ 12000000, 11025, 1, 0x19 },
	{ 12000000, 12000, 1,  0x8 },
	{ 12000000, 16000, 1,  0xa },
	{ 12000000, 22050, 1, 0x1b },
	{ 12000000, 24000, 1, 0x1c },
	{ 12000000, 32000, 1,  0xc },
	{ 12000000, 44100, 1, 0x11 },
	{ 12000000, 48000, 1,  0x0 },
	{ 12000000, 88200, 1, 0x1f },
	{ 12000000, 96000, 1,  0xe },
};

static int wm8737_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8737_priv *wm8737 = snd_soc_codec_get_drvdata(codec);
	int i;
	u16 clocking = 0;
	u16 af = 0;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate != params_rate(params))
			continue;

		if (coeff_div[i].mclk == wm8737->mclk)
			break;

		if (coeff_div[i].mclk == wm8737->mclk * 2) {
			clocking |= WM8737_CLKDIV2;
			break;
		}
	}

	if (i == ARRAY_SIZE(coeff_div)) {
		dev_err(codec->dev, "%dHz MCLK can't support %dHz\n",
			wm8737->mclk, params_rate(params));
		return -EINVAL;
	}

	clocking |= coeff_div[i].usb | (coeff_div[i].sr << WM8737_SR_SHIFT);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		af |= 0x8;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		af |= 0x10;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		af |= 0x18;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8737_AUDIO_FORMAT, WM8737_WL_MASK, af);
	snd_soc_update_bits(codec, WM8737_CLOCKING,
			    WM8737_USB_MODE | WM8737_CLKDIV2 | WM8737_SR_MASK,
			    clocking);

	return 0;
}

static int wm8737_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8737_priv *wm8737 = snd_soc_codec_get_drvdata(codec);
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (freq == coeff_div[i].mclk ||
		    freq == coeff_div[i].mclk * 2) {
			wm8737->mclk = freq;
			return 0;
		}
	}

	dev_err(codec->dev, "MCLK rate %dHz not supported\n", freq);

	return -EINVAL;
}


static int wm8737_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 af = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		af |= WM8737_MS;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		af |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		af |= 0x1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		af |= 0x3;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		af |= 0x13;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		af |= WM8737_LRP;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8737_AUDIO_FORMAT,
			    WM8737_FORMAT_MASK | WM8737_LRP | WM8737_MS, af);

	return 0;
}

static int wm8737_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8737_priv *wm8737 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID at 2*75k */
		snd_soc_update_bits(codec, WM8737_MISC_BIAS_CONTROL,
				    WM8737_VMIDSEL_MASK, 0);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8737->supplies),
						    wm8737->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			snd_soc_cache_sync(codec);

			/* Fast VMID ramp at 2*2.5k */
			snd_soc_update_bits(codec, WM8737_MISC_BIAS_CONTROL,
					    WM8737_VMIDSEL_MASK, 0x4);

			/* Bring VMID up */
			snd_soc_update_bits(codec, WM8737_POWER_MANAGEMENT,
					    WM8737_VMID_MASK |
					    WM8737_VREF_MASK,
					    WM8737_VMID_MASK |
					    WM8737_VREF_MASK);

			msleep(500);
		}

		/* VMID at 2*300k */
		snd_soc_update_bits(codec, WM8737_MISC_BIAS_CONTROL,
				    WM8737_VMIDSEL_MASK, 2);

		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, WM8737_POWER_MANAGEMENT,
				    WM8737_VMID_MASK | WM8737_VREF_MASK, 0);

		regulator_bulk_disable(ARRAY_SIZE(wm8737->supplies),
				       wm8737->supplies);
		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

#define WM8737_RATES SNDRV_PCM_RATE_8000_96000

#define WM8737_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8737_dai_ops = {
	.hw_params	= wm8737_hw_params,
	.set_sysclk	= wm8737_set_dai_sysclk,
	.set_fmt	= wm8737_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8737_dai = {
	.name = "wm8737",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,  /* Mono modes not yet supported */
		.channels_max = 2,
		.rates = WM8737_RATES,
		.formats = WM8737_FORMATS,
	},
	.ops = &wm8737_dai_ops,
};

#ifdef CONFIG_PM
static int wm8737_suspend(struct snd_soc_codec *codec)
{
	wm8737_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8737_resume(struct snd_soc_codec *codec)
{
	wm8737_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define wm8737_suspend NULL
#define wm8737_resume NULL
#endif

static int wm8737_probe(struct snd_soc_codec *codec)
{
	struct wm8737_priv *wm8737 = snd_soc_codec_get_drvdata(codec);
	int ret, i;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, wm8737->control_type);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8737->supplies); i++)
		wm8737->supplies[i].supply = wm8737_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8737->supplies),
				 wm8737->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8737->supplies),
				    wm8737->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = wm8737_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	snd_soc_update_bits(codec, WM8737_LEFT_PGA_VOLUME, WM8737_LVU,
			    WM8737_LVU);
	snd_soc_update_bits(codec, WM8737_RIGHT_PGA_VOLUME, WM8737_RVU,
			    WM8737_RVU);

	wm8737_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(wm8737->supplies), wm8737->supplies);

	snd_soc_add_controls(codec, wm8737_snd_controls,
			     ARRAY_SIZE(wm8737_snd_controls));
	wm8737_add_widgets(codec);

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8737->supplies), wm8737->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8737->supplies), wm8737->supplies);

	return ret;
}

static int wm8737_remove(struct snd_soc_codec *codec)
{
	struct wm8737_priv *wm8737 = snd_soc_codec_get_drvdata(codec);

	wm8737_set_bias_level(codec, SND_SOC_BIAS_OFF);
	regulator_bulk_free(ARRAY_SIZE(wm8737->supplies), wm8737->supplies);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8737 = {
	.probe		= wm8737_probe,
	.remove		= wm8737_remove,
	.suspend	= wm8737_suspend,
	.resume		= wm8737_resume,
	.set_bias_level = wm8737_set_bias_level,

	.reg_cache_size = WM8737_REGISTER_COUNT - 1, /* Skip reset */
	.reg_word_size	= sizeof(u16),
	.reg_cache_default = wm8737_reg,
};

static const struct of_device_id wm8737_of_match[] = {
	{ .compatible = "wlf,wm8737", },
	{ }
};

MODULE_DEVICE_TABLE(of, wm8737_of_match);

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8737_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8737_priv *wm8737;
	int ret;

	wm8737 = kzalloc(sizeof(struct wm8737_priv), GFP_KERNEL);
	if (wm8737 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8737);
	wm8737->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
				      &soc_codec_dev_wm8737, &wm8737_dai, 1);
	if (ret < 0)
		kfree(wm8737);
	return ret;

}

static __devexit int wm8737_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8737_i2c_id[] = {
	{ "wm8737", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8737_i2c_id);

static struct i2c_driver wm8737_i2c_driver = {
	.driver = {
		.name = "wm8737",
		.owner = THIS_MODULE,
		.of_match_table = wm8737_of_match,
	},
	.probe =    wm8737_i2c_probe,
	.remove =   __devexit_p(wm8737_i2c_remove),
	.id_table = wm8737_i2c_id,
};
#endif

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8737_spi_probe(struct spi_device *spi)
{
	struct wm8737_priv *wm8737;
	int ret;

	wm8737 = kzalloc(sizeof(struct wm8737_priv), GFP_KERNEL);
	if (wm8737 == NULL)
		return -ENOMEM;

	wm8737->control_type = SND_SOC_SPI;
	spi_set_drvdata(spi, wm8737);

	ret = snd_soc_register_codec(&spi->dev,
				     &soc_codec_dev_wm8737, &wm8737_dai, 1);
	if (ret < 0)
		kfree(wm8737);
	return ret;
}

static int __devexit wm8737_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver wm8737_spi_driver = {
	.driver = {
		.name	= "wm8737",
		.owner	= THIS_MODULE,
		.of_match_table = wm8737_of_match,
	},
	.probe		= wm8737_spi_probe,
	.remove		= __devexit_p(wm8737_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

static int __init wm8737_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8737_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8737 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8737_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8737 SPI driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8737_modinit);

static void __exit wm8737_exit(void)
{
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8737_spi_driver);
#endif
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8737_i2c_driver);
#endif
}
module_exit(wm8737_exit);

MODULE_DESCRIPTION("ASoC WM8737 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
