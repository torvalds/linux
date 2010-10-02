/*
 * wm8962.c  --  WM8962 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
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
#include <linux/gcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8962.h>

#include "wm8962.h"

#define WM8962_NUM_SUPPLIES 8
static const char *wm8962_supply_names[WM8962_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD",
	"CPVDD",
	"MICVDD",
	"PLLVDD",
	"SPKVDD1",
	"SPKVDD2",
};

/* codec private data */
struct wm8962_priv {
	struct snd_soc_codec *codec;

	u16 reg_cache[WM8962_MAX_REGISTER + 1];

	int sysclk;
	int sysclk_rate;

	int bclk;  /* Desired BCLK */
	int lrclk;

	int fll_src;
	int fll_fref;
	int fll_fout;

	struct delayed_work mic_work;
	struct snd_soc_jack *jack;

	struct regulator_bulk_data supplies[WM8962_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8962_NUM_SUPPLIES];

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	struct input_dev *beep;
	struct work_struct beep_work;
	int beep_rate;
#endif

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

/* We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8962_REGULATOR_EVENT(n) \
static int wm8962_regulator_event_##n(struct notifier_block *nb, \
				    unsigned long event, void *data)	\
{ \
	struct wm8962_priv *wm8962 = container_of(nb, struct wm8962_priv, \
						  disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		wm8962->codec->cache_sync = 1; \
	} \
	return 0; \
}

WM8962_REGULATOR_EVENT(0)
WM8962_REGULATOR_EVENT(1)
WM8962_REGULATOR_EVENT(2)
WM8962_REGULATOR_EVENT(3)
WM8962_REGULATOR_EVENT(4)
WM8962_REGULATOR_EVENT(5)
WM8962_REGULATOR_EVENT(6)
WM8962_REGULATOR_EVENT(7)

static int wm8962_volatile_register(unsigned int reg)
{
	if (wm8962_reg_access[reg].vol)
		return 1;
	else
		return 0;
}

static int wm8962_readable_register(unsigned int reg)
{
	if (wm8962_reg_access[reg].read)
		return 1;
	else
		return 0;
}

static int wm8962_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8962_SOFTWARE_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(inpga_tlv, -2325, 75, 0);
static const DECLARE_TLV_DB_SCALE(mixin_tlv, -1500, 300, 0);
static const unsigned int mixinpga_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 1, TLV_DB_SCALE_ITEM(0, 600, 0),
	2, 2, TLV_DB_SCALE_ITEM(1300, 1300, 0),
	3, 4, TLV_DB_SCALE_ITEM(1800, 200, 0),
	5, 5, TLV_DB_SCALE_ITEM(2400, 0, 0),
	6, 7, TLV_DB_SCALE_ITEM(2700, 300, 0),
};
static const DECLARE_TLV_DB_SCALE(beep_tlv, -9600, 600, 1);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(st_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(inmix_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(hp_tlv, -700, 100, 0);
static const unsigned int classd_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 6, TLV_DB_SCALE_ITEM(0, 150, 0),
	7, 7, TLV_DB_SCALE_ITEM(1200, 0, 0),
};

/* The VU bits for the headphones are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_hp_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	u16 *reg_cache = wm8962->reg_cache;
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_HPOUTL_PGA_ENA)
		return snd_soc_write(codec, WM8962_HPOUTL_VOLUME,
				     reg_cache[WM8962_HPOUTL_VOLUME]);

	/* ...otherwise the right.  The VU is stereo. */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_HPOUTR_PGA_ENA)
		return snd_soc_write(codec, WM8962_HPOUTR_VOLUME,
				     reg_cache[WM8962_HPOUTR_VOLUME]);

	return 0;
}

/* The VU bits for the speakers are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_spk_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	u16 *reg_cache = wm8962->reg_cache;
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_SPKOUTL_PGA_ENA)
		return snd_soc_write(codec, WM8962_SPKOUTL_VOLUME,
				     reg_cache[WM8962_SPKOUTL_VOLUME]);

	/* ...otherwise the right.  The VU is stereo. */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_SPKOUTR_PGA_ENA)
		return snd_soc_write(codec, WM8962_SPKOUTR_VOLUME,
				     reg_cache[WM8962_SPKOUTR_VOLUME]);

	return 0;
}

static const struct snd_kcontrol_new wm8962_snd_controls[] = {
SOC_DOUBLE("Input Mixer Switch", WM8962_INPUT_MIXER_CONTROL_1, 3, 2, 1, 1),

SOC_SINGLE_TLV("MIXINL IN2L Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 6, 7, 0,
	       mixin_tlv),
SOC_SINGLE_TLV("MIXINL PGA Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 3, 7, 0,
	       mixinpga_tlv),
SOC_SINGLE_TLV("MIXINL IN3L Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 0, 7, 0,
	       mixin_tlv),

SOC_SINGLE_TLV("MIXINR IN2R Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 6, 7, 0,
	       mixin_tlv),
SOC_SINGLE_TLV("MIXINR PGA Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 3, 7, 0,
	       mixinpga_tlv),
SOC_SINGLE_TLV("MIXINR IN3R Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 0, 7, 0,
	       mixin_tlv),

SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8962_LEFT_ADC_VOLUME,
		 WM8962_RIGHT_ADC_VOLUME, 1, 127, 0, digital_tlv),
SOC_DOUBLE_R_TLV("Capture Volume", WM8962_LEFT_INPUT_VOLUME,
		 WM8962_RIGHT_INPUT_VOLUME, 0, 63, 0, inpga_tlv),
SOC_DOUBLE_R("Capture Switch", WM8962_LEFT_INPUT_VOLUME,
	     WM8962_RIGHT_INPUT_VOLUME, 7, 1, 1),
SOC_DOUBLE_R("Capture ZC Switch", WM8962_LEFT_INPUT_VOLUME,
	     WM8962_RIGHT_INPUT_VOLUME, 6, 1, 1),

SOC_DOUBLE_R_TLV("Sidetone Volume", WM8962_DAC_DSP_MIXING_1,
		 WM8962_DAC_DSP_MIXING_2, 4, 12, 0, st_tlv),

SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8962_LEFT_DAC_VOLUME,
		 WM8962_RIGHT_DAC_VOLUME, 1, 127, 0, digital_tlv),
SOC_SINGLE("DAC High Performance Switch", WM8962_ADC_DAC_CONTROL_2, 0, 1, 0),

SOC_SINGLE("ADC High Performance Switch", WM8962_ADDITIONAL_CONTROL_1,
	   5, 1, 0),

SOC_SINGLE_TLV("Beep Volume", WM8962_BEEP_GENERATOR_1, 4, 15, 0, beep_tlv),

