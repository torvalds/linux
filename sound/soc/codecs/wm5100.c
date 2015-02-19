/*
 * wm5100.c  --  WM5100 ALSA SoC Audio driver
 *
 * Copyright 2011-2 Wolfson Microelectronics plc
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
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/gcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/fixed.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm5100.h>

#include "wm5100.h"

#define WM5100_NUM_CORE_SUPPLIES 2
static const char *wm5100_core_supply_names[WM5100_NUM_CORE_SUPPLIES] = {
	"DBVDD1",
	"LDOVDD", /* If DCVDD is supplied externally specify as LDOVDD */
};

#define WM5100_AIFS     3
#define WM5100_SYNC_SRS 3

struct wm5100_fll {
	int fref;
	int fout;
	int src;
	struct completion lock;
};

/* codec private data */
struct wm5100_priv {
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_codec *codec;

	struct regulator_bulk_data core_supplies[WM5100_NUM_CORE_SUPPLIES];

	int rev;

	int sysclk;
	int asyncclk;

	bool aif_async[WM5100_AIFS];
	bool aif_symmetric[WM5100_AIFS];
	int sr_ref[WM5100_SYNC_SRS];

	bool out_ena[2];

	struct snd_soc_jack *jack;
	bool jack_detecting;
	bool jack_mic;
	int jack_mode;
	int jack_flips;

	struct wm5100_fll fll[2];