SOC_DOUBLE_R_TLV("Headphone Volume", WM8962_HPOUTL_VOLUME,
		 WM8962_HPOUTR_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_EXT("Headphone Switch", WM8962_PWR_MGMT_2, 1, 0, 1, 1,
	       snd_soc_get_volsw, wm8962_put_hp_sw),
SOC_DOUBLE_R("Headphone ZC Switch", WM8962_HPOUTL_VOLUME, WM8962_HPOUTR_VOLUME,
	     7, 1, 0),
SOC_DOUBLE_TLV("Headphone Aux Volume", WM8962_ANALOGUE_HP_2, 3, 6, 7, 0,
	       hp_tlv),

SOC_DOUBLE_R("Headphone Mixer Switch", WM8962_HEADPHONE_MIXER_3,
	     WM8962_HEADPHONE_MIXER_4, 8, 1, 1),

SOC_SINGLE_TLV("HPMIXL IN4L Volume", WM8962_HEADPHONE_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXL IN4R Volume", WM8962_HEADPHONE_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXL MIXINL Volume", WM8962_HEADPHONE_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("HPMIXL MIXINR Volume", WM8962_HEADPHONE_MIXER_3,
	       6, 1, 1, inmix_tlv),

SOC_SINGLE_TLV("HPMIXR IN4L Volume", WM8962_HEADPHONE_MIXER_4,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXR IN4R Volume", WM8962_HEADPHONE_MIXER_4,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXR MIXINL Volume", WM8962_HEADPHONE_MIXER_4,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("HPMIXR MIXINR Volume", WM8962_HEADPHONE_MIXER_4,
	       6, 1, 1, inmix_tlv),

SOC_SINGLE_TLV("Speaker Boost Volume", WM8962_CLASS_D_CONTROL_2, 0, 7, 0,
	       classd_tlv),
};

static const struct snd_kcontrol_new wm8962_spk_mono_controls[] = {
SOC_SINGLE_TLV("Speaker Volume", WM8962_SPKOUTL_VOLUME, 0, 127, 0, out_tlv),
SOC_SINGLE_EXT("Speaker Switch", WM8962_CLASS_D_CONTROL_1, 1, 1, 1,
	       snd_soc_get_volsw, wm8962_put_spk_sw),
SOC_SINGLE("Speaker ZC Switch", WM8962_SPKOUTL_VOLUME, 7, 1, 0),

SOC_SINGLE("Speaker Mixer Switch", WM8962_SPEAKER_MIXER_3, 8, 1, 1),
SOC_SINGLE_TLV("Speaker Mixer IN4L Volume", WM8962_SPEAKER_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("Speaker Mixer IN4R Volume", WM8962_SPEAKER_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("Speaker Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_3,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       7, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       6, 1, 0, inmix_tlv),
};

static const struct snd_kcontrol_new wm8962_spk_stereo_controls[] = {
SOC_DOUBLE_R_TLV("Speaker Volume", WM8962_SPKOUTL_VOLUME,
		 WM8962_SPKOUTR_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_EXT("Speaker Switch", WM8962_CLASS_D_CONTROL_1, 1, 0, 1, 1,
	       snd_soc_get_volsw, wm8962_put_spk_sw),
SOC_DOUBLE_R("Speaker ZC Switch", WM8962_SPKOUTL_VOLUME, WM8962_SPKOUTR_VOLUME,
	     7, 1, 0),

SOC_DOUBLE_R("Speaker Mixer Switch", WM8962_SPEAKER_MIXER_3,
	     WM8962_SPEAKER_MIXER_4, 8, 1, 1),

SOC_SINGLE_TLV("SPKOUTL Mixer IN4L Volume", WM8962_SPEAKER_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer IN4R Volume", WM8962_SPEAKER_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_3,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       7, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       6, 1, 0, inmix_tlv),

SOC_SINGLE_TLV("SPKOUTR Mixer IN4L Volume", WM8962_SPEAKER_MIXER_4,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer IN4R Volume", WM8962_SPEAKER_MIXER_4,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_4,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_4,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       5, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       4, 1, 0, inmix_tlv),
};

static int sysclk_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int src;
	int fll;

	src = snd_soc_read(codec, WM8962_CLOCKING2) & WM8962_SYSCLK_SRC_MASK;

	switch (src) {
	case 0:      /* MCLK */
		fll = 0;
		break;
	case 0x200:  /* FLL */
		fll = 1;
		break;
	default:
		dev_err(codec->dev, "Unknown SYSCLK source %x\n", src);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (fll)
			snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
					    WM8962_FLL_ENA, WM8962_FLL_ENA);
		break;

	case SND_SOC_DAPM_POST_PMD:
		if (fll)
			snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
					    WM8962_FLL_ENA, 0);
		break;

	default:
		BUG();
		return -EINVAL;
	}

	return 0;
}

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(5);
		break;

	default:
		BUG();
		return -EINVAL;
	}

	return 0;
}

static int hp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int timeout;
	int reg;
	int expected = (WM8962_DCS_STARTUP_DONE_HP1L |
			WM8962_DCS_STARTUP_DONE_HP1R);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA);
		udelay(20);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY);

		/* Start the DC servo */
		snd_soc_update_bits(codec, WM8962_DC_SERVO_1,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP);

		/* Wait for it to complete, should be well under 100ms */
		timeout = 0;
		do {
			msleep(1);
			reg = snd_soc_read(codec, WM8962_DC_SERVO_6);
			if (reg < 0) {
				dev_err(codec->dev,
					"Failed to read DCS status: %d\n",
					reg);
				continue;
			}
			dev_dbg(codec->dev, "DCS status: %x\n", reg);
		} while (++timeout < 200 && (reg & expected) != expected);

		if ((reg & expected) != expected)
			dev_err(codec->dev, "DC servo timed out\n");
		else
			dev_dbg(codec->dev, "DC servo complete after %dms\n",
				timeout);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP);
		udelay(20);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT, 0);

		udelay(20);

		snd_soc_update_bits(codec, WM8962_DC_SERVO_1,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP,
				    0);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA |
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY |
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP, 0);
				    
		break;

	default:
		BUG();
		return -EINVAL;
	
	}

	return 0;
}

/* VU bits for the output PGAs only take effect while the PGA is powered */
static int out_pga_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	u16 *reg_cache = wm8962->reg_cache;
	int reg;

	switch (w->shift) {
	case WM8962_HPOUTR_PGA_ENA_SHIFT:
		reg = WM8962_HPOUTR_VOLUME;
		break;
	case WM8962_HPOUTL_PGA_ENA_SHIFT:
		reg = WM8962_HPOUTL_VOLUME;
		break;
	case WM8962_SPKOUTR_PGA_ENA_SHIFT:
		reg = WM8962_SPKOUTR_VOLUME;
		break;
	case WM8962_SPKOUTL_PGA_ENA_SHIFT:
		reg = WM8962_SPKOUTL_VOLUME;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return snd_soc_write(codec, reg, reg_cache[reg]);
	default:
		BUG();
		return -EINVAL;
	}
}

static const char *st_text[] = { "None", "Right", "Left" };

static const struct soc_enum str_enum =
	SOC_ENUM_SINGLE(WM8962_DAC_DSP_MIXING_1, 2, 3, st_text);

static const struct snd_kcontrol_new str_mux =
	SOC_DAPM_ENUM("Right Sidetone", str_enum);

static const struct soc_enum stl_enum =
	SOC_ENUM_SINGLE(WM8962_DAC_DSP_MIXING_2, 2, 3, st_text);

static const struct snd_kcontrol_new stl_mux =
	SOC_DAPM_ENUM("Left Sidetone", stl_enum);

static const char *outmux_text[] = { "DAC", "Mixer" };

static const struct soc_enum spkoutr_enum =
	SOC_ENUM_SINGLE(WM8962_SPEAKER_MIXER_2, 7, 2, outmux_text);

static const struct snd_kcontrol_new spkoutr_mux =
	SOC_DAPM_ENUM("SPKOUTR Mux", spkoutr_enum);

static const struct soc_enum spkoutl_enum =
	SOC_ENUM_SINGLE(WM8962_SPEAKER_MIXER_1, 7, 2, outmux_text);

static const struct snd_kcontrol_new spkoutl_mux =
	SOC_DAPM_ENUM("SPKOUTL Mux", spkoutl_enum);

static const struct soc_enum hpoutr_enum =
	SOC_ENUM_SINGLE(WM8962_HEADPHONE_MIXER_2, 7, 2, outmux_text);

static const struct snd_kcontrol_new hpoutr_mux =
	SOC_DAPM_ENUM("HPOUTR Mux", hpoutr_enum);

static const struct soc_enum hpoutl_enum =
	SOC_ENUM_SINGLE(WM8962_HEADPHONE_MIXER_1, 7, 2, outmux_text);

static const struct snd_kcontrol_new hpoutl_mux =
	SOC_DAPM_ENUM("HPOUTL Mux", hpoutl_enum);

static const struct snd_kcontrol_new inpgal[] = {
SOC_DAPM_SINGLE("IN1L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 3, 1, 0),
SOC_DAPM_SINGLE("IN2L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 2, 1, 0),
SOC_DAPM_SINGLE("IN3L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 1, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 0, 1, 0),
};

static const struct snd_kcontrol_new inpgar[] = {
SOC_DAPM_SINGLE("IN1R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 3, 1, 0),
SOC_DAPM_SINGLE("IN2R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 2, 1, 0),
SOC_DAPM_SINGLE("IN3R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 0, 1, 0),
};

static const struct snd_kcontrol_new mixinl[] = {
SOC_DAPM_SINGLE("IN2L Switch", WM8962_INPUT_MIXER_CONTROL_2, 5, 1, 0),
SOC_DAPM_SINGLE("IN3L Switch", WM8962_INPUT_MIXER_CONTROL_2, 4, 1, 0),
SOC_DAPM_SINGLE("PGA Switch", WM8962_INPUT_MIXER_CONTROL_2, 3, 1, 0),
};

static const struct snd_kcontrol_new mixinr[] = {
SOC_DAPM_SINGLE("IN2R Switch", WM8962_INPUT_MIXER_CONTROL_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN3R Switch", WM8962_INPUT_MIXER_CONTROL_2, 1, 1, 0),
SOC_DAPM_SINGLE("PGA Switch", WM8962_INPUT_MIXER_CONTROL_2, 0, 1, 0),
};

static const struct snd_kcontrol_new hpmixl[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_HEADPHONE_MIXER_1, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_HEADPHONE_MIXER_1, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_HEADPHONE_MIXER_1, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_HEADPHONE_MIXER_1, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_HEADPHONE_MIXER_1, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_HEADPHONE_MIXER_1, 0, 1, 0),
};

static const struct snd_kcontrol_new hpmixr[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_HEADPHONE_MIXER_2, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_HEADPHONE_MIXER_2, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_HEADPHONE_MIXER_2, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_HEADPHONE_MIXER_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_HEADPHONE_MIXER_2, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_HEADPHONE_MIXER_2, 0, 1, 0),
};

static const struct snd_kcontrol_new spkmixl[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_SPEAKER_MIXER_1, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_SPEAKER_MIXER_1, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_SPEAKER_MIXER_1, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_SPEAKER_MIXER_1, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_SPEAKER_MIXER_1, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_SPEAKER_MIXER_1, 0, 1, 0),
};

static const struct snd_kcontrol_new spkmixr[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_SPEAKER_MIXER_2, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_SPEAKER_MIXER_2, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_SPEAKER_MIXER_2, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_SPEAKER_MIXER_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_SPEAKER_MIXER_2, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_SPEAKER_MIXER_2, 0, 1, 0),
};

static const struct snd_soc_dapm_widget wm8962_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("IN4L"),
SND_SOC_DAPM_INPUT("IN4R"),
SND_SOC_DAPM_INPUT("Beep"),

SND_SOC_DAPM_MICBIAS("MICBIAS", WM8962_PWR_MGMT_1, 1, 0),

SND_SOC_DAPM_SUPPLY("Class G", WM8962_CHARGE_PUMP_B, 0, 1, NULL, 0),
SND_SOC_DAPM_SUPPLY("SYSCLK", WM8962_CLOCKING2, 5, 0, sysclk_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("Charge Pump", WM8962_CHARGE_PUMP_1, 0, 0, cp_event,
		    SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8962_ADDITIONAL_CONTROL_1, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("INPGAL", WM8962_LEFT_INPUT_PGA_CONTROL, 4, 0,
		   inpgal, ARRAY_SIZE(inpgal)),
SND_SOC_DAPM_MIXER("INPGAR", WM8962_RIGHT_INPUT_PGA_CONTROL, 4, 0,
		   inpgar, ARRAY_SIZE(inpgar)),
SND_SOC_DAPM_MIXER("MIXINL", WM8962_PWR_MGMT_1, 5, 0,
		   mixinl, ARRAY_SIZE(mixinl)),
SND_SOC_DAPM_MIXER("MIXINR", WM8962_PWR_MGMT_1, 4, 0,
		   mixinr, ARRAY_SIZE(mixinr)),

SND_SOC_DAPM_ADC("ADCL", "Capture", WM8962_PWR_MGMT_1, 3, 0),
SND_SOC_DAPM_ADC("ADCR", "Capture", WM8962_PWR_MGMT_1, 2, 0),

SND_SOC_DAPM_MUX("STL", SND_SOC_NOPM, 0, 0, &stl_mux),
SND_SOC_DAPM_MUX("STR", SND_SOC_NOPM, 0, 0, &str_mux),

SND_SOC_DAPM_DAC("DACL", "Playback", WM8962_PWR_MGMT_2, 8, 0),
SND_SOC_DAPM_DAC("DACR", "Playback", WM8962_PWR_MGMT_2, 7, 0),

SND_SOC_DAPM_PGA("Left Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("HPMIXL", WM8962_MIXER_ENABLES, 3, 0,
		   hpmixl, ARRAY_SIZE(hpmixl)),
SND_SOC_DAPM_MIXER("HPMIXR", WM8962_MIXER_ENABLES, 2, 0,
		   hpmixr, ARRAY_SIZE(hpmixr)),

SND_SOC_DAPM_MUX_E("HPOUTL PGA", WM8962_PWR_MGMT_2, 6, 0, &hpoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_MUX_E("HPOUTR PGA", WM8962_PWR_MGMT_2, 5, 0, &hpoutr_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA_E("HPOUT", SND_SOC_NOPM, 0, 0, NULL, 0, hp_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
};

static const struct snd_soc_dapm_widget wm8962_dapm_spk_mono_widgets[] = {
SND_SOC_DAPM_MIXER("Speaker Mixer", WM8962_MIXER_ENABLES, 1, 0,
		   spkmixl, ARRAY_SIZE(spkmixl)),
SND_SOC_DAPM_MUX_E("Speaker PGA", WM8962_PWR_MGMT_2, 4, 0, &spkoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA("Speaker Output", WM8962_CLASS_D_CONTROL_1, 7, 0, NULL, 0),
SND_SOC_DAPM_OUTPUT("SPKOUT"),
};

static const struct snd_soc_dapm_widget wm8962_dapm_spk_stereo_widgets[] = {
SND_SOC_DAPM_MIXER("SPKOUTL Mixer", WM8962_MIXER_ENABLES, 1, 0,
		   spkmixl, ARRAY_SIZE(spkmixl)),
SND_SOC_DAPM_MIXER("SPKOUTR Mixer", WM8962_MIXER_ENABLES, 0, 0,
		   spkmixr, ARRAY_SIZE(spkmixr)),

SND_SOC_DAPM_MUX_E("SPKOUTL PGA", WM8962_PWR_MGMT_2, 4, 0, &spkoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_MUX_E("SPKOUTR PGA", WM8962_PWR_MGMT_2, 3, 0, &spkoutr_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("SPKOUTR Output", WM8962_CLASS_D_CONTROL_1, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("SPKOUTL Output", WM8962_CLASS_D_CONTROL_1, 6, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("SPKOUTL"),
SND_SOC_DAPM_OUTPUT("SPKOUTR"),
};

static const struct snd_soc_dapm_route wm8962_intercon[] = {
	{ "INPGAL", "IN1L Switch", "IN1L" },
	{ "INPGAL", "IN2L Switch", "IN2L" },
	{ "INPGAL", "IN3L Switch", "IN3L" },
	{ "INPGAL", "IN4L Switch", "IN4L" },

	{ "INPGAR", "IN1R Switch", "IN1R" },
	{ "INPGAR", "IN2R Switch", "IN2R" },
	{ "INPGAR", "IN3R Switch", "IN3R" },
	{ "INPGAR", "IN4R Switch", "IN4R" },

	{ "MIXINL", "IN2L Switch", "IN2L" },
	{ "MIXINL", "IN3L Switch", "IN3L" },
	{ "MIXINL", "PGA Switch", "INPGAL" },

	{ "MIXINR", "IN2R Switch", "IN2R" },
	{ "MIXINR", "IN3R Switch", "IN3R" },
	{ "MIXINR", "PGA Switch", "INPGAR" },

	{ "ADCL", NULL, "SYSCLK" },
	{ "ADCL", NULL, "TOCLK" },
	{ "ADCL", NULL, "MIXINL" },

	{ "ADCR", NULL, "SYSCLK" },
	{ "ADCR", NULL, "TOCLK" },
	{ "ADCR", NULL, "MIXINR" },

	{ "STL", "Left", "ADCL" },
	{ "STL", "Right", "ADCR" },

	{ "STR", "Left", "ADCL" },
	{ "STR", "Right", "ADCR" },

	{ "DACL", NULL, "SYSCLK" },
	{ "DACL", NULL, "TOCLK" },
	{ "DACL", NULL, "Beep" },
	{ "DACL", NULL, "STL" },

	{ "DACR", NULL, "SYSCLK" },
	{ "DACR", NULL, "TOCLK" },
	{ "DACR", NULL, "Beep" },
	{ "DACR", NULL, "STR" },

	{ "HPMIXL", "IN4L Switch", "IN4L" },
	{ "HPMIXL", "IN4R Switch", "IN4R" },
	{ "HPMIXL", "DACL Switch", "DACL" },
	{ "HPMIXL", "DACR Switch", "DACR" },
	{ "HPMIXL", "MIXINL Switch", "MIXINL" },
	{ "HPMIXL", "MIXINR Switch", "MIXINR" },

	{ "HPMIXR", "IN4L Switch", "IN4L" },
	{ "HPMIXR", "IN4R Switch", "IN4R" },
	{ "HPMIXR", "DACL Switch", "DACL" },
	{ "HPMIXR", "DACR Switch", "DACR" },
	{ "HPMIXR", "MIXINL Switch", "MIXINL" },
	{ "HPMIXR", "MIXINR Switch", "MIXINR" },

	{ "Left Bypass", NULL, "HPMIXL" },
	{ "Left Bypass", NULL, "Class G" },

	{ "Right Bypass", NULL, "HPMIXR" },
	{ "Right Bypass", NULL, "Class G" },

	{ "HPOUTL PGA", "Mixer", "Left Bypass" },
	{ "HPOUTL PGA", "DAC", "DACL" },

	{ "HPOUTR PGA", "Mixer", "Right Bypass" },
	{ "HPOUTR PGA", "DAC", "DACR" },

	{ "HPOUT", NULL, "HPOUTL PGA" },
	{ "HPOUT", NULL, "HPOUTR PGA" },
	{ "HPOUT", NULL, "Charge Pump" },
	{ "HPOUT", NULL, "SYSCLK" },
	{ "HPOUT", NULL, "TOCLK" },

	{ "HPOUTL", NULL, "HPOUT" },
	{ "HPOUTR", NULL, "HPOUT" },
};

static const struct snd_soc_dapm_route wm8962_spk_mono_intercon[] = {
	{ "Speaker Mixer", "IN4L Switch", "IN4L" },
	{ "Speaker Mixer", "IN4R Switch", "IN4R" },
	{ "Speaker Mixer", "DACL Switch", "DACL" },
	{ "Speaker Mixer", "DACR Switch", "DACR" },
	{ "Speaker Mixer", "MIXINL Switch", "MIXINL" },
	{ "Speaker Mixer", "MIXINR Switch", "MIXINR" },

	{ "Speaker PGA", "Mixer", "Speaker Mixer" },
	{ "Speaker PGA", "DAC", "DACL" },

	{ "Speaker Output", NULL, "Speaker PGA" },
	{ "Speaker Output", NULL, "SYSCLK" },
	{ "Speaker Output", NULL, "TOCLK" },

	{ "SPKOUT", NULL, "Speaker Output" },
};

static const struct snd_soc_dapm_route wm8962_spk_stereo_intercon[] = {
	{ "SPKOUTL Mixer", "IN4L Switch", "IN4L" },
	{ "SPKOUTL Mixer", "IN4R Switch", "IN4R" },
	{ "SPKOUTL Mixer", "DACL Switch", "DACL" },
	{ "SPKOUTL Mixer", "DACR Switch", "DACR" },
	{ "SPKOUTL Mixer", "MIXINL Switch", "MIXINL" },
	{ "SPKOUTL Mixer", "MIXINR Switch", "MIXINR" },

	{ "SPKOUTR Mixer", "IN4L Switch", "IN4L" },
	{ "SPKOUTR Mixer", "IN4R Switch", "IN4R" },
	{ "SPKOUTR Mixer", "DACL Switch", "DACL" },
	{ "SPKOUTR Mixer", "DACR Switch", "DACR" },
	{ "SPKOUTR Mixer", "MIXINL Switch", "MIXINL" },
	{ "SPKOUTR Mixer", "MIXINR Switch", "MIXINR" },

	{ "SPKOUTL PGA", "Mixer", "SPKOUTL Mixer" },
	{ "SPKOUTL PGA", "DAC", "DACL" },

	{ "SPKOUTR PGA", "Mixer", "SPKOUTR Mixer" },
	{ "SPKOUTR PGA", "DAC", "DACR" },

	{ "SPKOUTL Output", NULL, "SPKOUTL PGA" },
	{ "SPKOUTL Output", NULL, "SYSCLK" },
	{ "SPKOUTL Output", NULL, "TOCLK" },

	{ "SPKOUTR Output", NULL, "SPKOUTR PGA" },
	{ "SPKOUTR Output", NULL, "SYSCLK" },
	{ "SPKOUTR Output", NULL, "TOCLK" },

	{ "SPKOUTL", NULL, "SPKOUTL Output" },
	{ "SPKOUTR", NULL, "SPKOUTR Output" },
};

static int wm8962_add_widgets(struct snd_soc_codec *codec)
{
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);

	snd_soc_add_controls(codec, wm8962_snd_controls,
			     ARRAY_SIZE(wm8962_snd_controls));
	if (pdata && pdata->spk_mono)
		snd_soc_add_controls(codec, wm8962_spk_mono_controls,
				     ARRAY_SIZE(wm8962_spk_mono_controls));
	else
		snd_soc_add_controls(codec, wm8962_spk_stereo_controls,
				     ARRAY_SIZE(wm8962_spk_stereo_controls));


	snd_soc_dapm_new_controls(codec, wm8962_dapm_widgets,
				  ARRAY_SIZE(wm8962_dapm_widgets));
	if (pdata && pdata->spk_mono)
		snd_soc_dapm_new_controls(codec, wm8962_dapm_spk_mono_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_mono_widgets));
	else
		snd_soc_dapm_new_controls(codec, wm8962_dapm_spk_stereo_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_stereo_widgets));

	snd_soc_dapm_add_routes(codec, wm8962_intercon,
				ARRAY_SIZE(wm8962_intercon));
	if (pdata && pdata->spk_mono)
		snd_soc_dapm_add_routes(codec, wm8962_spk_mono_intercon,
					ARRAY_SIZE(wm8962_spk_mono_intercon));
	else
		snd_soc_dapm_add_routes(codec, wm8962_spk_stereo_intercon,
					ARRAY_SIZE(wm8962_spk_stereo_intercon));


	snd_soc_dapm_disable_pin(codec, "Beep");

	return 0;
}

static void wm8962_sync_cache(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int i;

	if (!codec->cache_sync)
		return;

	dev_dbg(codec->dev, "Syncing cache\n");

	codec->cache_only = 0;

	/* Sync back cached values if they're different from the
	 * hardware default.
	 */
	for (i = 1; i < ARRAY_SIZE(wm8962->reg_cache); i++) {
		if (i == WM8962_SOFTWARE_RESET)
			continue;
		if (wm8962->reg_cache[i] == wm8962_reg[i])
			continue;

		snd_soc_write(codec, i, wm8962->reg_cache[i]);
	}

	codec->cache_sync = 0;
}

/* -1 for reserved values */
static const int bclk_divs[] = {
	1, -1, 2, 3, 4, -1, 6, 8, -1, 12, 16, 24, -1, 32, 32, 32
};

static void wm8962_configure_bclk(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int dspclk, i;
	int clocking2 = 0;
	int aif2 = 0;

	if (!wm8962->bclk) {
		dev_dbg(codec->dev, "No BCLK rate configured\n");
		return;
	}

	dspclk = snd_soc_read(codec, WM8962_CLOCKING1);
	if (dspclk < 0) {
		dev_err(codec->dev, "Failed to read DSPCLK: %d\n", dspclk);
		return;
	}

	dspclk = (dspclk & WM8962_DSPCLK_DIV_MASK) >> WM8962_DSPCLK_DIV_SHIFT;
	switch (dspclk) {
	case 0:
		dspclk = wm8962->sysclk_rate;
		break;
	case 1:
		dspclk = wm8962->sysclk_rate / 2;
		break;
	case 2:
		dspclk = wm8962->sysclk_rate / 4;
		break;
	default:
		dev_warn(codec->dev, "Unknown DSPCLK divisor read back\n");
		dspclk = wm8962->sysclk;
	}

	dev_dbg(codec->dev, "DSPCLK is %dHz, BCLK %d\n", dspclk, wm8962->bclk);

	/* We're expecting an exact match */
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		if (bclk_divs[i] < 0)
			continue;

		if (dspclk / bclk_divs[i] == wm8962->bclk) {
			dev_dbg(codec->dev, "Selected BCLK_DIV %d for %dHz\n",
				bclk_divs[i], wm8962->bclk);
			clocking2 |= i;
			break;
		}
	}
	if (i == ARRAY_SIZE(bclk_divs)) {
		dev_err(codec->dev, "Unsupported BCLK ratio %d\n",
			dspclk / wm8962->bclk);
		return;
	}

	aif2 |= wm8962->bclk / wm8962->lrclk;
	dev_dbg(codec->dev, "Selected LRCLK divisor %d for %dHz\n",
		wm8962->bclk / wm8962->lrclk, wm8962->lrclk);

	snd_soc_update_bits(codec, WM8962_CLOCKING2,
			    WM8962_BCLK_DIV_MASK, clocking2);
	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_2,
			    WM8962_AIF_RATE_MASK, aif2);
}

static int wm8962_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (level == codec->bias_level)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID 2*50k */
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x80);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
						    wm8962->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			wm8962_sync_cache(codec);

			snd_soc_update_bits(codec, WM8962_ANTI_POP,
					    WM8962_STARTUP_BIAS_ENA |
					    WM8962_VMID_BUF_ENA,
					    WM8962_STARTUP_BIAS_ENA |
					    WM8962_VMID_BUF_ENA);

			/* Bias enable at 2*50k for ramp */
			snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
					    WM8962_VMID_SEL_MASK |
					    WM8962_BIAS_ENA,
					    WM8962_BIAS_ENA | 0x180);

			msleep(5);

			snd_soc_update_bits(codec, WM8962_CLOCKING2,
					    WM8962_CLKREG_OVD,
					    WM8962_CLKREG_OVD);

			wm8962_configure_bclk(codec);
		}

		/* VMID 2*250k */
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x100);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK | WM8962_BIAS_ENA, 0);

		snd_soc_update_bits(codec, WM8962_ANTI_POP,
				    WM8962_STARTUP_BIAS_ENA |
				    WM8962_VMID_BUF_ENA, 0);

		regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies),
				       wm8962->supplies);
		break;
	}
	codec->bias_level = level;
	return 0;
}