	struct wm5100_pdata pdata;

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

static int wm5100_sr_code[] = {
	0,
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	0,
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

static int wm5100_sr_regs[WM5100_SYNC_SRS] = {
	WM5100_CLOCKING_4,
	WM5100_CLOCKING_5,
	WM5100_CLOCKING_6,
};

static int wm5100_alloc_sr(struct snd_soc_codec *codec, int rate)
{
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	int sr_code, sr_free, i;

	for (i = 0; i < ARRAY_SIZE(wm5100_sr_code); i++)
		if (wm5100_sr_code[i] == rate)
			break;
	if (i == ARRAY_SIZE(wm5100_sr_code)) {
		dev_err(codec->dev, "Unsupported sample rate: %dHz\n", rate);
		return -EINVAL;
	}
	sr_code = i;

	if ((wm5100->sysclk % rate) == 0) {
		/* Is this rate already in use? */
		sr_free = -1;
		for (i = 0; i < ARRAY_SIZE(wm5100_sr_regs); i++) {
			if (!wm5100->sr_ref[i] && sr_free == -1) {
				sr_free = i;
				continue;
			}
			if ((snd_soc_read(codec, wm5100_sr_regs[i]) &
			     WM5100_SAMPLE_RATE_1_MASK) == sr_code)
				break;
		}

		if (i < ARRAY_SIZE(wm5100_sr_regs)) {
			wm5100->sr_ref[i]++;
			dev_dbg(codec->dev, "SR %dHz, slot %d, ref %d\n",
				rate, i, wm5100->sr_ref[i]);
			return i;
		}

		if (sr_free == -1) {
			dev_err(codec->dev, "All SR slots already in use\n");
			return -EBUSY;
		}

		dev_dbg(codec->dev, "Allocating SR slot %d for %dHz\n",
			sr_free, rate);
		wm5100->sr_ref[sr_free]++;
		snd_soc_update_bits(codec, wm5100_sr_regs[sr_free],
				    WM5100_SAMPLE_RATE_1_MASK,
				    sr_code);

		return sr_free;

	} else {
		dev_err(codec->dev,
			"SR %dHz incompatible with %dHz SYSCLK and %dHz ASYNCCLK\n",
			rate, wm5100->sysclk, wm5100->asyncclk);
		return -EINVAL;
	}
}

static void wm5100_free_sr(struct snd_soc_codec *codec, int rate)
{
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	int i, sr_code;

	for (i = 0; i < ARRAY_SIZE(wm5100_sr_code); i++)
		if (wm5100_sr_code[i] == rate)
			break;
	if (i == ARRAY_SIZE(wm5100_sr_code)) {
		dev_err(codec->dev, "Unsupported sample rate: %dHz\n", rate);
		return;
	}
	sr_code = wm5100_sr_code[i];

	for (i = 0; i < ARRAY_SIZE(wm5100_sr_regs); i++) {
		if (!wm5100->sr_ref[i])
			continue;

		if ((snd_soc_read(codec, wm5100_sr_regs[i]) &
		     WM5100_SAMPLE_RATE_1_MASK) == sr_code)
			break;
	}
	if (i < ARRAY_SIZE(wm5100_sr_regs)) {
		wm5100->sr_ref[i]--;
		dev_dbg(codec->dev, "Dereference SR %dHz, count now %d\n",
			rate, wm5100->sr_ref[i]);
	} else {
		dev_warn(codec->dev, "Freeing unreferenced sample rate %dHz\n",
			 rate);
	}
}

static int wm5100_reset(struct wm5100_priv *wm5100)
{
	if (wm5100->pdata.reset) {
		gpio_set_value_cansleep(wm5100->pdata.reset, 0);
		gpio_set_value_cansleep(wm5100->pdata.reset, 1);

		return 0;
	} else {
		return regmap_write(wm5100->regmap, WM5100_SOFTWARE_RESET, 0);
	}
}

static DECLARE_TLV_DB_SCALE(in_tlv, -6300, 100, 0);
static DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static DECLARE_TLV_DB_SCALE(mixer_tlv, -3200, 100, 0);
static DECLARE_TLV_DB_SCALE(out_tlv, -6400, 100, 0);
static DECLARE_TLV_DB_SCALE(digital_tlv, -6400, 50, 0);

static const char *wm5100_mixer_texts[] = {
	"None",
	"Tone Generator 1",
	"Tone Generator 2",
	"AEC loopback",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"IN3L",
	"IN3R",
	"IN4L",
	"IN4R",
	"AIF1RX1",
	"AIF1RX2",
	"AIF1RX3",
	"AIF1RX4",
	"AIF1RX5",
	"AIF1RX6",
	"AIF1RX7",
	"AIF1RX8",
	"AIF2RX1",
	"AIF2RX2",
	"AIF3RX1",
	"AIF3RX2",
	"EQ1",
	"EQ2",
	"EQ3",
	"EQ4",
	"DRC1L",
	"DRC1R",
	"LHPF1",
	"LHPF2",
	"LHPF3",
	"LHPF4",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"DSP2.1",
	"DSP2.2",
	"DSP2.3",
	"DSP2.4",
	"DSP2.5",
	"DSP2.6",
	"DSP3.1",
	"DSP3.2",
	"DSP3.3",
	"DSP3.4",
	"DSP3.5",
	"DSP3.6",
	"ASRC1L",
	"ASRC1R",
	"ASRC2L",
	"ASRC2R",
	"ISRC1INT1",
	"ISRC1INT2",
	"ISRC1INT3",
	"ISRC1INT4",
	"ISRC2INT1",
	"ISRC2INT2",
	"ISRC2INT3",
	"ISRC2INT4",
	"ISRC1DEC1",
	"ISRC1DEC2",
	"ISRC1DEC3",
	"ISRC1DEC4",
	"ISRC2DEC1",
	"ISRC2DEC2",
	"ISRC2DEC3",
	"ISRC2DEC4",
};

static int wm5100_mixer_values[] = {
	0x00,
	0x04,   /* Tone */
	0x05,
	0x08,   /* AEC */
	0x10,   /* Input */
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x16,
	0x17,
	0x20,   /* AIF */
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x27,
	0x28,
	0x29,
	0x30,   /* AIF3 - check */
	0x31,
	0x50,   /* EQ */
	0x51,
	0x52,
	0x53,
	0x54,
	0x58,   /* DRC */
	0x59,
	0x60,   /* LHPF1 */
	0x61,   /* LHPF2 */
	0x62,   /* LHPF3 */
	0x63,   /* LHPF4 */
	0x68,   /* DSP1 */
	0x69,
	0x6a,
	0x6b,
	0x6c,
	0x6d,
	0x70,   /* DSP2 */
	0x71,
	0x72,
	0x73,
	0x74,
	0x75,
	0x78,   /* DSP3 */
	0x79,
	0x7a,
	0x7b,
	0x7c,
	0x7d,
	0x90,   /* ASRC1 */
	0x91,
	0x92,   /* ASRC2 */
	0x93,
	0xa0,   /* ISRC1DEC1 */
	0xa1,
	0xa2,
	0xa3,
	0xa4,   /* ISRC1INT1 */
	0xa5,
	0xa6,
	0xa7,
	0xa8,   /* ISRC2DEC1 */
	0xa9,
	0xaa,
	0xab,
	0xac,   /* ISRC2INT1 */
	0xad,
	0xae,
	0xaf,
};

#define WM5100_MIXER_CONTROLS(name, base) \
	SOC_SINGLE_TLV(name " Input 1 Volume", base + 1 , \
		       WM5100_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 2 Volume", base + 3 , \
		       WM5100_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 3 Volume", base + 5 , \
		       WM5100_MIXER_VOL_SHIFT, 80, 0, mixer_tlv), \
	SOC_SINGLE_TLV(name " Input 4 Volume", base + 7 , \
		       WM5100_MIXER_VOL_SHIFT, 80, 0, mixer_tlv)

#define WM5100_MUX_ENUM_DECL(name, reg) \
	SOC_VALUE_ENUM_SINGLE_DECL(name, reg, 0, 0xff, 			\
				   wm5100_mixer_texts, wm5100_mixer_values)

#define WM5100_MUX_CTL_DECL(name) \
	const struct snd_kcontrol_new name##_mux =	\
		SOC_DAPM_ENUM("Route", name##_enum)

#define WM5100_MIXER_ENUMS(name, base_reg) \
	static WM5100_MUX_ENUM_DECL(name##_in1_enum, base_reg);	     \
	static WM5100_MUX_ENUM_DECL(name##_in2_enum, base_reg + 2);  \
	static WM5100_MUX_ENUM_DECL(name##_in3_enum, base_reg + 4);  \
	static WM5100_MUX_ENUM_DECL(name##_in4_enum, base_reg + 6);  \
	static WM5100_MUX_CTL_DECL(name##_in1); \
	static WM5100_MUX_CTL_DECL(name##_in2); \
	static WM5100_MUX_CTL_DECL(name##_in3); \
	static WM5100_MUX_CTL_DECL(name##_in4) 

WM5100_MIXER_ENUMS(HPOUT1L, WM5100_OUT1LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(HPOUT1R, WM5100_OUT1RMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(HPOUT2L, WM5100_OUT2LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(HPOUT2R, WM5100_OUT2RMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(HPOUT3L, WM5100_OUT3LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(HPOUT3R, WM5100_OUT3RMIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(SPKOUTL, WM5100_OUT4LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(SPKOUTR, WM5100_OUT4RMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(SPKDAT1L, WM5100_OUT5LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(SPKDAT1R, WM5100_OUT5RMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(SPKDAT2L, WM5100_OUT6LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(SPKDAT2R, WM5100_OUT6RMIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(PWM1, WM5100_PWM1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(PWM2, WM5100_PWM1MIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(AIF1TX1, WM5100_AIF1TX1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX2, WM5100_AIF1TX2MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX3, WM5100_AIF1TX3MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX4, WM5100_AIF1TX4MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX5, WM5100_AIF1TX5MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX6, WM5100_AIF1TX6MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX7, WM5100_AIF1TX7MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF1TX8, WM5100_AIF1TX8MIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(AIF2TX1, WM5100_AIF2TX1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF2TX2, WM5100_AIF2TX2MIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(AIF3TX1, WM5100_AIF1TX1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(AIF3TX2, WM5100_AIF1TX2MIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(EQ1, WM5100_EQ1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(EQ2, WM5100_EQ2MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(EQ3, WM5100_EQ3MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(EQ4, WM5100_EQ4MIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(DRC1L, WM5100_DRC1LMIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(DRC1R, WM5100_DRC1RMIX_INPUT_1_SOURCE);

WM5100_MIXER_ENUMS(LHPF1, WM5100_HPLP1MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(LHPF2, WM5100_HPLP2MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(LHPF3, WM5100_HPLP3MIX_INPUT_1_SOURCE);
WM5100_MIXER_ENUMS(LHPF4, WM5100_HPLP4MIX_INPUT_1_SOURCE);

#define WM5100_MUX(name, ctrl) \
	SND_SOC_DAPM_MUX(name, SND_SOC_NOPM, 0, 0, ctrl)

#define WM5100_MIXER_WIDGETS(name, name_str)	\
	WM5100_MUX(name_str " Input 1", &name##_in1_mux), \
	WM5100_MUX(name_str " Input 2", &name##_in2_mux), \
	WM5100_MUX(name_str " Input 3", &name##_in3_mux), \
	WM5100_MUX(name_str " Input 4", &name##_in4_mux), \
	SND_SOC_DAPM_MIXER(name_str " Mixer", SND_SOC_NOPM, 0, 0, NULL, 0)

#define WM5100_MIXER_INPUT_ROUTES(name)	\
	{ name, "Tone Generator 1", "Tone Generator 1" }, \
        { name, "Tone Generator 2", "Tone Generator 2" }, \
        { name, "IN1L", "IN1L PGA" }, \
        { name, "IN1R", "IN1R PGA" }, \
        { name, "IN2L", "IN2L PGA" }, \
        { name, "IN2R", "IN2R PGA" }, \
        { name, "IN3L", "IN3L PGA" }, \
        { name, "IN3R", "IN3R PGA" }, \
        { name, "IN4L", "IN4L PGA" }, \
        { name, "IN4R", "IN4R PGA" }, \
        { name, "AIF1RX1", "AIF1RX1" }, \
        { name, "AIF1RX2", "AIF1RX2" }, \
        { name, "AIF1RX3", "AIF1RX3" }, \
        { name, "AIF1RX4", "AIF1RX4" }, \
        { name, "AIF1RX5", "AIF1RX5" }, \
        { name, "AIF1RX6", "AIF1RX6" }, \
        { name, "AIF1RX7", "AIF1RX7" }, \
        { name, "AIF1RX8", "AIF1RX8" }, \
        { name, "AIF2RX1", "AIF2RX1" }, \
        { name, "AIF2RX2", "AIF2RX2" }, \
        { name, "AIF3RX1", "AIF3RX1" }, \
        { name, "AIF3RX2", "AIF3RX2" }, \
        { name, "EQ1", "EQ1" }, \
        { name, "EQ2", "EQ2" }, \
        { name, "EQ3", "EQ3" }, \
        { name, "EQ4", "EQ4" }, \
        { name, "DRC1L", "DRC1L" }, \
        { name, "DRC1R", "DRC1R" }, \
        { name, "LHPF1", "LHPF1" }, \
        { name, "LHPF2", "LHPF2" }, \
        { name, "LHPF3", "LHPF3" }, \
        { name, "LHPF4", "LHPF4" }

#define WM5100_MIXER_ROUTES(widget, name) \
	{ widget, NULL, name " Mixer" },         \
	{ name " Mixer", NULL, name " Input 1" }, \
	{ name " Mixer", NULL, name " Input 2" }, \
	{ name " Mixer", NULL, name " Input 3" }, \
	{ name " Mixer", NULL, name " Input 4" }, \
	WM5100_MIXER_INPUT_ROUTES(name " Input 1"), \
	WM5100_MIXER_INPUT_ROUTES(name " Input 2"), \
	WM5100_MIXER_INPUT_ROUTES(name " Input 3"), \
	WM5100_MIXER_INPUT_ROUTES(name " Input 4")

static const char *wm5100_lhpf_mode_text[] = {
	"Low-pass", "High-pass"
};

static SOC_ENUM_SINGLE_DECL(wm5100_lhpf1_mode,
			    WM5100_HPLPF1_1, WM5100_LHPF1_MODE_SHIFT,
			    wm5100_lhpf_mode_text);

static SOC_ENUM_SINGLE_DECL(wm5100_lhpf2_mode,
			    WM5100_HPLPF2_1, WM5100_LHPF2_MODE_SHIFT,
			    wm5100_lhpf_mode_text);

static SOC_ENUM_SINGLE_DECL(wm5100_lhpf3_mode,
			    WM5100_HPLPF3_1, WM5100_LHPF3_MODE_SHIFT,
			    wm5100_lhpf_mode_text);

static SOC_ENUM_SINGLE_DECL(wm5100_lhpf4_mode,
			    WM5100_HPLPF4_1, WM5100_LHPF4_MODE_SHIFT,
			    wm5100_lhpf_mode_text);

static const struct snd_kcontrol_new wm5100_snd_controls[] = {
SOC_SINGLE("IN1 High Performance Switch", WM5100_IN1L_CONTROL,
	   WM5100_IN1_OSR_SHIFT, 1, 0),
SOC_SINGLE("IN2 High Performance Switch", WM5100_IN2L_CONTROL,
	   WM5100_IN2_OSR_SHIFT, 1, 0),
SOC_SINGLE("IN3 High Performance Switch", WM5100_IN3L_CONTROL,
	   WM5100_IN3_OSR_SHIFT, 1, 0),
SOC_SINGLE("IN4 High Performance Switch", WM5100_IN4L_CONTROL,
	   WM5100_IN4_OSR_SHIFT, 1, 0),

/* Only applicable for analogue inputs */
SOC_DOUBLE_R_TLV("IN1 Volume", WM5100_IN1L_CONTROL, WM5100_IN1R_CONTROL,
		 WM5100_IN1L_PGA_VOL_SHIFT, 94, 0, in_tlv),
SOC_DOUBLE_R_TLV("IN2 Volume", WM5100_IN2L_CONTROL, WM5100_IN2R_CONTROL,
		 WM5100_IN2L_PGA_VOL_SHIFT, 94, 0, in_tlv),
SOC_DOUBLE_R_TLV("IN3 Volume", WM5100_IN3L_CONTROL, WM5100_IN3R_CONTROL,
		 WM5100_IN3L_PGA_VOL_SHIFT, 94, 0, in_tlv),
SOC_DOUBLE_R_TLV("IN4 Volume", WM5100_IN4L_CONTROL, WM5100_IN4R_CONTROL,
		 WM5100_IN4L_PGA_VOL_SHIFT, 94, 0, in_tlv),

SOC_DOUBLE_R_TLV("IN1 Digital Volume", WM5100_ADC_DIGITAL_VOLUME_1L,
		 WM5100_ADC_DIGITAL_VOLUME_1R, WM5100_IN1L_VOL_SHIFT, 191,
		 0, digital_tlv),
SOC_DOUBLE_R_TLV("IN2 Digital Volume", WM5100_ADC_DIGITAL_VOLUME_2L,
		 WM5100_ADC_DIGITAL_VOLUME_2R, WM5100_IN2L_VOL_SHIFT, 191,
		 0, digital_tlv),
SOC_DOUBLE_R_TLV("IN3 Digital Volume", WM5100_ADC_DIGITAL_VOLUME_3L,
		 WM5100_ADC_DIGITAL_VOLUME_3R, WM5100_IN3L_VOL_SHIFT, 191,
		 0, digital_tlv),
SOC_DOUBLE_R_TLV("IN4 Digital Volume", WM5100_ADC_DIGITAL_VOLUME_4L,
		 WM5100_ADC_DIGITAL_VOLUME_4R, WM5100_IN4L_VOL_SHIFT, 191,
		 0, digital_tlv),

SOC_DOUBLE_R("IN1 Switch", WM5100_ADC_DIGITAL_VOLUME_1L,
	     WM5100_ADC_DIGITAL_VOLUME_1R, WM5100_IN1L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("IN2 Switch", WM5100_ADC_DIGITAL_VOLUME_2L,
	     WM5100_ADC_DIGITAL_VOLUME_2R, WM5100_IN2L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("IN3 Switch", WM5100_ADC_DIGITAL_VOLUME_3L,
	     WM5100_ADC_DIGITAL_VOLUME_3R, WM5100_IN3L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("IN4 Switch", WM5100_ADC_DIGITAL_VOLUME_4L,
	     WM5100_ADC_DIGITAL_VOLUME_4R, WM5100_IN4L_MUTE_SHIFT, 1, 1),

SND_SOC_BYTES_MASK("EQ1 Coefficients", WM5100_EQ1_1, 20, WM5100_EQ1_ENA),
SND_SOC_BYTES_MASK("EQ2 Coefficients", WM5100_EQ2_1, 20, WM5100_EQ2_ENA),
SND_SOC_BYTES_MASK("EQ3 Coefficients", WM5100_EQ3_1, 20, WM5100_EQ3_ENA),
SND_SOC_BYTES_MASK("EQ4 Coefficients", WM5100_EQ4_1, 20, WM5100_EQ4_ENA),

SND_SOC_BYTES_MASK("DRC Coefficients", WM5100_DRC1_CTRL1, 5,
		   WM5100_DRCL_ENA | WM5100_DRCR_ENA),

SND_SOC_BYTES("LHPF1 Coefficeints", WM5100_HPLPF1_2, 1),
SND_SOC_BYTES("LHPF2 Coefficeints", WM5100_HPLPF2_2, 1),
SND_SOC_BYTES("LHPF3 Coefficeints", WM5100_HPLPF3_2, 1),
SND_SOC_BYTES("LHPF4 Coefficeints", WM5100_HPLPF4_2, 1),

SOC_SINGLE("HPOUT1 High Performance Switch", WM5100_OUT_VOLUME_1L,
	   WM5100_OUT1_OSR_SHIFT, 1, 0),
SOC_SINGLE("HPOUT2 High Performance Switch", WM5100_OUT_VOLUME_2L,
	   WM5100_OUT2_OSR_SHIFT, 1, 0),
SOC_SINGLE("HPOUT3 High Performance Switch", WM5100_OUT_VOLUME_3L,
	   WM5100_OUT3_OSR_SHIFT, 1, 0),
SOC_SINGLE("SPKOUT High Performance Switch", WM5100_OUT_VOLUME_4L,
	   WM5100_OUT4_OSR_SHIFT, 1, 0),
SOC_SINGLE("SPKDAT1 High Performance Switch", WM5100_DAC_VOLUME_LIMIT_5L,
	   WM5100_OUT5_OSR_SHIFT, 1, 0),
SOC_SINGLE("SPKDAT2 High Performance Switch", WM5100_DAC_VOLUME_LIMIT_6L,
	   WM5100_OUT6_OSR_SHIFT, 1, 0),

SOC_DOUBLE_R_TLV("HPOUT1 Digital Volume", WM5100_DAC_DIGITAL_VOLUME_1L,
		 WM5100_DAC_DIGITAL_VOLUME_1R, WM5100_OUT1L_VOL_SHIFT, 159, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("HPOUT2 Digital Volume", WM5100_DAC_DIGITAL_VOLUME_2L,
		 WM5100_DAC_DIGITAL_VOLUME_2R, WM5100_OUT2L_VOL_SHIFT, 159, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("HPOUT3 Digital Volume", WM5100_DAC_DIGITAL_VOLUME_3L,
		 WM5100_DAC_DIGITAL_VOLUME_3R, WM5100_OUT3L_VOL_SHIFT, 159, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("SPKOUT Digital Volume", WM5100_DAC_DIGITAL_VOLUME_4L,
		 WM5100_DAC_DIGITAL_VOLUME_4R, WM5100_OUT4L_VOL_SHIFT, 159, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("SPKDAT1 Digital Volume", WM5100_DAC_DIGITAL_VOLUME_5L,
		 WM5100_DAC_DIGITAL_VOLUME_5R, WM5100_OUT5L_VOL_SHIFT, 159, 0,
		 digital_tlv),
SOC_DOUBLE_R_TLV("SPKDAT2 Digital Volume", WM5100_DAC_DIGITAL_VOLUME_6L,
		 WM5100_DAC_DIGITAL_VOLUME_6R, WM5100_OUT6L_VOL_SHIFT, 159, 0,
		 digital_tlv),

SOC_DOUBLE_R("HPOUT1 Digital Switch", WM5100_DAC_DIGITAL_VOLUME_1L,
	     WM5100_DAC_DIGITAL_VOLUME_1R, WM5100_OUT1L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("HPOUT2 Digital Switch", WM5100_DAC_DIGITAL_VOLUME_2L,
	     WM5100_DAC_DIGITAL_VOLUME_2R, WM5100_OUT2L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("HPOUT3 Digital Switch", WM5100_DAC_DIGITAL_VOLUME_3L,
	     WM5100_DAC_DIGITAL_VOLUME_3R, WM5100_OUT3L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("SPKOUT Digital Switch", WM5100_DAC_DIGITAL_VOLUME_4L,
	     WM5100_DAC_DIGITAL_VOLUME_4R, WM5100_OUT4L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("SPKDAT1 Digital Switch", WM5100_DAC_DIGITAL_VOLUME_5L,
	     WM5100_DAC_DIGITAL_VOLUME_5R, WM5100_OUT5L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("SPKDAT2 Digital Switch", WM5100_DAC_DIGITAL_VOLUME_6L,
	     WM5100_DAC_DIGITAL_VOLUME_6R, WM5100_OUT6L_MUTE_SHIFT, 1, 1),

/* FIXME: Only valid from -12dB to 0dB (52-64) */
SOC_DOUBLE_R_TLV("HPOUT1 Volume", WM5100_OUT_VOLUME_1L, WM5100_OUT_VOLUME_1R,
		 WM5100_OUT1L_PGA_VOL_SHIFT, 64, 0, out_tlv),
SOC_DOUBLE_R_TLV("HPOUT2 Volume", WM5100_OUT_VOLUME_2L, WM5100_OUT_VOLUME_2R,
		 WM5100_OUT2L_PGA_VOL_SHIFT, 64, 0, out_tlv),
SOC_DOUBLE_R_TLV("HPOUT3 Volume", WM5100_OUT_VOLUME_3L, WM5100_OUT_VOLUME_3R,
		 WM5100_OUT2L_PGA_VOL_SHIFT, 64, 0, out_tlv),

SOC_DOUBLE("SPKDAT1 Switch", WM5100_PDM_SPK1_CTRL_1, WM5100_SPK1L_MUTE_SHIFT,
	   WM5100_SPK1R_MUTE_SHIFT, 1, 1),
SOC_DOUBLE("SPKDAT2 Switch", WM5100_PDM_SPK2_CTRL_1, WM5100_SPK2L_MUTE_SHIFT,
	   WM5100_SPK2R_MUTE_SHIFT, 1, 1),

SOC_SINGLE_TLV("EQ1 Band 1 Volume", WM5100_EQ1_1, WM5100_EQ1_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 Band 2 Volume", WM5100_EQ1_1, WM5100_EQ1_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 Band 3 Volume", WM5100_EQ1_1, WM5100_EQ1_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 Band 4 Volume", WM5100_EQ1_2, WM5100_EQ1_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 Band 5 Volume", WM5100_EQ1_2, WM5100_EQ1_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

SOC_SINGLE_TLV("EQ2 Band 1 Volume", WM5100_EQ2_1, WM5100_EQ2_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Band 2 Volume", WM5100_EQ2_1, WM5100_EQ2_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Band 3 Volume", WM5100_EQ2_1, WM5100_EQ2_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Band 4 Volume", WM5100_EQ2_2, WM5100_EQ2_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Band 5 Volume", WM5100_EQ2_2, WM5100_EQ2_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

SOC_SINGLE_TLV("EQ3 Band 1 Volume", WM5100_EQ1_1, WM5100_EQ3_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Band 2 Volume", WM5100_EQ3_1, WM5100_EQ3_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Band 3 Volume", WM5100_EQ3_1, WM5100_EQ3_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Band 4 Volume", WM5100_EQ3_2, WM5100_EQ3_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Band 5 Volume", WM5100_EQ3_2, WM5100_EQ3_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

SOC_SINGLE_TLV("EQ4 Band 1 Volume", WM5100_EQ4_1, WM5100_EQ4_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Band 2 Volume", WM5100_EQ4_1, WM5100_EQ4_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Band 3 Volume", WM5100_EQ4_1, WM5100_EQ4_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Band 4 Volume", WM5100_EQ4_2, WM5100_EQ4_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Band 5 Volume", WM5100_EQ4_2, WM5100_EQ4_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

SOC_ENUM("LHPF1 Mode", wm5100_lhpf1_mode),
SOC_ENUM("LHPF2 Mode", wm5100_lhpf2_mode),
SOC_ENUM("LHPF3 Mode", wm5100_lhpf3_mode),
SOC_ENUM("LHPF4 Mode", wm5100_lhpf4_mode),

WM5100_MIXER_CONTROLS("HPOUT1L", WM5100_OUT1LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("HPOUT1R", WM5100_OUT1RMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("HPOUT2L", WM5100_OUT2LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("HPOUT2R", WM5100_OUT2RMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("HPOUT3L", WM5100_OUT3LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("HPOUT3R", WM5100_OUT3RMIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("SPKOUTL", WM5100_OUT4LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("SPKOUTR", WM5100_OUT4RMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("SPKDAT1L", WM5100_OUT5LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("SPKDAT1R", WM5100_OUT5RMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("SPKDAT2L", WM5100_OUT6LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("SPKDAT2R", WM5100_OUT6RMIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("PWM1", WM5100_PWM1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("PWM2", WM5100_PWM2MIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("AIF1TX1", WM5100_AIF1TX1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX2", WM5100_AIF1TX2MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX3", WM5100_AIF1TX3MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX4", WM5100_AIF1TX4MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX5", WM5100_AIF1TX5MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX6", WM5100_AIF1TX6MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX7", WM5100_AIF1TX7MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF1TX8", WM5100_AIF1TX8MIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("AIF2TX1", WM5100_AIF2TX1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF2TX2", WM5100_AIF2TX2MIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("AIF3TX1", WM5100_AIF3TX1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("AIF3TX2", WM5100_AIF3TX2MIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("EQ1", WM5100_EQ1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("EQ2", WM5100_EQ2MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("EQ3", WM5100_EQ3MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("EQ4", WM5100_EQ4MIX_INPUT_1_SOURCE),

WM5100_MIXER_CONTROLS("DRC1L", WM5100_DRC1LMIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("DRC1R", WM5100_DRC1RMIX_INPUT_1_SOURCE),
SND_SOC_BYTES_MASK("DRC", WM5100_DRC1_CTRL1, 5,
		   WM5100_DRCL_ENA | WM5100_DRCR_ENA),

WM5100_MIXER_CONTROLS("LHPF1", WM5100_HPLP1MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("LHPF2", WM5100_HPLP2MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("LHPF3", WM5100_HPLP3MIX_INPUT_1_SOURCE),
WM5100_MIXER_CONTROLS("LHPF4", WM5100_HPLP4MIX_INPUT_1_SOURCE),
};

static void wm5100_seq_notifier(struct snd_soc_dapm_context *dapm,
				enum snd_soc_dapm_type event, int subseq)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(dapm);
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	u16 val, expect, i;

	/* Wait for the outputs to flag themselves as enabled */
	if (wm5100->out_ena[0]) {
		expect = snd_soc_read(codec, WM5100_CHANNEL_ENABLES_1);
		for (i = 0; i < 200; i++) {
			val = snd_soc_read(codec, WM5100_OUTPUT_STATUS_1);
			if (val == expect) {
				wm5100->out_ena[0] = false;
				break;
			}
		}
		if (i == 200) {
			dev_err(codec->dev, "Timeout waiting for OUTPUT1 %x\n",
				expect);
		}
	}

	if (wm5100->out_ena[1]) {
		expect = snd_soc_read(codec, WM5100_OUTPUT_ENABLES_2);
		for (i = 0; i < 200; i++) {
			val = snd_soc_read(codec, WM5100_OUTPUT_STATUS_2);
			if (val == expect) {
				wm5100->out_ena[1] = false;
				break;
			}
		}
		if (i == 200) {
			dev_err(codec->dev, "Timeout waiting for OUTPUT2 %x\n",
				expect);
		}
	}
}

static int wm5100_out_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);

	switch (w->reg) {
	case WM5100_CHANNEL_ENABLES_1:
		wm5100->out_ena[0] = true;
		break;
	case WM5100_OUTPUT_ENABLES_2:
		wm5100->out_ena[0] = true;
		break;
	default:
		break;
	}

	return 0;
}

static void wm5100_log_status3(struct wm5100_priv *wm5100, int val)
{
	if (val & WM5100_SPK_SHUTDOWN_WARN_EINT)
		dev_crit(wm5100->dev, "Speaker shutdown warning\n");
	if (val & WM5100_SPK_SHUTDOWN_EINT)
		dev_crit(wm5100->dev, "Speaker shutdown\n");
	if (val & WM5100_CLKGEN_ERR_EINT)
		dev_crit(wm5100->dev, "SYSCLK underclocked\n");
	if (val & WM5100_CLKGEN_ERR_ASYNC_EINT)
		dev_crit(wm5100->dev, "ASYNCCLK underclocked\n");
}

static void wm5100_log_status4(struct wm5100_priv *wm5100, int val)
{
	if (val & WM5100_AIF3_ERR_EINT)
		dev_err(wm5100->dev, "AIF3 configuration error\n");
	if (val & WM5100_AIF2_ERR_EINT)
		dev_err(wm5100->dev, "AIF2 configuration error\n");
	if (val & WM5100_AIF1_ERR_EINT)
		dev_err(wm5100->dev, "AIF1 configuration error\n");
	if (val & WM5100_CTRLIF_ERR_EINT)
		dev_err(wm5100->dev, "Control interface error\n");
	if (val & WM5100_ISRC2_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "ISRC2 underclocked\n");
	if (val & WM5100_ISRC1_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "ISRC1 underclocked\n");
	if (val & WM5100_FX_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "FX underclocked\n");
	if (val & WM5100_AIF3_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "AIF3 underclocked\n");
	if (val & WM5100_AIF2_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "AIF2 underclocked\n");
	if (val & WM5100_AIF1_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "AIF1 underclocked\n");
	if (val & WM5100_ASRC_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "ASRC underclocked\n");
	if (val & WM5100_DAC_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "DAC underclocked\n");
	if (val & WM5100_ADC_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "ADC underclocked\n");
	if (val & WM5100_MIXER_UNDERCLOCKED_EINT)
		dev_err(wm5100->dev, "Mixer underclocked\n");
}

static int wm5100_post_ev(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = snd_soc_read(codec, WM5100_INTERRUPT_RAW_STATUS_3);
	ret &= WM5100_SPK_SHUTDOWN_WARN_STS |
		WM5100_SPK_SHUTDOWN_STS | WM5100_CLKGEN_ERR_STS |
		WM5100_CLKGEN_ERR_ASYNC_STS;
	wm5100_log_status3(wm5100, ret);

	ret = snd_soc_read(codec, WM5100_INTERRUPT_RAW_STATUS_4);
	wm5100_log_status4(wm5100, ret);

	return 0;
}

static const struct snd_soc_dapm_widget wm5100_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", WM5100_CLOCKING_3, WM5100_SYSCLK_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("ASYNCCLK", WM5100_CLOCKING_6, WM5100_ASYNC_CLK_ENA_SHIFT,
		    0, NULL, 0),

SND_SOC_DAPM_REGULATOR_SUPPLY("CPVDD", 20, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("DBVDD2", 0, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("DBVDD3", 0, 0),

SND_SOC_DAPM_SUPPLY("CP1", WM5100_HP_CHARGE_PUMP_1, WM5100_CP1_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("CP2", WM5100_MIC_CHARGE_PUMP_1, WM5100_CP2_ENA_SHIFT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("CP2 Active", WM5100_MIC_CHARGE_PUMP_1,
		    WM5100_CP2_BYPASS_SHIFT, 1, NULL, 0),

SND_SOC_DAPM_SUPPLY("MICBIAS1", WM5100_MIC_BIAS_CTRL_1, WM5100_MICB1_ENA_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS2", WM5100_MIC_BIAS_CTRL_2, WM5100_MICB2_ENA_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS3", WM5100_MIC_BIAS_CTRL_3, WM5100_MICB3_ENA_SHIFT,
		    0, NULL, 0),

SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("IN4L"),
SND_SOC_DAPM_INPUT("IN4R"),
SND_SOC_DAPM_SIGGEN("TONE"),

SND_SOC_DAPM_PGA_E("IN1L PGA", WM5100_INPUT_ENABLES, WM5100_IN1L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN1R PGA", WM5100_INPUT_ENABLES, WM5100_IN1R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2L PGA", WM5100_INPUT_ENABLES, WM5100_IN2L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2R PGA", WM5100_INPUT_ENABLES, WM5100_IN2R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN3L PGA", WM5100_INPUT_ENABLES, WM5100_IN3L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN3R PGA", WM5100_INPUT_ENABLES, WM5100_IN3R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN4L PGA", WM5100_INPUT_ENABLES, WM5100_IN4L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN4R PGA", WM5100_INPUT_ENABLES, WM5100_IN4R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("Tone Generator 1", WM5100_TONE_GENERATOR_1,
		 WM5100_TONE1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("Tone Generator 2", WM5100_TONE_GENERATOR_1,
		 WM5100_TONE2_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_AIF_IN("AIF1RX1", "AIF1 Playback", 0,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX2", "AIF1 Playback", 1,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX3", "AIF1 Playback", 2,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX4", "AIF1 Playback", 3,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX5", "AIF1 Playback", 4,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX6", "AIF1 Playback", 5,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX7", "AIF1 Playback", 6,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX8", "AIF1 Playback", 7,
		    WM5100_AUDIO_IF_1_27, WM5100_AIF1RX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("AIF2RX1", "AIF2 Playback", 0,
		    WM5100_AUDIO_IF_2_27, WM5100_AIF2RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX2", "AIF2 Playback", 1,
		    WM5100_AUDIO_IF_2_27, WM5100_AIF2RX2_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("AIF3RX1", "AIF3 Playback", 0,
		    WM5100_AUDIO_IF_3_27, WM5100_AIF3RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF3RX2", "AIF3 Playback", 1,
		    WM5100_AUDIO_IF_3_27, WM5100_AIF3RX2_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("AIF1TX1", "AIF1 Capture", 0,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX2", "AIF1 Capture", 1,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX3", "AIF1 Capture", 2,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX4", "AIF1 Capture", 3,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX5", "AIF1 Capture", 4,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX6", "AIF1 Capture", 5,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX7", "AIF1 Capture", 6,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX8", "AIF1 Capture", 7,
		    WM5100_AUDIO_IF_1_26, WM5100_AIF1TX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("AIF2TX1", "AIF2 Capture", 0,
		    WM5100_AUDIO_IF_2_26, WM5100_AIF2TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX2", "AIF2 Capture", 1,
		    WM5100_AUDIO_IF_2_26, WM5100_AIF2TX2_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("AIF3TX1", "AIF3 Capture", 0,
		    WM5100_AUDIO_IF_3_26, WM5100_AIF3TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF3TX2", "AIF3 Capture", 1,
		    WM5100_AUDIO_IF_3_26, WM5100_AIF3TX2_ENA_SHIFT, 0),

SND_SOC_DAPM_PGA_E("OUT6L", WM5100_OUTPUT_ENABLES_2, WM5100_OUT6L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT6R", WM5100_OUTPUT_ENABLES_2, WM5100_OUT6R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT5L", WM5100_OUTPUT_ENABLES_2, WM5100_OUT5L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT5R", WM5100_OUTPUT_ENABLES_2, WM5100_OUT5R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT4L", WM5100_OUTPUT_ENABLES_2, WM5100_OUT4L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT4R", WM5100_OUTPUT_ENABLES_2, WM5100_OUT4R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT3L", WM5100_CHANNEL_ENABLES_1, WM5100_HP3L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT3R", WM5100_CHANNEL_ENABLES_1, WM5100_HP3R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT2L", WM5100_CHANNEL_ENABLES_1, WM5100_HP2L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT2R", WM5100_CHANNEL_ENABLES_1, WM5100_HP2R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT1L", WM5100_CHANNEL_ENABLES_1, WM5100_HP1L_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT1R", WM5100_CHANNEL_ENABLES_1, WM5100_HP1R_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("PWM1 Driver", WM5100_PWM_DRIVE_1, WM5100_PWM1_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("PWM2 Driver", WM5100_PWM_DRIVE_1, WM5100_PWM2_ENA_SHIFT, 0,
		   NULL, 0, wm5100_out_ev, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("EQ1", WM5100_EQ1_1, WM5100_EQ1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ2", WM5100_EQ2_1, WM5100_EQ2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ3", WM5100_EQ3_1, WM5100_EQ3_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ4", WM5100_EQ4_1, WM5100_EQ4_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("DRC1L", WM5100_DRC1_CTRL1, WM5100_DRCL_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC1R", WM5100_DRC1_CTRL1, WM5100_DRCR_ENA_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_PGA("LHPF1", WM5100_HPLPF1_1, WM5100_LHPF1_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF2", WM5100_HPLPF2_1, WM5100_LHPF2_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF3", WM5100_HPLPF3_1, WM5100_LHPF3_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF4", WM5100_HPLPF4_1, WM5100_LHPF4_ENA_SHIFT, 0,
		 NULL, 0),

WM5100_MIXER_WIDGETS(EQ1, "EQ1"),
WM5100_MIXER_WIDGETS(EQ2, "EQ2"),
WM5100_MIXER_WIDGETS(EQ3, "EQ3"),
WM5100_MIXER_WIDGETS(EQ4, "EQ4"),

WM5100_MIXER_WIDGETS(DRC1L, "DRC1L"),
WM5100_MIXER_WIDGETS(DRC1R, "DRC1R"),

WM5100_MIXER_WIDGETS(LHPF1, "LHPF1"),
WM5100_MIXER_WIDGETS(LHPF2, "LHPF2"),
WM5100_MIXER_WIDGETS(LHPF3, "LHPF3"),
WM5100_MIXER_WIDGETS(LHPF4, "LHPF4"),

WM5100_MIXER_WIDGETS(AIF1TX1, "AIF1TX1"),
WM5100_MIXER_WIDGETS(AIF1TX2, "AIF1TX2"),
WM5100_MIXER_WIDGETS(AIF1TX3, "AIF1TX3"),
WM5100_MIXER_WIDGETS(AIF1TX4, "AIF1TX4"),
WM5100_MIXER_WIDGETS(AIF1TX5, "AIF1TX5"),
WM5100_MIXER_WIDGETS(AIF1TX6, "AIF1TX6"),
WM5100_MIXER_WIDGETS(AIF1TX7, "AIF1TX7"),
WM5100_MIXER_WIDGETS(AIF1TX8, "AIF1TX8"),

WM5100_MIXER_WIDGETS(AIF2TX1, "AIF2TX1"),
WM5100_MIXER_WIDGETS(AIF2TX2, "AIF2TX2"),

WM5100_MIXER_WIDGETS(AIF3TX1, "AIF3TX1"),
WM5100_MIXER_WIDGETS(AIF3TX2, "AIF3TX2"),

WM5100_MIXER_WIDGETS(HPOUT1L, "HPOUT1L"),
WM5100_MIXER_WIDGETS(HPOUT1R, "HPOUT1R"),
WM5100_MIXER_WIDGETS(HPOUT2L, "HPOUT2L"),
WM5100_MIXER_WIDGETS(HPOUT2R, "HPOUT2R"),
WM5100_MIXER_WIDGETS(HPOUT3L, "HPOUT3L"),
WM5100_MIXER_WIDGETS(HPOUT3R, "HPOUT3R"),

WM5100_MIXER_WIDGETS(SPKOUTL, "SPKOUTL"),
WM5100_MIXER_WIDGETS(SPKOUTR, "SPKOUTR"),
WM5100_MIXER_WIDGETS(SPKDAT1L, "SPKDAT1L"),
WM5100_MIXER_WIDGETS(SPKDAT1R, "SPKDAT1R"),
WM5100_MIXER_WIDGETS(SPKDAT2L, "SPKDAT2L"),
WM5100_MIXER_WIDGETS(SPKDAT2R, "SPKDAT2R"),

WM5100_MIXER_WIDGETS(PWM1, "PWM1"),
WM5100_MIXER_WIDGETS(PWM2, "PWM2"),

SND_SOC_DAPM_OUTPUT("HPOUT1L"),
SND_SOC_DAPM_OUTPUT("HPOUT1R"),
SND_SOC_DAPM_OUTPUT("HPOUT2L"),
SND_SOC_DAPM_OUTPUT("HPOUT2R"),
SND_SOC_DAPM_OUTPUT("HPOUT3L"),
SND_SOC_DAPM_OUTPUT("HPOUT3R"),
SND_SOC_DAPM_OUTPUT("SPKOUTL"),
SND_SOC_DAPM_OUTPUT("SPKOUTR"),
SND_SOC_DAPM_OUTPUT("SPKDAT1"),
SND_SOC_DAPM_OUTPUT("SPKDAT2"),
SND_SOC_DAPM_OUTPUT("PWM1"),
SND_SOC_DAPM_OUTPUT("PWM2"),
};

/* We register a _POST event if we don't have IRQ support so we can
 * look at the error status from the CODEC - if we've got the IRQ
 * hooked up then we will get prompted to look by an interrupt.
 */
static const struct snd_soc_dapm_widget wm5100_dapm_widgets_noirq[] = {
SND_SOC_DAPM_POST("Post", wm5100_post_ev),
};

static const struct snd_soc_dapm_route wm5100_dapm_routes[] = {
	{ "CP1", NULL, "CPVDD" },
	{ "CP2 Active", NULL, "CPVDD" },

	{ "IN1L", NULL, "SYSCLK" },
	{ "IN1R", NULL, "SYSCLK" },
	{ "IN2L", NULL, "SYSCLK" },
	{ "IN2R", NULL, "SYSCLK" },
	{ "IN3L", NULL, "SYSCLK" },
	{ "IN3R", NULL, "SYSCLK" },
	{ "IN4L", NULL, "SYSCLK" },
	{ "IN4R", NULL, "SYSCLK" },

	{ "OUT1L", NULL, "SYSCLK" },
	{ "OUT1R", NULL, "SYSCLK" },
	{ "OUT2L", NULL, "SYSCLK" },
	{ "OUT2R", NULL, "SYSCLK" },
	{ "OUT3L", NULL, "SYSCLK" },
	{ "OUT3R", NULL, "SYSCLK" },
	{ "OUT4L", NULL, "SYSCLK" },
	{ "OUT4R", NULL, "SYSCLK" },
	{ "OUT5L", NULL, "SYSCLK" },
	{ "OUT5R", NULL, "SYSCLK" },
	{ "OUT6L", NULL, "SYSCLK" },
	{ "OUT6R", NULL, "SYSCLK" },

	{ "AIF1RX1", NULL, "SYSCLK" },
	{ "AIF1RX2", NULL, "SYSCLK" },
	{ "AIF1RX3", NULL, "SYSCLK" },
	{ "AIF1RX4", NULL, "SYSCLK" },
	{ "AIF1RX5", NULL, "SYSCLK" },
	{ "AIF1RX6", NULL, "SYSCLK" },
	{ "AIF1RX7", NULL, "SYSCLK" },
	{ "AIF1RX8", NULL, "SYSCLK" },

	{ "AIF2RX1", NULL, "SYSCLK" },
	{ "AIF2RX1", NULL, "DBVDD2" },
	{ "AIF2RX2", NULL, "SYSCLK" },
	{ "AIF2RX2", NULL, "DBVDD2" },

	{ "AIF3RX1", NULL, "SYSCLK" },
	{ "AIF3RX1", NULL, "DBVDD3" },
	{ "AIF3RX2", NULL, "SYSCLK" },
	{ "AIF3RX2", NULL, "DBVDD3" },

	{ "AIF1TX1", NULL, "SYSCLK" },
	{ "AIF1TX2", NULL, "SYSCLK" },
	{ "AIF1TX3", NULL, "SYSCLK" },
	{ "AIF1TX4", NULL, "SYSCLK" },
	{ "AIF1TX5", NULL, "SYSCLK" },
	{ "AIF1TX6", NULL, "SYSCLK" },
	{ "AIF1TX7", NULL, "SYSCLK" },
	{ "AIF1TX8", NULL, "SYSCLK" },

	{ "AIF2TX1", NULL, "SYSCLK" },
	{ "AIF2TX1", NULL, "DBVDD2" },
	{ "AIF2TX2", NULL, "SYSCLK" },
	{ "AIF2TX2", NULL, "DBVDD2" },

	{ "AIF3TX1", NULL, "SYSCLK" },
	{ "AIF3TX1", NULL, "DBVDD3" },
	{ "AIF3TX2", NULL, "SYSCLK" },
	{ "AIF3TX2", NULL, "DBVDD3" },

	{ "MICBIAS1", NULL, "CP2" },
	{ "MICBIAS2", NULL, "CP2" },
	{ "MICBIAS3", NULL, "CP2" },

	{ "IN1L PGA", NULL, "CP2" },
	{ "IN1R PGA", NULL, "CP2" },
	{ "IN2L PGA", NULL, "CP2" },
	{ "IN2R PGA", NULL, "CP2" },
	{ "IN3L PGA", NULL, "CP2" },
	{ "IN3R PGA", NULL, "CP2" },
	{ "IN4L PGA", NULL, "CP2" },
	{ "IN4R PGA", NULL, "CP2" },

	{ "IN1L PGA", NULL, "CP2 Active" },
	{ "IN1R PGA", NULL, "CP2 Active" },
	{ "IN2L PGA", NULL, "CP2 Active" },
	{ "IN2R PGA", NULL, "CP2 Active" },
	{ "IN3L PGA", NULL, "CP2 Active" },
	{ "IN3R PGA", NULL, "CP2 Active" },
	{ "IN4L PGA", NULL, "CP2 Active" },
	{ "IN4R PGA", NULL, "CP2 Active" },

	{ "OUT1L", NULL, "CP1" },
	{ "OUT1R", NULL, "CP1" },
	{ "OUT2L", NULL, "CP1" },
	{ "OUT2R", NULL, "CP1" },
	{ "OUT3L", NULL, "CP1" },
	{ "OUT3R", NULL, "CP1" },

	{ "Tone Generator 1", NULL, "TONE" },
	{ "Tone Generator 2", NULL, "TONE" },

	{ "IN1L PGA", NULL, "IN1L" },
	{ "IN1R PGA", NULL, "IN1R" },
	{ "IN2L PGA", NULL, "IN2L" },
	{ "IN2R PGA", NULL, "IN2R" },
	{ "IN3L PGA", NULL, "IN3L" },
	{ "IN3R PGA", NULL, "IN3R" },
	{ "IN4L PGA", NULL, "IN4L" },
	{ "IN4R PGA", NULL, "IN4R" },

	WM5100_MIXER_ROUTES("OUT1L", "HPOUT1L"),
	WM5100_MIXER_ROUTES("OUT1R", "HPOUT1R"),
	WM5100_MIXER_ROUTES("OUT2L", "HPOUT2L"),
	WM5100_MIXER_ROUTES("OUT2R", "HPOUT2R"),
	WM5100_MIXER_ROUTES("OUT3L", "HPOUT3L"),
	WM5100_MIXER_ROUTES("OUT3R", "HPOUT3R"),

	WM5100_MIXER_ROUTES("OUT4L", "SPKOUTL"),
	WM5100_MIXER_ROUTES("OUT4R", "SPKOUTR"),
	WM5100_MIXER_ROUTES("OUT5L", "SPKDAT1L"),
	WM5100_MIXER_ROUTES("OUT5R", "SPKDAT1R"),
	WM5100_MIXER_ROUTES("OUT6L", "SPKDAT2L"),
	WM5100_MIXER_ROUTES("OUT6R", "SPKDAT2R"),

	WM5100_MIXER_ROUTES("PWM1 Driver", "PWM1"),
	WM5100_MIXER_ROUTES("PWM2 Driver", "PWM2"),

	WM5100_MIXER_ROUTES("AIF1TX1", "AIF1TX1"),
	WM5100_MIXER_ROUTES("AIF1TX2", "AIF1TX2"),
	WM5100_MIXER_ROUTES("AIF1TX3", "AIF1TX3"),
	WM5100_MIXER_ROUTES("AIF1TX4", "AIF1TX4"),
	WM5100_MIXER_ROUTES("AIF1TX5", "AIF1TX5"),
	WM5100_MIXER_ROUTES("AIF1TX6", "AIF1TX6"),
	WM5100_MIXER_ROUTES("AIF1TX7", "AIF1TX7"),
	WM5100_MIXER_ROUTES("AIF1TX8", "AIF1TX8"),

	WM5100_MIXER_ROUTES("AIF2TX1", "AIF2TX1"),
	WM5100_MIXER_ROUTES("AIF2TX2", "AIF2TX2"),

	WM5100_MIXER_ROUTES("AIF3TX1", "AIF3TX1"),
	WM5100_MIXER_ROUTES("AIF3TX2", "AIF3TX2"),

	WM5100_MIXER_ROUTES("EQ1", "EQ1"),
	WM5100_MIXER_ROUTES("EQ2", "EQ2"),
	WM5100_MIXER_ROUTES("EQ3", "EQ3"),
	WM5100_MIXER_ROUTES("EQ4", "EQ4"),

	WM5100_MIXER_ROUTES("DRC1L", "DRC1L"),
	WM5100_MIXER_ROUTES("DRC1R", "DRC1R"),

	WM5100_MIXER_ROUTES("LHPF1", "LHPF1"),
	WM5100_MIXER_ROUTES("LHPF2", "LHPF2"),
	WM5100_MIXER_ROUTES("LHPF3", "LHPF3"),
	WM5100_MIXER_ROUTES("LHPF4", "LHPF4"),

	{ "HPOUT1L", NULL, "OUT1L" },
	{ "HPOUT1R", NULL, "OUT1R" },
	{ "HPOUT2L", NULL, "OUT2L" },
	{ "HPOUT2R", NULL, "OUT2R" },
	{ "HPOUT3L", NULL, "OUT3L" },
	{ "HPOUT3R", NULL, "OUT3R" },
	{ "SPKOUTL", NULL, "OUT4L" },
	{ "SPKOUTR", NULL, "OUT4R" },
	{ "SPKDAT1", NULL, "OUT5L" },
	{ "SPKDAT1", NULL, "OUT5R" },
	{ "SPKDAT2", NULL, "OUT6L" },
	{ "SPKDAT2", NULL, "OUT6R" },
	{ "PWM1", NULL, "PWM1 Driver" },
	{ "PWM2", NULL, "PWM2 Driver" },
};

static const struct reg_default wm5100_reva_patches[] = {
	{ WM5100_AUDIO_IF_1_10, 0 },
	{ WM5100_AUDIO_IF_1_11, 1 },
	{ WM5100_AUDIO_IF_1_12, 2 },
	{ WM5100_AUDIO_IF_1_13, 3 },
	{ WM5100_AUDIO_IF_1_14, 4 },
	{ WM5100_AUDIO_IF_1_15, 5 },
	{ WM5100_AUDIO_IF_1_16, 6 },
	{ WM5100_AUDIO_IF_1_17, 7 },

	{ WM5100_AUDIO_IF_1_18, 0 },
	{ WM5100_AUDIO_IF_1_19, 1 },
	{ WM5100_AUDIO_IF_1_20, 2 },
	{ WM5100_AUDIO_IF_1_21, 3 },
	{ WM5100_AUDIO_IF_1_22, 4 },
	{ WM5100_AUDIO_IF_1_23, 5 },
	{ WM5100_AUDIO_IF_1_24, 6 },
	{ WM5100_AUDIO_IF_1_25, 7 },

	{ WM5100_AUDIO_IF_2_10, 0 },
	{ WM5100_AUDIO_IF_2_11, 1 },

	{ WM5100_AUDIO_IF_2_18, 0 },
	{ WM5100_AUDIO_IF_2_19, 1 },

	{ WM5100_AUDIO_IF_3_10, 0 },
	{ WM5100_AUDIO_IF_3_11, 1 },

	{ WM5100_AUDIO_IF_3_18, 0 },
	{ WM5100_AUDIO_IF_3_19, 1 },
};

static int wm5100_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int lrclk, bclk, mask, base;

	base = dai->driver->base;

	lrclk = 0;
	bclk = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		mask = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		mask = 2;
		break;
	default:
		dev_err(codec->dev, "Unsupported DAI format %d\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk |= WM5100_AIF1TX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= WM5100_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		lrclk |= WM5100_AIF1TX_LRCLK_MSTR;
		bclk |= WM5100_AIF1_BCLK_MSTR;
		break;
	default:
		dev_err(codec->dev, "Unsupported master mode %d\n",
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= WM5100_AIF1_BCLK_INV;
		lrclk |= WM5100_AIF1TX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= WM5100_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk |= WM5100_AIF1TX_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, base + 1, WM5100_AIF1_BCLK_MSTR |
			    WM5100_AIF1_BCLK_INV, bclk);
	snd_soc_update_bits(codec, base + 2, WM5100_AIF1TX_LRCLK_MSTR |
			    WM5100_AIF1TX_LRCLK_INV, lrclk);
	snd_soc_update_bits(codec, base + 3, WM5100_AIF1TX_LRCLK_MSTR |
			    WM5100_AIF1TX_LRCLK_INV, lrclk);
	snd_soc_update_bits(codec, base + 5, WM5100_AIF1_FMT_MASK, mask);

	return 0;
}

#define WM5100_NUM_BCLK_RATES 19

static int wm5100_bclk_rates_dat[WM5100_NUM_BCLK_RATES] = {
	32000,
	48000,
	64000,
	96000,
	128000,
	192000,
	256000,
	384000,
	512000,
	768000,
	1024000,
	1536000,
	2048000,
	3072000,
	4096000,
	6144000,
	8192000,
	12288000,
	24576000,
};

static int wm5100_bclk_rates_cd[WM5100_NUM_BCLK_RATES] = {
	29400,
	44100,
	58800,
	88200,
	117600,
	176400,
	235200,
	352800,
	470400,
	705600,
	940800,
	1411200,
	1881600,
	2882400,
	3763200,
	5644800,
	7526400,
	11289600,
	22579600,
};

static int wm5100_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	bool async = wm5100->aif_async[dai->id];
	int i, base, bclk, aif_rate, lrclk, wl, fl, sr;
	int *bclk_rates;

	base = dai->driver->base;

	/* Data sizes if not using TDM */
	wl = snd_pcm_format_width(params_format(params));
	if (wl < 0)
		return wl;
	fl = snd_soc_params_to_frame_size(params);
	if (fl < 0)
		return fl;

	dev_dbg(codec->dev, "Word length %d bits, frame length %d bits\n",
		wl, fl);

	/* Target BCLK rate */
	bclk = snd_soc_params_to_bclk(params);
	if (bclk < 0)
		return bclk;

	/* Root for BCLK depends on SYS/ASYNCCLK */
	if (!async) {
		aif_rate = wm5100->sysclk;
		sr = wm5100_alloc_sr(codec, params_rate(params));
		if (sr < 0)
			return sr;
	} else {
		/* If we're in ASYNCCLK set the ASYNC sample rate */
		aif_rate = wm5100->asyncclk;
		sr = 3;

		for (i = 0; i < ARRAY_SIZE(wm5100_sr_code); i++)
			if (params_rate(params) == wm5100_sr_code[i])
				break;
		if (i == ARRAY_SIZE(wm5100_sr_code)) {
			dev_err(codec->dev, "Invalid rate %dHzn",
				params_rate(params));
			return -EINVAL;
		}

		/* TODO: We should really check for symmetry */
		snd_soc_update_bits(codec, WM5100_CLOCKING_8,
				    WM5100_ASYNC_SAMPLE_RATE_MASK, i);
	}

	if (!aif_rate) {
		dev_err(codec->dev, "%s has no rate set\n",
			async ? "ASYNCCLK" : "SYSCLK");
		return -EINVAL;
	}

	dev_dbg(codec->dev, "Target BCLK is %dHz, using %dHz %s\n",
		bclk, aif_rate, async ? "ASYNCCLK" : "SYSCLK");

	if (aif_rate % 4000)
		bclk_rates = wm5100_bclk_rates_cd;
	else
		bclk_rates = wm5100_bclk_rates_dat;

	for (i = 0; i < WM5100_NUM_BCLK_RATES; i++)
		if (bclk_rates[i] >= bclk && (bclk_rates[i] % bclk == 0))
			break;
	if (i == WM5100_NUM_BCLK_RATES) {
		dev_err(codec->dev,
			"No valid BCLK for %dHz found from %dHz %s\n",
			bclk, aif_rate, async ? "ASYNCCLK" : "SYSCLK");
		return -EINVAL;
	}

	bclk = i;
	dev_dbg(codec->dev, "Setting %dHz BCLK\n", bclk_rates[bclk]);
	snd_soc_update_bits(codec, base + 1, WM5100_AIF1_BCLK_FREQ_MASK, bclk);

	lrclk = bclk_rates[bclk] / params_rate(params);
	dev_dbg(codec->dev, "Setting %dHz LRCLK\n", bclk_rates[bclk] / lrclk);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
	    wm5100->aif_symmetric[dai->id])
		snd_soc_update_bits(codec, base + 7,
				    WM5100_AIF1RX_BCPF_MASK, lrclk);
	else
		snd_soc_update_bits(codec, base + 6,
				    WM5100_AIF1TX_BCPF_MASK, lrclk);

	i = (wl << WM5100_AIF1TX_WL_SHIFT) | fl;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_update_bits(codec, base + 9,
				    WM5100_AIF1RX_WL_MASK |
				    WM5100_AIF1RX_SLOT_LEN_MASK, i);
	else
		snd_soc_update_bits(codec, base + 8,
				    WM5100_AIF1TX_WL_MASK |
				    WM5100_AIF1TX_SLOT_LEN_MASK, i);

	snd_soc_update_bits(codec, base + 4, WM5100_AIF1_RATE_MASK, sr);

	return 0;
}

static const struct snd_soc_dai_ops wm5100_dai_ops = {
	.set_fmt = wm5100_set_fmt,
	.hw_params = wm5100_hw_params,
};

static int wm5100_set_sysclk(struct snd_soc_codec *codec, int clk_id,
			     int source, unsigned int freq, int dir)
{
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	int *rate_store;
	int fval, audio_rate, ret, reg;

	switch (clk_id) {
	case WM5100_CLK_SYSCLK:
		reg = WM5100_CLOCKING_3;
		rate_store = &wm5100->sysclk;
		break;
	case WM5100_CLK_ASYNCCLK:
		reg = WM5100_CLOCKING_7;
		rate_store = &wm5100->asyncclk;
		break;
	case WM5100_CLK_32KHZ:
		/* The 32kHz clock is slightly different to the others */
		switch (source) {
		case WM5100_CLKSRC_MCLK1:
		case WM5100_CLKSRC_MCLK2:
		case WM5100_CLKSRC_SYSCLK:
			snd_soc_update_bits(codec, WM5100_CLOCKING_1,
					    WM5100_CLK_32K_SRC_MASK,
					    source);
			break;
		default:
			return -EINVAL;
		}
		return 0;

	case WM5100_CLK_AIF1:
	case WM5100_CLK_AIF2:
	case WM5100_CLK_AIF3:
		/* Not real clocks, record which clock domain they're in */
		switch (source) {
		case WM5100_CLKSRC_SYSCLK:
			wm5100->aif_async[clk_id - 1] = false;
			break;
		case WM5100_CLKSRC_ASYNCCLK:
			wm5100->aif_async[clk_id - 1] = true;
			break;
		default:
			dev_err(codec->dev, "Invalid source %d\n", source);
			return -EINVAL;
		}	
		return 0;

	case WM5100_CLK_OPCLK:
		switch (freq) {
		case 5644800:
		case 6144000:
			snd_soc_update_bits(codec, WM5100_MISC_GPIO_1,
					    WM5100_OPCLK_SEL_MASK, 0);
			break;
		case 11289600:
		case 12288000:
			snd_soc_update_bits(codec, WM5100_MISC_GPIO_1,
					    WM5100_OPCLK_SEL_MASK, 0);
			break;
		case 22579200:
		case 24576000:
			snd_soc_update_bits(codec, WM5100_MISC_GPIO_1,
					    WM5100_OPCLK_SEL_MASK, 0);
			break;
		default:
			dev_err(codec->dev, "Unsupported OPCLK %dHz\n",
				freq);
			return -EINVAL;
		}
		return 0;

	default:
		dev_err(codec->dev, "Unknown clock %d\n", clk_id);
		return -EINVAL;
	}

	switch (source) {
	case WM5100_CLKSRC_SYSCLK:
	case WM5100_CLKSRC_ASYNCCLK:
		dev_err(codec->dev, "Invalid source %d\n", source);
		return -EINVAL;
	}

	switch (freq) {
	case 5644800:
	case 6144000:
		fval = 0;
		break;
	case 11289600:
	case 12288000:
		fval = 1;
		break;
	case 22579200:
	case 24576000:
		fval = 2;
		break;
	default:
		dev_err(codec->dev, "Invalid clock rate: %d\n", freq);
		return -EINVAL;
	}

	switch (freq) {
	case 5644800:
	case 11289600:
	case 22579200:
		audio_rate = 44100;
		break;

	case 6144000:
	case 12288000:
	case 24576000:
		audio_rate = 48000;
		break;

	default:
		BUG();
		audio_rate = 0;
		break;
	}

	/* TODO: Check if MCLKs are in use and enable/disable pulls to
	 * match.
	 */

	snd_soc_update_bits(codec, reg, WM5100_SYSCLK_FREQ_MASK |
			    WM5100_SYSCLK_SRC_MASK,
			    fval << WM5100_SYSCLK_FREQ_SHIFT | source);

	/* If this is SYSCLK then configure the clock rate for the
	 * internal audio functions to the natural sample rate for
	 * this clock rate.
	 */
	if (clk_id == WM5100_CLK_SYSCLK) {
		dev_dbg(codec->dev, "Setting primary audio rate to %dHz",
			audio_rate);
		if (0 && *rate_store)
			wm5100_free_sr(codec, audio_rate);
		ret = wm5100_alloc_sr(codec, audio_rate);
		if (ret != 0)
			dev_warn(codec->dev, "Primary audio slot is %d\n",
				 ret);
	}

	*rate_store = freq;

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_refclk_div;
	u16 n;
	u16 theta;
	u16 lambda;
};

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	unsigned int target;
	unsigned int div;
	unsigned int fratio, gcd_fll;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_refclk_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_refclk_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("FLL Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 2;
	while (Fout * div < 90000000) {
		div++;
		if (div > 64) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	fll_div->fll_outdiv = div - 1;

	pr_debug("FLL Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			fratio = fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	fll_div->n = target / (fratio * Fref);

	if (target % Fref == 0) {
		fll_div->theta = 0;
		fll_div->lambda = 0;
	} else {
		gcd_fll = gcd(target, fratio * Fref);

		fll_div->theta = (target - (fll_div->n * fratio * Fref))
			/ gcd_fll;
		fll_div->lambda = (fratio * Fref) / gcd_fll;
	}

	pr_debug("FLL N=%x THETA=%x LAMBDA=%x\n",
		 fll_div->n, fll_div->theta, fll_div->lambda);
	pr_debug("FLL_FRATIO=%x(%d) FLL_OUTDIV=%x FLL_REFCLK_DIV=%x\n",
		 fll_div->fll_fratio, fratio, fll_div->fll_outdiv,
		 fll_div->fll_refclk_div);

	return 0;
}

static int wm5100_set_fll(struct snd_soc_codec *codec, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div factors;
	struct wm5100_fll *fll;
	int ret, base, lock, i, timeout;

	switch (fll_id) {
	case WM5100_FLL1:
		fll = &wm5100->fll[0];
		base = WM5100_FLL1_CONTROL_1 - 1;
		lock = WM5100_FLL1_LOCK_STS;
		break;
	case WM5100_FLL2:
		fll = &wm5100->fll[1];
		base = WM5100_FLL2_CONTROL_2 - 1;
		lock = WM5100_FLL2_LOCK_STS;
		break;
	default:
		dev_err(codec->dev, "Unknown FLL %d\n",fll_id);
		return -EINVAL;
	}

	if (!Fout) {
		dev_dbg(codec->dev, "FLL%d disabled", fll_id);
		if (fll->fout)
			pm_runtime_put(codec->dev);
		fll->fout = 0;
		snd_soc_update_bits(codec, base + 1, WM5100_FLL1_ENA, 0);
		return 0;
	}

	switch (source) {
	case WM5100_FLL_SRC_MCLK1:
	case WM5100_FLL_SRC_MCLK2:
	case WM5100_FLL_SRC_FLL1:
	case WM5100_FLL_SRC_FLL2:
	case WM5100_FLL_SRC_AIF1BCLK:
	case WM5100_FLL_SRC_AIF2BCLK:
	case WM5100_FLL_SRC_AIF3BCLK:
		break;
	default:
		dev_err(codec->dev, "Invalid FLL source %d\n", source);
		return -EINVAL;
	}

	ret = fll_factors(&factors, Fref, Fout);
	if (ret < 0)
		return ret;

	/* Disable the FLL while we reconfigure */
	snd_soc_update_bits(codec, base + 1, WM5100_FLL1_ENA, 0);

	snd_soc_update_bits(codec, base + 2,
			    WM5100_FLL1_OUTDIV_MASK | WM5100_FLL1_FRATIO_MASK,
			    (factors.fll_outdiv << WM5100_FLL1_OUTDIV_SHIFT) |
			    factors.fll_fratio);
	snd_soc_update_bits(codec, base + 3, WM5100_FLL1_THETA_MASK,
			    factors.theta);
	snd_soc_update_bits(codec, base + 5, WM5100_FLL1_N_MASK, factors.n);
	snd_soc_update_bits(codec, base + 6,
			    WM5100_FLL1_REFCLK_DIV_MASK |
			    WM5100_FLL1_REFCLK_SRC_MASK,
			    (factors.fll_refclk_div
			     << WM5100_FLL1_REFCLK_DIV_SHIFT) | source);
	snd_soc_update_bits(codec, base + 7, WM5100_FLL1_LAMBDA_MASK,
			    factors.lambda);

	/* Clear any pending completions */
	try_wait_for_completion(&fll->lock);

	pm_runtime_get_sync(codec->dev);

	snd_soc_update_bits(codec, base + 1, WM5100_FLL1_ENA, WM5100_FLL1_ENA);

	if (i2c->irq)
		timeout = 2;
	else
		timeout = 50;

	snd_soc_update_bits(codec, WM5100_CLOCKING_3, WM5100_SYSCLK_ENA,
			    WM5100_SYSCLK_ENA);

	/* Poll for the lock; will use interrupt when we can test */
	for (i = 0; i < timeout; i++) {
		if (i2c->irq) {
			ret = wait_for_completion_timeout(&fll->lock,
							  msecs_to_jiffies(25));
			if (ret > 0)
				break;
		} else {
			msleep(1);
		}

		ret = snd_soc_read(codec,
				   WM5100_INTERRUPT_RAW_STATUS_3);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to read FLL status: %d\n",
				ret);
			continue;
		}
		if (ret & lock)
			break;
	}
	if (i == timeout) {
		dev_err(codec->dev, "FLL%d lock timed out\n", fll_id);
		pm_runtime_put(codec->dev);
		return -ETIMEDOUT;
	}

	fll->src = source;
	fll->fref = Fref;
	fll->fout = Fout;

	dev_dbg(codec->dev, "FLL%d running %dHz->%dHz\n", fll_id,
		Fref, Fout);

	return 0;
}

/* Actually go much higher */
#define WM5100_RATES SNDRV_PCM_RATE_8000_192000

#define WM5100_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm5100_dai[] = {
	{
		.name = "wm5100-aif1",
		.base = WM5100_AUDIO_IF_1_1 - 1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM5100_RATES,
			.formats = WM5100_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF1 Capture",
			 .channels_min = 2,
			 .channels_max = 2,
			 .rates = WM5100_RATES,
			 .formats = WM5100_FORMATS,
		 },
		.ops = &wm5100_dai_ops,
	},
	{
		.name = "wm5100-aif2",
		.id = 1,
		.base = WM5100_AUDIO_IF_2_1 - 1,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM5100_RATES,
			.formats = WM5100_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF2 Capture",
			 .channels_min = 2,
			 .channels_max = 2,
			 .rates = WM5100_RATES,
			 .formats = WM5100_FORMATS,
		 },
		.ops = &wm5100_dai_ops,
	},
	{
		.name = "wm5100-aif3",
		.id = 2,
		.base = WM5100_AUDIO_IF_3_1 - 1,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM5100_RATES,
			.formats = WM5100_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF3 Capture",
			 .channels_min = 2,
			 .channels_max = 2,
			 .rates = WM5100_RATES,
			 .formats = WM5100_FORMATS,
		 },
		.ops = &wm5100_dai_ops,
	},
};

static int wm5100_dig_vu[] = {
	WM5100_ADC_DIGITAL_VOLUME_1L,
	WM5100_ADC_DIGITAL_VOLUME_1R,
	WM5100_ADC_DIGITAL_VOLUME_2L,
	WM5100_ADC_DIGITAL_VOLUME_2R,
	WM5100_ADC_DIGITAL_VOLUME_3L,
	WM5100_ADC_DIGITAL_VOLUME_3R,
	WM5100_ADC_DIGITAL_VOLUME_4L,
	WM5100_ADC_DIGITAL_VOLUME_4R,

	WM5100_DAC_DIGITAL_VOLUME_1L,
	WM5100_DAC_DIGITAL_VOLUME_1R,
	WM5100_DAC_DIGITAL_VOLUME_2L,
	WM5100_DAC_DIGITAL_VOLUME_2R,
	WM5100_DAC_DIGITAL_VOLUME_3L,
	WM5100_DAC_DIGITAL_VOLUME_3R,
	WM5100_DAC_DIGITAL_VOLUME_4L,
	WM5100_DAC_DIGITAL_VOLUME_4R,
	WM5100_DAC_DIGITAL_VOLUME_5L,
	WM5100_DAC_DIGITAL_VOLUME_5R,
	WM5100_DAC_DIGITAL_VOLUME_6L,
	WM5100_DAC_DIGITAL_VOLUME_6R,
};

static void wm5100_set_detect_mode(struct wm5100_priv *wm5100, int the_mode)
{
	struct wm5100_jack_mode *mode = &wm5100->pdata.jack_modes[the_mode];

	if (WARN_ON(the_mode >= ARRAY_SIZE(wm5100->pdata.jack_modes)))
		return;

	gpio_set_value_cansleep(wm5100->pdata.hp_pol, mode->hp_pol);
	regmap_update_bits(wm5100->regmap, WM5100_ACCESSORY_DETECT_MODE_1,
			   WM5100_ACCDET_BIAS_SRC_MASK |
			   WM5100_ACCDET_SRC,
			   (mode->bias << WM5100_ACCDET_BIAS_SRC_SHIFT) |
			   mode->micd_src << WM5100_ACCDET_SRC_SHIFT);
	regmap_update_bits(wm5100->regmap, WM5100_MISC_CONTROL,
			   WM5100_HPCOM_SRC,
			   mode->micd_src << WM5100_HPCOM_SRC_SHIFT);

	wm5100->jack_mode = the_mode;

	dev_dbg(wm5100->dev, "Set microphone polarity to %d\n",
		wm5100->jack_mode);
}

static void wm5100_report_headphone(struct wm5100_priv *wm5100)
{
	dev_dbg(wm5100->dev, "Headphone detected\n");
	wm5100->jack_detecting = false;
	snd_soc_jack_report(wm5100->jack, SND_JACK_HEADPHONE,
			    SND_JACK_HEADPHONE);

	/* Increase the detection rate a bit for responsiveness. */
	regmap_update_bits(wm5100->regmap, WM5100_MIC_DETECT_1,
			   WM5100_ACCDET_RATE_MASK,
			   7 << WM5100_ACCDET_RATE_SHIFT);
}

static void wm5100_micd_irq(struct wm5100_priv *wm5100)
{
	unsigned int val;
	int ret;

	ret = regmap_read(wm5100->regmap, WM5100_MIC_DETECT_3, &val);
	if (ret != 0) {
		dev_err(wm5100->dev, "Failed to read micropone status: %d\n",
			ret);
		return;
	}

	dev_dbg(wm5100->dev, "Microphone event: %x\n", val);

	if (!(val & WM5100_ACCDET_VALID)) {
		dev_warn(wm5100->dev, "Microphone detection state invalid\n");
		return;
	}

	/* No accessory, reset everything and report removal */
	if (!(val & WM5100_ACCDET_STS)) {
		dev_dbg(wm5100->dev, "Jack removal detected\n");
		wm5100->jack_mic = false;
		wm5100->jack_detecting = true;
		wm5100->jack_flips = 0;
		snd_soc_jack_report(wm5100->jack, 0,
				    SND_JACK_LINEOUT | SND_JACK_HEADSET |
				    SND_JACK_BTN_0);

		regmap_update_bits(wm5100->regmap, WM5100_MIC_DETECT_1,
				   WM5100_ACCDET_RATE_MASK,
				   WM5100_ACCDET_RATE_MASK);
		return;
	}

	/* If the measurement is very high we've got a microphone,
	 * either we just detected one or if we already reported then
	 * we've got a button release event.
	 */
	if (val & 0x400) {
		if (wm5100->jack_detecting) {
			dev_dbg(wm5100->dev, "Microphone detected\n");
			wm5100->jack_mic = true;
			wm5100->jack_detecting = false;
			snd_soc_jack_report(wm5100->jack,
					    SND_JACK_HEADSET,
					    SND_JACK_HEADSET | SND_JACK_BTN_0);

			/* Increase poll rate to give better responsiveness
			 * for buttons */
			regmap_update_bits(wm5100->regmap, WM5100_MIC_DETECT_1,
					   WM5100_ACCDET_RATE_MASK,
					   5 << WM5100_ACCDET_RATE_SHIFT);
		} else {
			dev_dbg(wm5100->dev, "Mic button up\n");
			snd_soc_jack_report(wm5100->jack, 0, SND_JACK_BTN_0);
		}

		return;
	}

	/* If we detected a lower impedence during initial startup
	 * then we probably have the wrong polarity, flip it.  Don't
	 * do this for the lowest impedences to speed up detection of
	 * plain headphones and give up if neither polarity looks
	 * sensible.
	 */
	if (wm5100->jack_detecting && (val & 0x3f8)) {
		wm5100->jack_flips++;

		if (wm5100->jack_flips > 1)
			wm5100_report_headphone(wm5100);
		else
			wm5100_set_detect_mode(wm5100, !wm5100->jack_mode);

		return;
	}

	/* Don't distinguish between buttons, just report any low
	 * impedence as BTN_0.
	 */
	if (val & 0x3fc) {
		if (wm5100->jack_mic) {
			dev_dbg(wm5100->dev, "Mic button detected\n");
			snd_soc_jack_report(wm5100->jack, SND_JACK_BTN_0,
					    SND_JACK_BTN_0);
		} else if (wm5100->jack_detecting) {
			wm5100_report_headphone(wm5100);
		}
	}
}

int wm5100_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack)
{
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	if (jack) {
		wm5100->jack = jack;
		wm5100->jack_detecting = true;
		wm5100->jack_flips = 0;

		wm5100_set_detect_mode(wm5100, 0);

		/* Slowest detection rate, gives debounce for initial
		 * detection */
		snd_soc_update_bits(codec, WM5100_MIC_DETECT_1,
				    WM5100_ACCDET_BIAS_STARTTIME_MASK |
				    WM5100_ACCDET_RATE_MASK,
				    (7 << WM5100_ACCDET_BIAS_STARTTIME_SHIFT) |
				    WM5100_ACCDET_RATE_MASK);

		/* We need the charge pump to power MICBIAS */
		snd_soc_dapm_mutex_lock(dapm);

		snd_soc_dapm_force_enable_pin_unlocked(dapm, "CP2");
		snd_soc_dapm_force_enable_pin_unlocked(dapm, "SYSCLK");

		snd_soc_dapm_sync_unlocked(dapm);

		snd_soc_dapm_mutex_unlock(dapm);

		/* We start off just enabling microphone detection - even a
		 * plain headphone will trigger detection.
		 */
		snd_soc_update_bits(codec, WM5100_MIC_DETECT_1,
				    WM5100_ACCDET_ENA, WM5100_ACCDET_ENA);

		snd_soc_update_bits(codec, WM5100_INTERRUPT_STATUS_3_MASK,
				    WM5100_IM_ACCDET_EINT, 0);
	} else {
		snd_soc_update_bits(codec, WM5100_INTERRUPT_STATUS_3_MASK,
				    WM5100_IM_HPDET_EINT |
				    WM5100_IM_ACCDET_EINT,
				    WM5100_IM_HPDET_EINT |
				    WM5100_IM_ACCDET_EINT);
		snd_soc_update_bits(codec, WM5100_MIC_DETECT_1,
				    WM5100_ACCDET_ENA, 0);
		wm5100->jack = NULL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm5100_detect);

static irqreturn_t wm5100_irq(int irq, void *data)
{
	struct wm5100_priv *wm5100 = data;
	irqreturn_t status = IRQ_NONE;
	unsigned int irq_val, mask_val;
	int ret;

	ret = regmap_read(wm5100->regmap, WM5100_INTERRUPT_STATUS_3, &irq_val);
	if (ret < 0) {
		dev_err(wm5100->dev, "Failed to read IRQ status 3: %d\n",
			ret);
		irq_val = 0;
	}

	ret = regmap_read(wm5100->regmap, WM5100_INTERRUPT_STATUS_3_MASK,
			  &mask_val);
	if (ret < 0) {
		dev_err(wm5100->dev, "Failed to read IRQ mask 3: %d\n",
			ret);
		mask_val = 0xffff;
	}

	irq_val &= ~mask_val;

	regmap_write(wm5100->regmap, WM5100_INTERRUPT_STATUS_3, irq_val);

	if (irq_val)
		status = IRQ_HANDLED;

	wm5100_log_status3(wm5100, irq_val);

	if (irq_val & WM5100_FLL1_LOCK_EINT) {
		dev_dbg(wm5100->dev, "FLL1 locked\n");
		complete(&wm5100->fll[0].lock);
	}
	if (irq_val & WM5100_FLL2_LOCK_EINT) {
		dev_dbg(wm5100->dev, "FLL2 locked\n");
		complete(&wm5100->fll[1].lock);
	}

	if (irq_val & WM5100_ACCDET_EINT)
		wm5100_micd_irq(wm5100);

	ret = regmap_read(wm5100->regmap, WM5100_INTERRUPT_STATUS_4, &irq_val);
	if (ret < 0) {
		dev_err(wm5100->dev, "Failed to read IRQ status 4: %d\n",
			ret);
		irq_val = 0;
	}

	ret = regmap_read(wm5100->regmap, WM5100_INTERRUPT_STATUS_4_MASK,
			  &mask_val);
	if (ret < 0) {
		dev_err(wm5100->dev, "Failed to read IRQ mask 4: %d\n",
			ret);
		mask_val = 0xffff;
	}

	irq_val &= ~mask_val;

	if (irq_val)
		status = IRQ_HANDLED;

	regmap_write(wm5100->regmap, WM5100_INTERRUPT_STATUS_4, irq_val);

	wm5100_log_status4(wm5100, irq_val);

	return status;
}

static irqreturn_t wm5100_edge_irq(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;
	irqreturn_t val;

	do {
		val = wm5100_irq(irq, data);
		if (val != IRQ_NONE)
			ret = val;
	} while (val != IRQ_NONE);

	return ret;
}

#ifdef CONFIG_GPIOLIB
static inline struct wm5100_priv *gpio_to_wm5100(struct gpio_chip *chip)
{
	return container_of(chip, struct wm5100_priv, gpio_chip);
}

static void wm5100_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm5100_priv *wm5100 = gpio_to_wm5100(chip);

	regmap_update_bits(wm5100->regmap, WM5100_GPIO_CTRL_1 + offset,
			   WM5100_GP1_LVL, !!value << WM5100_GP1_LVL_SHIFT);
}

static int wm5100_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm5100_priv *wm5100 = gpio_to_wm5100(chip);
	int val, ret;

	val = (1 << WM5100_GP1_FN_SHIFT) | (!!value << WM5100_GP1_LVL_SHIFT);

	ret = regmap_update_bits(wm5100->regmap, WM5100_GPIO_CTRL_1 + offset,
				 WM5100_GP1_FN_MASK | WM5100_GP1_DIR |
				 WM5100_GP1_LVL, val);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static int wm5100_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm5100_priv *wm5100 = gpio_to_wm5100(chip);
	unsigned int reg;
	int ret;

	ret = regmap_read(wm5100->regmap, WM5100_GPIO_CTRL_1 + offset, &reg);
	if (ret < 0)
		return ret;

	return (reg & WM5100_GP1_LVL) != 0;
}

static int wm5100_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm5100_priv *wm5100 = gpio_to_wm5100(chip);

	return regmap_update_bits(wm5100->regmap, WM5100_GPIO_CTRL_1 + offset,
				  WM5100_GP1_FN_MASK | WM5100_GP1_DIR,
				  (1 << WM5100_GP1_FN_SHIFT) |
				  (1 << WM5100_GP1_DIR_SHIFT));
}

static struct gpio_chip wm5100_template_chip = {
	.label			= "wm5100",
	.owner			= THIS_MODULE,
	.direction_output	= wm5100_gpio_direction_out,
	.set			= wm5100_gpio_set,
	.direction_input	= wm5100_gpio_direction_in,
	.get			= wm5100_gpio_get,
	.can_sleep		= 1,
};

static void wm5100_init_gpio(struct i2c_client *i2c)
{
	struct wm5100_priv *wm5100 = i2c_get_clientdata(i2c);
	int ret;

	wm5100->gpio_chip = wm5100_template_chip;
	wm5100->gpio_chip.ngpio = 6;
	wm5100->gpio_chip.dev = &i2c->dev;

	if (wm5100->pdata.gpio_base)
		wm5100->gpio_chip.base = wm5100->pdata.gpio_base;
	else
		wm5100->gpio_chip.base = -1;

	ret = gpiochip_add(&wm5100->gpio_chip);
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm5100_free_gpio(struct i2c_client *i2c)
{
	struct wm5100_priv *wm5100 = i2c_get_clientdata(i2c);

	gpiochip_remove(&wm5100->gpio_chip);
}
#else
static void wm5100_init_gpio(struct i2c_client *i2c)
{
}

static void wm5100_free_gpio(struct i2c_client *i2c)
{
}
#endif

static int wm5100_probe(struct snd_soc_codec *codec)
{
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);
	int ret, i;

	wm5100->codec = codec;

	for (i = 0; i < ARRAY_SIZE(wm5100_dig_vu); i++)
		snd_soc_update_bits(codec, wm5100_dig_vu[i], WM5100_OUT_VU,
				    WM5100_OUT_VU);

	/* Don't debounce interrupts to support use of SYSCLK only */
	snd_soc_write(codec, WM5100_IRQ_DEBOUNCE_1, 0);
	snd_soc_write(codec, WM5100_IRQ_DEBOUNCE_2, 0);

	/* TODO: check if we're symmetric */

	if (i2c->irq)
		snd_soc_dapm_new_controls(&codec->dapm,
					  wm5100_dapm_widgets_noirq,
					  ARRAY_SIZE(wm5100_dapm_widgets_noirq));

	if (wm5100->pdata.hp_pol) {
		ret = gpio_request_one(wm5100->pdata.hp_pol,
				       GPIOF_OUT_INIT_HIGH, "WM5100 HP_POL");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request HP_POL %d: %d\n",
				wm5100->pdata.hp_pol, ret);
			goto err_gpio;
		}
	}

	return 0;

err_gpio:

	return ret;
}

static int wm5100_remove(struct snd_soc_codec *codec)
{
	struct wm5100_priv *wm5100 = snd_soc_codec_get_drvdata(codec);

	if (wm5100->pdata.hp_pol) {
		gpio_free(wm5100->pdata.hp_pol);
	}

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm5100 = {
	.probe =	wm5100_probe,
	.remove =	wm5100_remove,

	.set_sysclk = wm5100_set_sysclk,
	.set_pll = wm5100_set_fll,
	.idle_bias_off = 1,

	.seq_notifier = wm5100_seq_notifier,
	.controls = wm5100_snd_controls,
	.num_controls = ARRAY_SIZE(wm5100_snd_controls),
	.dapm_widgets = wm5100_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm5100_dapm_widgets),
	.dapm_routes = wm5100_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm5100_dapm_routes),
};

static const struct regmap_config wm5100_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = WM5100_MAX_REGISTER,
	.reg_defaults = wm5100_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm5100_reg_defaults),
	.volatile_reg = wm5100_volatile_register,
	.readable_reg = wm5100_readable_register,
	.cache_type = REGCACHE_RBTREE,
};

static const unsigned int wm5100_mic_ctrl_reg[] = {
	WM5100_IN1L_CONTROL,
	WM5100_IN2L_CONTROL,
	WM5100_IN3L_CONTROL,
	WM5100_IN4L_CONTROL,
};

static int wm5100_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm5100_pdata *pdata = dev_get_platdata(&i2c->dev);
	struct wm5100_priv *wm5100;
	unsigned int reg;
	int ret, i, irq_flags;

	wm5100 = devm_kzalloc(&i2c->dev, sizeof(struct wm5100_priv),
			      GFP_KERNEL);
	if (wm5100 == NULL)
		return -ENOMEM;

	wm5100->dev = &i2c->dev;

	wm5100->regmap = devm_regmap_init_i2c(i2c, &wm5100_regmap);
	if (IS_ERR(wm5100->regmap)) {
		ret = PTR_ERR(wm5100->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm5100->fll); i++)
		init_completion(&wm5100->fll[i].lock);

	if (pdata)
		wm5100->pdata = *pdata;

	i2c_set_clientdata(i2c, wm5100);

	for (i = 0; i < ARRAY_SIZE(wm5100->core_supplies); i++)
		wm5100->core_supplies[i].supply = wm5100_core_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev,
				      ARRAY_SIZE(wm5100->core_supplies),
				      wm5100->core_supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request core supplies: %d\n",
			ret);
		goto err;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm5100->core_supplies),
				    wm5100->core_supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable core supplies: %d\n",
			ret);
		goto err;
	}

	if (wm5100->pdata.ldo_ena) {
		ret = gpio_request_one(wm5100->pdata.ldo_ena,
				       GPIOF_OUT_INIT_HIGH, "WM5100 LDOENA");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request LDOENA %d: %d\n",
				wm5100->pdata.ldo_ena, ret);
			goto err_enable;
		}
		msleep(2);
	}

	if (wm5100->pdata.reset) {
		ret = gpio_request_one(wm5100->pdata.reset,
				       GPIOF_OUT_INIT_HIGH, "WM5100 /RESET");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request /RESET %d: %d\n",
				wm5100->pdata.reset, ret);
			goto err_ldo;
		}
	}

	ret = regmap_read(wm5100->regmap, WM5100_SOFTWARE_RESET, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register: %d\n", ret);
		goto err_reset;
	}
	switch (reg) {
	case 0x8997:
	case 0x5100:
		break;

	default:
		dev_err(&i2c->dev, "Device is not a WM5100, ID is %x\n", reg);
		ret = -EINVAL;
		goto err_reset;
	}

	ret = regmap_read(wm5100->regmap, WM5100_DEVICE_REVISION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read revision register\n");
		goto err_reset;
	}
	wm5100->rev = reg & WM5100_DEVICE_REVISION_MASK;

	dev_info(&i2c->dev, "revision %c\n", wm5100->rev + 'A');

	ret = wm5100_reset(wm5100);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to issue reset\n");
		goto err_reset;
	}

	switch (wm5100->rev) {
	case 0:
		ret = regmap_register_patch(wm5100->regmap,
					    wm5100_reva_patches,
					    ARRAY_SIZE(wm5100_reva_patches));
		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to register patches: %d\n",
				ret);
			goto err_reset;
		}
		break;
	default:
		break;
	}


	wm5100_init_gpio(i2c);

	for (i = 0; i < ARRAY_SIZE(wm5100->pdata.gpio_defaults); i++) {
		if (!wm5100->pdata.gpio_defaults[i])
			continue;

		regmap_write(wm5100->regmap, WM5100_GPIO_CTRL_1 + i,
			     wm5100->pdata.gpio_defaults[i]);
	}

	for (i = 0; i < ARRAY_SIZE(wm5100->pdata.in_mode); i++) {
		regmap_update_bits(wm5100->regmap, wm5100_mic_ctrl_reg[i],
				   WM5100_IN1_MODE_MASK |
				   WM5100_IN1_DMIC_SUP_MASK,
				   (wm5100->pdata.in_mode[i] <<
				    WM5100_IN1_MODE_SHIFT) |
				   (wm5100->pdata.dmic_sup[i] <<
				    WM5100_IN1_DMIC_SUP_SHIFT));
	}

	if (i2c->irq) {
		if (wm5100->pdata.irq_flags)
			irq_flags = wm5100->pdata.irq_flags;
		else
			irq_flags = IRQF_TRIGGER_LOW;

		irq_flags |= IRQF_ONESHOT;

		if (irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING))
			ret = request_threaded_irq(i2c->irq, NULL,
						   wm5100_edge_irq, irq_flags,
						   "wm5100", wm5100);
		else
			ret = request_threaded_irq(i2c->irq, NULL, wm5100_irq,
						   irq_flags, "wm5100",
						   wm5100);

		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
				i2c->irq, ret);
		} else {
			/* Enable default interrupts */
			regmap_update_bits(wm5100->regmap,
					   WM5100_INTERRUPT_STATUS_3_MASK,
					   WM5100_IM_SPK_SHUTDOWN_WARN_EINT |
					   WM5100_IM_SPK_SHUTDOWN_EINT |
					   WM5100_IM_ASRC2_LOCK_EINT |
					   WM5100_IM_ASRC1_LOCK_EINT |
					   WM5100_IM_FLL2_LOCK_EINT |
					   WM5100_IM_FLL1_LOCK_EINT |
					   WM5100_CLKGEN_ERR_EINT |
					   WM5100_CLKGEN_ERR_ASYNC_EINT, 0);

			regmap_update_bits(wm5100->regmap,
					   WM5100_INTERRUPT_STATUS_4_MASK,
					   WM5100_AIF3_ERR_EINT |
					   WM5100_AIF2_ERR_EINT |
					   WM5100_AIF1_ERR_EINT |
					   WM5100_CTRLIF_ERR_EINT |
					   WM5100_ISRC2_UNDERCLOCKED_EINT |
					   WM5100_ISRC1_UNDERCLOCKED_EINT |
					   WM5100_FX_UNDERCLOCKED_EINT |
					   WM5100_AIF3_UNDERCLOCKED_EINT |
					   WM5100_AIF2_UNDERCLOCKED_EINT |
					   WM5100_AIF1_UNDERCLOCKED_EINT |
					   WM5100_ASRC_UNDERCLOCKED_EINT |
					   WM5100_DAC_UNDERCLOCKED_EINT |
					   WM5100_ADC_UNDERCLOCKED_EINT |
					   WM5100_MIXER_UNDERCLOCKED_EINT, 0);
		}
	}

	pm_runtime_set_active(&i2c->dev);
	pm_runtime_enable(&i2c->dev);
	pm_request_idle(&i2c->dev);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm5100, wm5100_dai,
				     ARRAY_SIZE(wm5100_dai));
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to register WM5100: %d\n", ret);
		goto err_reset;
	}

	return ret;

err_reset:
	if (i2c->irq)
		free_irq(i2c->irq, wm5100);
	wm5100_free_gpio(i2c);
	if (wm5100->pdata.reset) {
		gpio_set_value_cansleep(wm5100->pdata.reset, 0);
		gpio_free(wm5100->pdata.reset);
	}
err_ldo:
	if (wm5100->pdata.ldo_ena) {
		gpio_set_value_cansleep(wm5100->pdata.ldo_ena, 0);
		gpio_free(wm5100->pdata.ldo_ena);
	}
err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm5100->core_supplies),
			       wm5100->core_supplies);
err:
	return ret;
}

static int wm5100_i2c_remove(struct i2c_client *i2c)
{
	struct wm5100_priv *wm5100 = i2c_get_clientdata(i2c);

	snd_soc_unregister_codec(&i2c->dev);
	if (i2c->irq)
		free_irq(i2c->irq, wm5100);
	wm5100_free_gpio(i2c);
	if (wm5100->pdata.reset) {
		gpio_set_value_cansleep(wm5100->pdata.reset, 0);
		gpio_free(wm5100->pdata.reset);
	}
	if (wm5100->pdata.ldo_ena) {
		gpio_set_value_cansleep(wm5100->pdata.ldo_ena, 0);
		gpio_free(wm5100->pdata.ldo_ena);
	}

	return 0;
}

#ifdef CONFIG_PM
static int wm5100_runtime_suspend(struct device *dev)
{
	struct wm5100_priv *wm5100 = dev_get_drvdata(dev);

	regcache_cache_only(wm5100->regmap, true);
	regcache_mark_dirty(wm5100->regmap);
	if (wm5100->pdata.ldo_ena)
		gpio_set_value_cansleep(wm5100->pdata.ldo_ena, 0);
	regulator_bulk_disable(ARRAY_SIZE(wm5100->core_supplies),
			       wm5100->core_supplies);

	return 0;
}

static int wm5100_runtime_resume(struct device *dev)
{
	struct wm5100_priv *wm5100 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(wm5100->core_supplies),
				    wm5100->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n",
			ret);
		return ret;
	}

	if (wm5100->pdata.ldo_ena) {
		gpio_set_value_cansleep(wm5100->pdata.ldo_ena, 1);
		msleep(2);
	}

	regcache_cache_only(wm5100->regmap, false);
	regcache_sync(wm5100->regmap);

	return 0;
}
#endif

static struct dev_pm_ops wm5100_pm = {
	SET_RUNTIME_PM_OPS(wm5100_runtime_suspend, wm5100_runtime_resume,
			   NULL)
};

static const struct i2c_device_id wm5100_i2c_id[] = {
	{ "wm5100", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm5100_i2c_id);

static struct i2c_driver wm5100_i2c_driver = {
	.driver = {
		.name = "wm5100",
		.owner = THIS_MODULE,
		.pm = &wm5100_pm,
	},
	.probe =    wm5100_i2c_probe,
	.remove =   wm5100_i2c_remove,
	.id_table = wm5100_i2c_id,
};

module_i2c_driver(wm5100_i2c_driver);

MODULE_DESCRIPTION("ASoC WM5100 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