static const struct {
	int rate;
	int reg;
} sr_vals[] = {
	{ 48000, 0 },
	{ 44100, 0 },
	{ 32000, 1 },
	{ 22050, 2 },
	{ 24000, 2 },
	{ 16000, 3 },
	{ 11025, 4 },
	{ 12000, 4 },
	{ 8000,  5 },
	{ 88200, 6 },
	{ 96000, 6 },
};

static const int sysclk_rates[] = {
	64, 128, 192, 256, 384, 512, 768, 1024, 1408, 1536,
};

static int wm8962_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int rate = params_rate(params);
	int i;
	int aif0 = 0;
	int adctl3 = 0;
	int clocking4 = 0;

	wm8962->bclk = snd_soc_params_to_bclk(params);
	wm8962->lrclk = params_rate(params);

	for (i = 0; i < ARRAY_SIZE(sr_vals); i++) {
		if (sr_vals[i].rate == rate) {
			adctl3 |= sr_vals[i].reg;
			break;
		}
	}
	if (i == ARRAY_SIZE(sr_vals)) {
		dev_err(codec->dev, "Unsupported rate %dHz\n", rate);
		return -EINVAL;
	}

	if (rate % 8000 == 0)
		adctl3 |= WM8962_SAMPLE_RATE_INT_MODE;

	for (i = 0; i < ARRAY_SIZE(sysclk_rates); i++) {
		if (sysclk_rates[i] == wm8962->sysclk_rate / rate) {
			clocking4 |= i << WM8962_SYSCLK_RATE_SHIFT;
			break;
		}
	}
	if (i == ARRAY_SIZE(sysclk_rates)) {
		dev_err(codec->dev, "Unsupported sysclk ratio %d\n",
			wm8962->sysclk_rate / rate);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		aif0 |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		aif0 |= 0x80;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		aif0 |= 0xc0;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_0,
			    WM8962_WL_MASK, aif0);
	snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_3,
			    WM8962_SAMPLE_RATE_INT_MODE |
			    WM8962_SAMPLE_RATE_MASK, adctl3);
	snd_soc_update_bits(codec, WM8962_CLOCKING_4,
			    WM8962_SYSCLK_RATE_MASK, clocking4);

	wm8962_configure_bclk(codec);

	return 0;
}

static int wm8962_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int src;

	switch (clk_id) {
	case WM8962_SYSCLK_MCLK:
		wm8962->sysclk = WM8962_SYSCLK_MCLK;
		src = 0;
		break;
	case WM8962_SYSCLK_FLL:
		wm8962->sysclk = WM8962_SYSCLK_FLL;
		src = 1 << WM8962_SYSCLK_SRC_SHIFT;
		WARN_ON(freq != wm8962->fll_fout);
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_CLOCKING2, WM8962_SYSCLK_SRC_MASK,
			    src);

	wm8962->sysclk_rate = freq;

	return 0;
}

static int wm8962_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int aif0 = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		aif0 |= WM8962_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_B:
		aif0 |= 3;

		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
		case SND_SOC_DAIFMT_IB_NF:
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif0 |= 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif0 |= 2;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		aif0 |= WM8962_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		aif0 |= WM8962_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		aif0 |= WM8962_BCLK_INV | WM8962_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aif0 |= WM8962_MSTR;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_0,
			    WM8962_FMT_MASK | WM8962_BCLK_INV | WM8962_MSTR |
			    WM8962_LRCLK_INV, aif0);

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

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

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

		if (div > 4) {
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
	pr_debug("FLL_FRATIO=%x FLL_OUTDIV=%x FLL_REFCLK_DIV=%x\n",
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_refclk_div);

	return 0;
}

static int wm8962_set_fll(struct snd_soc_dai *dai, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	int ret;
	int fll1 = snd_soc_read(codec, WM8962_FLL_CONTROL_1) & WM8962_FLL_ENA;

	/* Any change? */
	if (source == wm8962->fll_src && Fref == wm8962->fll_fref &&
	    Fout == wm8962->fll_fout)
		return 0;

	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");

		wm8962->fll_fref = 0;
		wm8962->fll_fout = 0;

		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_ENA, 0);

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	switch (fll_id) {
	case WM8962_FLL_MCLK:
	case WM8962_FLL_BCLK:
	case WM8962_FLL_OSC:
		fll1 |= (fll_id - 1) << WM8962_FLL_REFCLK_SRC_SHIFT;
		break;
	case WM8962_FLL_INT:
		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_OSC_ENA, WM8962_FLL_OSC_ENA);
		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_5,
				    WM8962_FLL_FRC_NCO, WM8962_FLL_FRC_NCO);
		break;
	default:
		dev_err(codec->dev, "Unknown FLL source %d\n", ret);
		return -EINVAL;
	}

	if (fll_div.theta || fll_div.lambda)
		fll1 |= WM8962_FLL_FRAC;

	/* Stop the FLL while we reconfigure */
	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1, WM8962_FLL_ENA, 0);

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_2,
			    WM8962_FLL_OUTDIV_MASK |
			    WM8962_FLL_REFCLK_DIV_MASK,
			    (fll_div.fll_outdiv << WM8962_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_refclk_div));

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_3,
			    WM8962_FLL_FRATIO_MASK, fll_div.fll_fratio);

	snd_soc_write(codec, WM8962_FLL_CONTROL_6, fll_div.theta);
	snd_soc_write(codec, WM8962_FLL_CONTROL_7, fll_div.lambda);
	snd_soc_write(codec, WM8962_FLL_CONTROL_8, fll_div.n);

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
			    WM8962_FLL_FRAC | WM8962_FLL_REFCLK_SRC_MASK |
			    WM8962_FLL_ENA, fll1);

	dev_dbg(codec->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	wm8962->fll_fref = Fref;
	wm8962->fll_fout = Fout;
	wm8962->fll_src = source;

	return 0;
}

static int wm8962_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int val;

	if (mute)
		val = WM8962_DAC_MUTE;
	else
		val = 0;

	return snd_soc_update_bits(codec, WM8962_ADC_DAC_CONTROL_1,
				   WM8962_DAC_MUTE, val);
}

#define WM8962_RATES SNDRV_PCM_RATE_8000_96000

#define WM8962_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8962_dai_ops = {
	.hw_params = wm8962_hw_params,
	.set_sysclk = wm8962_set_dai_sysclk,
	.set_fmt = wm8962_set_dai_fmt,
	.set_pll = wm8962_set_fll,
	.digital_mute = wm8962_mute,
};

static struct snd_soc_dai_driver wm8962_dai = {
	.name = "wm8962",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.ops = &wm8962_dai_ops,
	.symmetric_rates = 1,
};

static void wm8962_mic_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 = container_of(work,
						  struct wm8962_priv,
						  mic_work.work);
	struct snd_soc_codec *codec = wm8962->codec;
	int status = 0;
	int irq_pol = 0;
	int reg;

	reg = snd_soc_read(codec, WM8962_ADDITIONAL_CONTROL_4);

	if (reg & WM8962_MICDET_STS) {
		status |= SND_JACK_MICROPHONE;
		irq_pol |= WM8962_MICD_IRQ_POL;
	}

	if (reg & WM8962_MICSHORT_STS) {
		status |= SND_JACK_BTN_0;
		irq_pol |= WM8962_MICSCD_IRQ_POL;
	}

	snd_soc_jack_report(wm8962->jack, status,
			    SND_JACK_MICROPHONE | SND_JACK_BTN_0);

	snd_soc_update_bits(codec, WM8962_MICINT_SOURCE_POL,
			    WM8962_MICSCD_IRQ_POL |
			    WM8962_MICD_IRQ_POL, irq_pol);
}

static irqreturn_t wm8962_irq(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int mask;
	int active;

	mask = snd_soc_read(codec, WM8962_INTERRUPT_STATUS_2);

	active = snd_soc_read(codec, WM8962_INTERRUPT_STATUS_2);
	active &= ~mask;

	if (active & WM8962_FIFOS_ERR_EINT)
		dev_err(codec->dev, "FIFO error\n");

	if (active & WM8962_TEMP_SHUT_EINT)
		dev_crit(codec->dev, "Thermal shutdown\n");

	if (active & (WM8962_MICSCD_EINT | WM8962_MICD_EINT)) {
		dev_dbg(codec->dev, "Microphone event detected\n");

		schedule_delayed_work(&wm8962->mic_work,
				      msecs_to_jiffies(250));
	}

	/* Acknowledge the interrupts */
	snd_soc_write(codec, WM8962_INTERRUPT_STATUS_2, active);

	return IRQ_HANDLED;
}

/**
 * wm8962_mic_detect - Enable microphone detection via the WM8962 IRQ
 *
 * @codec:  WM8962 codec
 * @jack:   jack to report detection events on
 *
 * Enable microphone detection via IRQ on the WM8962.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8962 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * If no jack is supplied detection will be disabled.
 */
int wm8962_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int irq_mask, enable;

	wm8962->jack = jack;
	if (jack) {
		irq_mask = 0;
		enable = WM8962_MICDET_ENA;
	} else {
		irq_mask = WM8962_MICD_EINT | WM8962_MICSCD_EINT;
		enable = 0;
	}

	snd_soc_update_bits(codec, WM8962_INTERRUPT_STATUS_2_MASK,
			    WM8962_MICD_EINT | WM8962_MICSCD_EINT, irq_mask);
	snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_4,
			    WM8962_MICDET_ENA, enable);

	/* Send an initial empty report */
	snd_soc_jack_report(wm8962->jack, 0,
			    SND_JACK_MICROPHONE | SND_JACK_BTN_0);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8962_mic_detect);

#ifdef CONFIG_PM
static int wm8962_resume(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	u16 *reg_cache = codec->reg_cache;
	int i;

	/* Restore the registers */
	for (i = 1; i < ARRAY_SIZE(wm8962->reg_cache); i++) {
		switch (i) {
		case WM8962_SOFTWARE_RESET:
			continue;
		default:
			break;
		}

		if (reg_cache[i] != wm8962_reg[i])
			snd_soc_write(codec, i, reg_cache[i]);
	}

	return 0;
}
#else
#define wm8962_resume NULL
#endif

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
static int beep_rates[] = {
	500, 1000, 2000, 4000,
};

static void wm8962_beep_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 =
		container_of(work, struct wm8962_priv, beep_work);
	struct snd_soc_codec *codec = wm8962->codec;
	int i;
	int reg = 0;
	int best = 0;

	if (wm8962->beep_rate) {
		for (i = 0; i < ARRAY_SIZE(beep_rates); i++) {
			if (abs(wm8962->beep_rate - beep_rates[i]) <
			    abs(wm8962->beep_rate - beep_rates[best]))
				best = i;
		}

		dev_dbg(codec->dev, "Set beep rate %dHz for requested %dHz\n",
			beep_rates[best], wm8962->beep_rate);

		reg = WM8962_BEEP_ENA | (best << WM8962_BEEP_RATE_SHIFT);

		snd_soc_dapm_enable_pin(codec, "Beep");
	} else {
		dev_dbg(codec->dev, "Disabling beep\n");
		snd_soc_dapm_disable_pin(codec, "Beep");
	}

	snd_soc_update_bits(codec, WM8962_BEEP_GENERATOR_1,
			    WM8962_BEEP_ENA | WM8962_BEEP_RATE_MASK, reg);

	snd_soc_dapm_sync(codec);
}

/* For usability define a way of injecting beep events for the device -
 * many systems will not have a keyboard.
 */
static int wm8962_beep_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int hz)
{
	struct snd_soc_codec *codec = input_get_drvdata(dev);
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "Beep event %x %x\n", code, hz);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	/* Kick the beep from a workqueue */
	wm8962->beep_rate = hz;
	schedule_work(&wm8962->beep_work);
	return 0;
}

static ssize_t wm8962_beep_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);
	long int time;

	strict_strtol(buf, 10, &time);

	input_event(wm8962->beep, EV_SND, SND_TONE, time);

	return count;
}

static DEVICE_ATTR(beep, 0200, NULL, wm8962_beep_set);

static void wm8962_init_beep(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	wm8962->beep = input_allocate_device();
	if (!wm8962->beep) {
		dev_err(codec->dev, "Failed to allocate beep device\n");
		return;
	}

	INIT_WORK(&wm8962->beep_work, wm8962_beep_work);
	wm8962->beep_rate = 0;

	wm8962->beep->name = "WM8962 Beep Generator";
	wm8962->beep->phys = dev_name(codec->dev);
	wm8962->beep->id.bustype = BUS_I2C;

	wm8962->beep->evbit[0] = BIT_MASK(EV_SND);
	wm8962->beep->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	wm8962->beep->event = wm8962_beep_event;
	wm8962->beep->dev.parent = codec->dev;
	input_set_drvdata(wm8962->beep, codec);

	ret = input_register_device(wm8962->beep);
	if (ret != 0) {
		input_free_device(wm8962->beep);
		wm8962->beep = NULL;
		dev_err(codec->dev, "Failed to register beep device\n");
	}

	ret = device_create_file(codec->dev, &dev_attr_beep);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to create keyclick file: %d\n",
			ret);
	}
}

static void wm8962_free_beep(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);

	device_remove_file(codec->dev, &dev_attr_beep);
	input_unregister_device(wm8962->beep);
	cancel_work_sync(&wm8962->beep_work);
	wm8962->beep = NULL;

	snd_soc_update_bits(codec, WM8962_BEEP_GENERATOR_1, WM8962_BEEP_ENA,0);
}
#else
static void wm8962_init_beep(struct snd_soc_codec *codec)
{
}

static void wm8962_free_beep(struct snd_soc_codec *codec)
{
}
#endif

static void wm8962_set_gpio_mode(struct snd_soc_codec *codec, int gpio)
{
	int mask = 0;
	int val = 0;

	/* Some of the GPIOs are behind MFP configuration and need to
	 * be put into GPIO mode. */
	switch (gpio) {
	case 2:
		mask = WM8962_CLKOUT2_SEL_MASK;
		val = 1 << WM8962_CLKOUT2_SEL_SHIFT;
		break;
	case 3:
		mask = WM8962_CLKOUT3_SEL_MASK;
		val = 1 << WM8962_CLKOUT3_SEL_SHIFT;
		break;
	default:
		break;
	}

	if (mask)
		snd_soc_update_bits(codec, WM8962_ANALOGUE_CLOCKING1,
				    mask, val);
}

#ifdef CONFIG_GPIOLIB
static inline struct wm8962_priv *gpio_to_wm8962(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8962_priv, gpio_chip);
}

static int wm8962_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;

	/* The WM8962 GPIOs aren't linearly numbered.  For simplicity
	 * we export linear numbers and error out if the unsupported
	 * ones are requsted.
	 */
	switch (offset + 1) {
	case 2:
	case 3:
	case 5:
	case 6:
		break;
	default:
		return -EINVAL;
	}

	wm8962_set_gpio_mode(codec, offset + 1);

	return 0;
}

static void wm8962_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;

	snd_soc_update_bits(codec, WM8962_GPIO_BASE + offset,
			    WM8962_GP2_LVL, value << WM8962_GP2_LVL_SHIFT);
}

static int wm8962_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;
	int val;

	/* Force function 1 (logic output) */
	val = (1 << WM8962_GP2_FN_SHIFT) | (value << WM8962_GP2_LVL_SHIFT);

	return snd_soc_update_bits(codec, WM8962_GPIO_BASE + offset,
				   WM8962_GP2_FN_MASK | WM8962_GP2_LVL, val);
}

static struct gpio_chip wm8962_template_chip = {
	.label			= "wm8962",
	.owner			= THIS_MODULE,
	.request		= wm8962_gpio_request,
	.direction_output	= wm8962_gpio_direction_out,
	.set			= wm8962_gpio_set,
	.can_sleep		= 1,
};

static void wm8962_init_gpio(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);
	int ret;

	wm8962->gpio_chip = wm8962_template_chip;
	wm8962->gpio_chip.ngpio = WM8962_MAX_GPIO;
	wm8962->gpio_chip.dev = codec->dev;

	if (pdata && pdata->gpio_base)
		wm8962->gpio_chip.base = pdata->gpio_base;
	else
		wm8962->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8962->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8962_free_gpio(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = gpiochip_remove(&wm8962->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to remove GPIOs: %d\n", ret);
}
#else
static void wm8962_init_gpio(struct snd_soc_codec *codec)
{
}

static void wm8962_free_gpio(struct snd_soc_codec *codec)
{
}
#endif

static int wm8962_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);
	struct i2c_client *i2c = container_of(codec->dev, struct i2c_client,
					      dev);
	int i, trigger, irq_pol;

	wm8962->codec = codec;
	INIT_DELAYED_WORK(&wm8962->mic_work, wm8962_mic_work);

	codec->cache_sync = 1;
	codec->idle_bias_off = 1;

	ret = snd_soc_codec_set_cache_io(codec, 16, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++)
		wm8962->supplies[i].supply = wm8962_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8962->supplies),
				 wm8962->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		goto err;
	}

	wm8962->disable_nb[0].notifier_call = wm8962_regulator_event_0;
	wm8962->disable_nb[1].notifier_call = wm8962_regulator_event_1;
	wm8962->disable_nb[2].notifier_call = wm8962_regulator_event_2;
	wm8962->disable_nb[3].notifier_call = wm8962_regulator_event_3;
	wm8962->disable_nb[4].notifier_call = wm8962_regulator_event_4;
	wm8962->disable_nb[5].notifier_call = wm8962_regulator_event_5;
	wm8962->disable_nb[6].notifier_call = wm8962_regulator_event_6;
	wm8962->disable_nb[7].notifier_call = wm8962_regulator_event_7;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++) {
		ret = regulator_register_notifier(wm8962->supplies[i].consumer,
						  &wm8962->disable_nb[i]);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
				    wm8962->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = snd_soc_read(codec, WM8962_SOFTWARE_RESET);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (ret != wm8962_reg[WM8962_SOFTWARE_RESET]) {
		dev_err(codec->dev, "Device is not a WM8962, ID %x != %x\n",
			ret, wm8962_reg[WM8962_SOFTWARE_RESET]);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = snd_soc_read(codec, WM8962_RIGHT_INPUT_VOLUME);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_enable;
	}
	
	dev_info(codec->dev, "customer id %x revision %c\n",
		 (ret & WM8962_CUST_ID_MASK) >> WM8962_CUST_ID_SHIFT,
		 ((ret & WM8962_CHIP_REV_MASK) >> WM8962_CHIP_REV_SHIFT)
		 + 'A');

	ret = wm8962_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	/* SYSCLK defaults to on; make sure it is off so we can safely
	 * write to registers if the device is declocked.
	 */
	snd_soc_update_bits(codec, WM8962_CLOCKING2, WM8962_SYSCLK_ENA, 0);

	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);

	if (pdata) {
		/* Apply static configuration for GPIOs */
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_init); i++)
			if (pdata->gpio_init[i]) {
				wm8962_set_gpio_mode(codec, i + 1);
				snd_soc_write(codec, 0x200 + i,
					      pdata->gpio_init[i] & 0xffff);
			}

		/* Put the speakers into mono mode? */
		if (pdata->spk_mono)
			wm8962->reg_cache[WM8962_CLASS_D_CONTROL_2]
				|= WM8962_SPK_MONO;

		/* Micbias setup, detection enable and detection
		 * threasholds. */
		if (pdata->mic_cfg)
			snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_4,
					    WM8962_MICDET_ENA |
					    WM8962_MICDET_THR_MASK |
					    WM8962_MICSHORT_THR_MASK |
					    WM8962_MICBIAS_LVL,
					    pdata->mic_cfg);
	}

	/* Latch volume update bits */
	wm8962->reg_cache[WM8962_LEFT_INPUT_VOLUME] |= WM8962_IN_VU;
	wm8962->reg_cache[WM8962_RIGHT_INPUT_VOLUME] |= WM8962_IN_VU;
	wm8962->reg_cache[WM8962_LEFT_ADC_VOLUME] |= WM8962_ADC_VU;
	wm8962->reg_cache[WM8962_RIGHT_ADC_VOLUME] |= WM8962_ADC_VU;	
	wm8962->reg_cache[WM8962_LEFT_DAC_VOLUME] |= WM8962_DAC_VU;
	wm8962->reg_cache[WM8962_RIGHT_DAC_VOLUME] |= WM8962_DAC_VU;
	wm8962->reg_cache[WM8962_SPKOUTL_VOLUME] |= WM8962_SPKOUT_VU;
	wm8962->reg_cache[WM8962_SPKOUTR_VOLUME] |= WM8962_SPKOUT_VU;
	wm8962->reg_cache[WM8962_HPOUTL_VOLUME] |= WM8962_HPOUT_VU;
	wm8962->reg_cache[WM8962_HPOUTR_VOLUME] |= WM8962_HPOUT_VU;

	wm8962_add_widgets(codec);

	wm8962_init_beep(codec);
	wm8962_init_gpio(codec);

	if (i2c->irq) {
		if (pdata && pdata->irq_active_low) {
			trigger = IRQF_TRIGGER_LOW;
			irq_pol = WM8962_IRQ_POL;
		} else {
			trigger = IRQF_TRIGGER_HIGH;
			irq_pol = 0;
		}

		snd_soc_update_bits(codec, WM8962_INTERRUPT_CONTROL,
				    WM8962_IRQ_POL, irq_pol);

		ret = request_threaded_irq(i2c->irq, NULL, wm8962_irq,
					   trigger | IRQF_ONESHOT,
					   "wm8962", codec);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to request IRQ %d: %d\n",
				i2c->irq, ret);
			/* Non-fatal */
		} else {
			/* Enable error reporting IRQs by default */
			snd_soc_update_bits(codec,
					    WM8962_INTERRUPT_STATUS_2_MASK,
					    WM8962_TEMP_SHUT_EINT |
					    WM8962_FIFOS_ERR_EINT, 0);
		}
	}

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);
err:
	kfree(wm8962);
	return ret;
}

static int wm8962_remove(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = container_of(codec->dev, struct i2c_client,
					      dev);
	int i;

	if (i2c->irq)
		free_irq(i2c->irq, codec);

	cancel_delayed_work_sync(&wm8962->mic_work);

	wm8962_free_gpio(codec);
	wm8962_free_beep(codec);
	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++)
		regulator_unregister_notifier(wm8962->supplies[i].consumer,
					      &wm8962->disable_nb[i]);
	regulator_bulk_free(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8962 = {
	.probe =	wm8962_probe,
	.remove =	wm8962_remove,
	.resume =	wm8962_resume,
	.set_bias_level = wm8962_set_bias_level,
	.reg_cache_size = WM8962_MAX_REGISTER + 1,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8962_reg,
	.volatile_register = wm8962_volatile_register,
	.readable_register = wm8962_readable_register,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8962_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8962_priv *wm8962;
	int ret;

	wm8962 = kzalloc(sizeof(struct wm8962_priv), GFP_KERNEL);
	if (wm8962 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8962);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm8962, &wm8962_dai, 1);
	if (ret < 0)
		kfree(wm8962);

	return ret;
}

static __devexit int wm8962_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8962_i2c_id[] = {
	{ "wm8962", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8962_i2c_id);

static struct i2c_driver wm8962_i2c_driver = {
	.driver = {
		.name = "wm8962",
		.owner = THIS_MODULE,
	},
	.probe =    wm8962_i2c_probe,
	.remove =   __devexit_p(wm8962_i2c_remove),
	.id_table = wm8962_i2c_id,
};
#endif

static int __init wm8962_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8962_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8962 I2C driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8962_modinit);

static void __exit wm8962_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8962_i2c_driver);
#endif
}
module_exit(wm8962_exit);

MODULE_DESCRIPTION("ASoC WM8962 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
