// SPDX-License-Identifier: GPL-2.0
// tscs42xx.c -- TSCS42xx ALSA SoC Audio driver
// Copyright 2017 Tempo Semiconductor, Inc.
// Author: Steven Eckhoff <steven.eckhoff.opensource@gmail.com>

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "tscs42xx.h"

#define COEFF_SIZE 3
#define BIQUAD_COEFF_COUNT 5
#define BIQUAD_SIZE (COEFF_SIZE * BIQUAD_COEFF_COUNT)

#define COEFF_RAM_MAX_ADDR 0xcd
#define COEFF_RAM_COEFF_COUNT (COEFF_RAM_MAX_ADDR + 1)
#define COEFF_RAM_SIZE (COEFF_SIZE * COEFF_RAM_COEFF_COUNT)

struct tscs42xx {

	int bclk_ratio;
	int samplerate;
	struct mutex audio_params_lock;

	u8 coeff_ram[COEFF_RAM_SIZE];
	bool coeff_ram_synced;
	struct mutex coeff_ram_lock;

	struct mutex pll_lock;

	struct regmap *regmap;

	struct clk *sysclk;
	int sysclk_src_id;
};

struct coeff_ram_ctl {
	unsigned int addr;
	struct soc_bytes_ext bytes_ext;
};

static bool tscs42xx_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case R_DACCRWRL:
	case R_DACCRWRM:
	case R_DACCRWRH:
	case R_DACCRRDL:
	case R_DACCRRDM:
	case R_DACCRRDH:
	case R_DACCRSTAT:
	case R_DACCRADDR:
	case R_PLLCTL0:
		return true;
	default:
		return false;
	};
}

static bool tscs42xx_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case R_DACCRWRL:
	case R_DACCRWRM:
	case R_DACCRWRH:
	case R_DACCRRDL:
	case R_DACCRRDM:
	case R_DACCRRDH:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config tscs42xx_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.volatile_reg = tscs42xx_volatile,
	.precious_reg = tscs42xx_precious,
	.max_register = R_DACMBCREL3H,

	.cache_type = REGCACHE_RBTREE,
	.can_multi_write = true,
};

#define MAX_PLL_LOCK_20MS_WAITS 1
static bool plls_locked(struct snd_soc_component *component)
{
	int ret;
	int count = MAX_PLL_LOCK_20MS_WAITS;

	do {
		ret = snd_soc_component_read32(component, R_PLLCTL0);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to read PLL lock status (%d)\n", ret);
			return false;
		} else if (ret > 0) {
			return true;
		}
		msleep(20);
	} while (count--);

	return false;
}

static int sample_rate_to_pll_freq_out(int sample_rate)
{
	switch (sample_rate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		return 112896000;
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 96000:
		return 122880000;
	default:
		return -EINVAL;
	}
}

#define DACCRSTAT_MAX_TRYS 10
static int write_coeff_ram(struct snd_soc_component *component, u8 *coeff_ram,
	unsigned int addr, unsigned int coeff_cnt)
{
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	int cnt;
	int trys;
	int ret;

	for (cnt = 0; cnt < coeff_cnt; cnt++, addr++) {

		for (trys = 0; trys < DACCRSTAT_MAX_TRYS; trys++) {
			ret = snd_soc_component_read32(component, R_DACCRSTAT);
			if (ret < 0) {
				dev_err(component->dev,
					"Failed to read stat (%d)\n", ret);
				return ret;
			}
			if (!ret)
				break;
		}

		if (trys == DACCRSTAT_MAX_TRYS) {
			ret = -EIO;
			dev_err(component->dev,
				"dac coefficient write error (%d)\n", ret);
			return ret;
		}

		ret = regmap_write(tscs42xx->regmap, R_DACCRADDR, addr);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to write dac ram address (%d)\n", ret);
			return ret;
		}

		ret = regmap_bulk_write(tscs42xx->regmap, R_DACCRWRL,
			&coeff_ram[addr * COEFF_SIZE],
			COEFF_SIZE);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to write dac ram (%d)\n", ret);
			return ret;
		}
	}

	return 0;
}

static int power_up_audio_plls(struct snd_soc_component *component)
{
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	int freq_out;
	int ret;
	unsigned int mask;
	unsigned int val;

	freq_out = sample_rate_to_pll_freq_out(tscs42xx->samplerate);
	switch (freq_out) {
	case 122880000: /* 48k */
		mask = RM_PLLCTL1C_PDB_PLL1;
		val = RV_PLLCTL1C_PDB_PLL1_ENABLE;
		break;
	case 112896000: /* 44.1k */
		mask = RM_PLLCTL1C_PDB_PLL2;
		val = RV_PLLCTL1C_PDB_PLL2_ENABLE;
		break;
	default:
		ret = -EINVAL;
		dev_err(component->dev,
				"Unrecognized PLL output freq (%d)\n", ret);
		return ret;
	}

	mutex_lock(&tscs42xx->pll_lock);

	ret = snd_soc_component_update_bits(component, R_PLLCTL1C, mask, val);
	if (ret < 0) {
		dev_err(component->dev, "Failed to turn PLL on (%d)\n", ret);
		goto exit;
	}

	if (!plls_locked(component)) {
		dev_err(component->dev, "Failed to lock plls\n");
		ret = -ENOMSG;
		goto exit;
	}

	ret = 0;
exit:
	mutex_unlock(&tscs42xx->pll_lock);

	return ret;
}

static int power_down_audio_plls(struct snd_soc_component *component)
{
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	int ret;

	mutex_lock(&tscs42xx->pll_lock);

	ret = snd_soc_component_update_bits(component, R_PLLCTL1C,
			RM_PLLCTL1C_PDB_PLL1,
			RV_PLLCTL1C_PDB_PLL1_DISABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to turn PLL off (%d)\n", ret);
		goto exit;
	}
	ret = snd_soc_component_update_bits(component, R_PLLCTL1C,
			RM_PLLCTL1C_PDB_PLL2,
			RV_PLLCTL1C_PDB_PLL2_DISABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to turn PLL off (%d)\n", ret);
		goto exit;
	}

	ret = 0;
exit:
	mutex_unlock(&tscs42xx->pll_lock);

	return ret;
}

static int coeff_ram_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	struct coeff_ram_ctl *ctl =
		(struct coeff_ram_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;

	mutex_lock(&tscs42xx->coeff_ram_lock);

	memcpy(ucontrol->value.bytes.data,
		&tscs42xx->coeff_ram[ctl->addr * COEFF_SIZE], params->max);

	mutex_unlock(&tscs42xx->coeff_ram_lock);

	return 0;
}

static int coeff_ram_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	struct coeff_ram_ctl *ctl =
		(struct coeff_ram_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	unsigned int coeff_cnt = params->max / COEFF_SIZE;
	int ret;

	mutex_lock(&tscs42xx->coeff_ram_lock);

	tscs42xx->coeff_ram_synced = false;

	memcpy(&tscs42xx->coeff_ram[ctl->addr * COEFF_SIZE],
		ucontrol->value.bytes.data, params->max);

	mutex_lock(&tscs42xx->pll_lock);

	if (plls_locked(component)) {
		ret = write_coeff_ram(component, tscs42xx->coeff_ram,
			ctl->addr, coeff_cnt);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to flush coeff ram cache (%d)\n", ret);
			goto exit;
		}
		tscs42xx->coeff_ram_synced = true;
	}

	ret = 0;
exit:
	mutex_unlock(&tscs42xx->pll_lock);

	mutex_unlock(&tscs42xx->coeff_ram_lock);

	return ret;
}

/* Input L Capture Route */
static char const * const input_select_text[] = {
	"Line 1", "Line 2", "Line 3", "D2S"
};

static const struct soc_enum left_input_select_enum =
SOC_ENUM_SINGLE(R_INSELL, FB_INSELL, ARRAY_SIZE(input_select_text),
		input_select_text);

static const struct snd_kcontrol_new left_input_select =
SOC_DAPM_ENUM("LEFT_INPUT_SELECT_ENUM", left_input_select_enum);

/* Input R Capture Route */
static const struct soc_enum right_input_select_enum =
SOC_ENUM_SINGLE(R_INSELR, FB_INSELR, ARRAY_SIZE(input_select_text),
		input_select_text);

static const struct snd_kcontrol_new right_input_select =
SOC_DAPM_ENUM("RIGHT_INPUT_SELECT_ENUM", right_input_select_enum);

/* Input Channel Mapping */
static char const * const ch_map_select_text[] = {
	"Normal", "Left to Right", "Right to Left", "Swap"
};

static const struct soc_enum ch_map_select_enum =
SOC_ENUM_SINGLE(R_AIC2, FB_AIC2_ADCDSEL, ARRAY_SIZE(ch_map_select_text),
		ch_map_select_text);

static int dapm_vref_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	msleep(20);
	return 0;
}

static int dapm_micb_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	msleep(20);
	return 0;
}

static int pll_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	int ret;

	if (SND_SOC_DAPM_EVENT_ON(event))
		ret = power_up_audio_plls(component);
	else
		ret = power_down_audio_plls(component);

	return ret;
}

static int dac_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	int ret;

	mutex_lock(&tscs42xx->coeff_ram_lock);

	if (tscs42xx->coeff_ram_synced == false) {
		ret = write_coeff_ram(component, tscs42xx->coeff_ram, 0x00,
			COEFF_RAM_COEFF_COUNT);
		if (ret < 0)
			goto exit;
		tscs42xx->coeff_ram_synced = true;
	}

	ret = 0;
exit:
	mutex_unlock(&tscs42xx->coeff_ram_lock);

	return ret;
}

static const struct snd_soc_dapm_widget tscs42xx_dapm_widgets[] = {
	/* Vref */
	SND_SOC_DAPM_SUPPLY_S("Vref", 1, R_PWRM2, FB_PWRM2_VREF, 0,
		dapm_vref_event, SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),

	/* PLL */
	SND_SOC_DAPM_SUPPLY("PLL", SND_SOC_NOPM, 0, 0, pll_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Headphone */
	SND_SOC_DAPM_DAC_E("DAC L", "HiFi Playback", R_PWRM2, FB_PWRM2_HPL, 0,
			dac_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC_E("DAC R", "HiFi Playback", R_PWRM2, FB_PWRM2_HPR, 0,
			dac_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("Headphone L"),
	SND_SOC_DAPM_OUTPUT("Headphone R"),

	/* Speaker */
	SND_SOC_DAPM_DAC_E("ClassD L", "HiFi Playback",
		R_PWRM2, FB_PWRM2_SPKL, 0,
		dac_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC_E("ClassD R", "HiFi Playback",
		R_PWRM2, FB_PWRM2_SPKR, 0,
		dac_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("Speaker L"),
	SND_SOC_DAPM_OUTPUT("Speaker R"),

	/* Capture */
	SND_SOC_DAPM_PGA("Analog In PGA L", R_PWRM1, FB_PWRM1_PGAL, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Analog In PGA R", R_PWRM1, FB_PWRM1_PGAR, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Analog Boost L", R_PWRM1, FB_PWRM1_BSTL, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Analog Boost R", R_PWRM1, FB_PWRM1_BSTR, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC Mute", R_CNVRTR0, FB_CNVRTR0_HPOR, true, NULL, 0),
	SND_SOC_DAPM_ADC("ADC L", "HiFi Capture", R_PWRM1, FB_PWRM1_ADCL, 0),
	SND_SOC_DAPM_ADC("ADC R", "HiFi Capture", R_PWRM1, FB_PWRM1_ADCR, 0),

	/* Capture Input */
	SND_SOC_DAPM_MUX("Input L Capture Route", R_PWRM2,
			FB_PWRM2_INSELL, 0, &left_input_select),
	SND_SOC_DAPM_MUX("Input R Capture Route", R_PWRM2,
			FB_PWRM2_INSELR, 0, &right_input_select),

	/* Digital Mic */
	SND_SOC_DAPM_SUPPLY_S("Digital Mic Enable", 2, R_DMICCTL,
		FB_DMICCTL_DMICEN, 0, NULL,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),

	/* Analog Mic */
	SND_SOC_DAPM_SUPPLY_S("Mic Bias", 2, R_PWRM1, FB_PWRM1_MICB,
		0, dapm_micb_event, SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_PRE_PMD),

	/* Line In */
	SND_SOC_DAPM_INPUT("Line In 1 L"),
	SND_SOC_DAPM_INPUT("Line In 1 R"),
	SND_SOC_DAPM_INPUT("Line In 2 L"),
	SND_SOC_DAPM_INPUT("Line In 2 R"),
	SND_SOC_DAPM_INPUT("Line In 3 L"),
	SND_SOC_DAPM_INPUT("Line In 3 R"),
};

static const struct snd_soc_dapm_route tscs42xx_intercon[] = {
	{"DAC L", NULL, "PLL"},
	{"DAC R", NULL, "PLL"},
	{"DAC L", NULL, "Vref"},
	{"DAC R", NULL, "Vref"},
	{"Headphone L", NULL, "DAC L"},
	{"Headphone R", NULL, "DAC R"},

	{"ClassD L", NULL, "PLL"},
	{"ClassD R", NULL, "PLL"},
	{"ClassD L", NULL, "Vref"},
	{"ClassD R", NULL, "Vref"},
	{"Speaker L", NULL, "ClassD L"},
	{"Speaker R", NULL, "ClassD R"},

	{"Input L Capture Route", NULL, "Vref"},
	{"Input R Capture Route", NULL, "Vref"},

	{"Mic Bias", NULL, "Vref"},

	{"Input L Capture Route", "Line 1", "Line In 1 L"},
	{"Input R Capture Route", "Line 1", "Line In 1 R"},
	{"Input L Capture Route", "Line 2", "Line In 2 L"},
	{"Input R Capture Route", "Line 2", "Line In 2 R"},
	{"Input L Capture Route", "Line 3", "Line In 3 L"},
	{"Input R Capture Route", "Line 3", "Line In 3 R"},

	{"Analog In PGA L", NULL, "Input L Capture Route"},
	{"Analog In PGA R", NULL, "Input R Capture Route"},
	{"Analog Boost L", NULL, "Analog In PGA L"},
	{"Analog Boost R", NULL, "Analog In PGA R"},
	{"ADC Mute", NULL, "Analog Boost L"},
	{"ADC Mute", NULL, "Analog Boost R"},
	{"ADC L", NULL, "PLL"},
	{"ADC R", NULL, "PLL"},
	{"ADC L", NULL, "ADC Mute"},
	{"ADC R", NULL, "ADC Mute"},
};

/************
 * CONTROLS *
 ************/

static char const * const eq_band_enable_text[] = {
	"Prescale only",
	"Band1",
	"Band1:2",
	"Band1:3",
	"Band1:4",
	"Band1:5",
	"Band1:6",
};

static char const * const level_detection_text[] = {
	"Average",
	"Peak",
};

static char const * const level_detection_window_text[] = {
	"512 Samples",
	"64 Samples",
};

static char const * const compressor_ratio_text[] = {
	"Reserved", "1.5:1", "2:1", "3:1", "4:1", "5:1", "6:1",
	"7:1", "8:1", "9:1", "10:1", "11:1", "12:1", "13:1", "14:1",
	"15:1", "16:1", "17:1", "18:1", "19:1", "20:1",
};

static DECLARE_TLV_DB_SCALE(hpvol_scale, -8850, 75, 0);
static DECLARE_TLV_DB_SCALE(spkvol_scale, -7725, 75, 0);
static DECLARE_TLV_DB_SCALE(dacvol_scale, -9563, 38, 0);
static DECLARE_TLV_DB_SCALE(adcvol_scale, -7125, 38, 0);
static DECLARE_TLV_DB_SCALE(invol_scale, -1725, 75, 0);
static DECLARE_TLV_DB_SCALE(mic_boost_scale, 0, 1000, 0);
static DECLARE_TLV_DB_MINMAX(mugain_scale, 0, 4650);
static DECLARE_TLV_DB_MINMAX(compth_scale, -9562, 0);

static const struct soc_enum eq1_band_enable_enum =
	SOC_ENUM_SINGLE(R_CONFIG1, FB_CONFIG1_EQ1_BE,
		ARRAY_SIZE(eq_band_enable_text), eq_band_enable_text);

static const struct soc_enum eq2_band_enable_enum =
	SOC_ENUM_SINGLE(R_CONFIG1, FB_CONFIG1_EQ2_BE,
		ARRAY_SIZE(eq_band_enable_text), eq_band_enable_text);

static const struct soc_enum cle_level_detection_enum =
	SOC_ENUM_SINGLE(R_CLECTL, FB_CLECTL_LVL_MODE,
		ARRAY_SIZE(level_detection_text),
		level_detection_text);

static const struct soc_enum cle_level_detection_window_enum =
	SOC_ENUM_SINGLE(R_CLECTL, FB_CLECTL_WINDOWSEL,
		ARRAY_SIZE(level_detection_window_text),
		level_detection_window_text);

static const struct soc_enum mbc_level_detection_enums[] = {
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_LVLMODE1,
		ARRAY_SIZE(level_detection_text),
			level_detection_text),
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_LVLMODE2,
		ARRAY_SIZE(level_detection_text),
			level_detection_text),
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_LVLMODE3,
		ARRAY_SIZE(level_detection_text),
			level_detection_text),
};

static const struct soc_enum mbc_level_detection_window_enums[] = {
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_WINSEL1,
		ARRAY_SIZE(level_detection_window_text),
			level_detection_window_text),
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_WINSEL2,
		ARRAY_SIZE(level_detection_window_text),
			level_detection_window_text),
	SOC_ENUM_SINGLE(R_DACMBCCTL, FB_DACMBCCTL_WINSEL3,
		ARRAY_SIZE(level_detection_window_text),
			level_detection_window_text),
};

static const struct soc_enum compressor_ratio_enum =
	SOC_ENUM_SINGLE(R_CMPRAT, FB_CMPRAT,
		ARRAY_SIZE(compressor_ratio_text), compressor_ratio_text);

static const struct soc_enum dac_mbc1_compressor_ratio_enum =
	SOC_ENUM_SINGLE(R_DACMBCRAT1, FB_DACMBCRAT1_RATIO,
		ARRAY_SIZE(compressor_ratio_text), compressor_ratio_text);

static const struct soc_enum dac_mbc2_compressor_ratio_enum =
	SOC_ENUM_SINGLE(R_DACMBCRAT2, FB_DACMBCRAT2_RATIO,
		ARRAY_SIZE(compressor_ratio_text), compressor_ratio_text);

static const struct soc_enum dac_mbc3_compressor_ratio_enum =
	SOC_ENUM_SINGLE(R_DACMBCRAT3, FB_DACMBCRAT3_RATIO,
		ARRAY_SIZE(compressor_ratio_text), compressor_ratio_text);

static int bytes_info_ext(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *ucontrol)
{
	struct coeff_ram_ctl *ctl =
		(struct coeff_ram_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;

	ucontrol->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	ucontrol->count = params->max;

	return 0;
}

#define COEFF_RAM_CTL(xname, xcount, xaddr) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = bytes_info_ext, \
	.get = coeff_ram_get, .put = coeff_ram_put, \
	.private_value = (unsigned long)&(struct coeff_ram_ctl) { \
		.addr = xaddr, \
		.bytes_ext = {.max = xcount, }, \
	} \
}

static const struct snd_kcontrol_new tscs42xx_snd_controls[] = {
	/* Volumes */
	SOC_DOUBLE_R_TLV("Headphone Volume", R_HPVOLL, R_HPVOLR,
			FB_HPVOLL, 0x7F, 0, hpvol_scale),
	SOC_DOUBLE_R_TLV("Speaker Volume", R_SPKVOLL, R_SPKVOLR,
			FB_SPKVOLL, 0x7F, 0, spkvol_scale),
	SOC_DOUBLE_R_TLV("Master Volume", R_DACVOLL, R_DACVOLR,
			FB_DACVOLL, 0xFF, 0, dacvol_scale),
	SOC_DOUBLE_R_TLV("PCM Volume", R_ADCVOLL, R_ADCVOLR,
			FB_ADCVOLL, 0xFF, 0, adcvol_scale),
	SOC_DOUBLE_R_TLV("Input Volume", R_INVOLL, R_INVOLR,
			FB_INVOLL, 0x3F, 0, invol_scale),

	/* INSEL */
	SOC_DOUBLE_R_TLV("Mic Boost Volume", R_INSELL, R_INSELR,
			FB_INSELL_MICBSTL, FV_INSELL_MICBSTL_30DB,
			0, mic_boost_scale),

	/* Input Channel Map */
	SOC_ENUM("Input Channel Map", ch_map_select_enum),

	/* Mic Bias */
	SOC_SINGLE("Mic Bias Boost Switch", 0x71, 0x07, 1, 0),

	/* Headphone Auto Switching */
	SOC_SINGLE("Headphone Auto Switching Switch",
			R_CTL, FB_CTL_HPSWEN, 1, 0),
	SOC_SINGLE("Headphone Detect Polarity Toggle Switch",
			R_CTL, FB_CTL_HPSWPOL, 1, 0),

	/* Coefficient Ram */
	COEFF_RAM_CTL("Cascade1L BiQuad1", BIQUAD_SIZE, 0x00),
	COEFF_RAM_CTL("Cascade1L BiQuad2", BIQUAD_SIZE, 0x05),
	COEFF_RAM_CTL("Cascade1L BiQuad3", BIQUAD_SIZE, 0x0a),
	COEFF_RAM_CTL("Cascade1L BiQuad4", BIQUAD_SIZE, 0x0f),
	COEFF_RAM_CTL("Cascade1L BiQuad5", BIQUAD_SIZE, 0x14),
	COEFF_RAM_CTL("Cascade1L BiQuad6", BIQUAD_SIZE, 0x19),

	COEFF_RAM_CTL("Cascade1R BiQuad1", BIQUAD_SIZE, 0x20),
	COEFF_RAM_CTL("Cascade1R BiQuad2", BIQUAD_SIZE, 0x25),
	COEFF_RAM_CTL("Cascade1R BiQuad3", BIQUAD_SIZE, 0x2a),
	COEFF_RAM_CTL("Cascade1R BiQuad4", BIQUAD_SIZE, 0x2f),
	COEFF_RAM_CTL("Cascade1R BiQuad5", BIQUAD_SIZE, 0x34),
	COEFF_RAM_CTL("Cascade1R BiQuad6", BIQUAD_SIZE, 0x39),

	COEFF_RAM_CTL("Cascade1L Prescale", COEFF_SIZE, 0x1f),
	COEFF_RAM_CTL("Cascade1R Prescale", COEFF_SIZE, 0x3f),

	COEFF_RAM_CTL("Cascade2L BiQuad1", BIQUAD_SIZE, 0x40),
	COEFF_RAM_CTL("Cascade2L BiQuad2", BIQUAD_SIZE, 0x45),
	COEFF_RAM_CTL("Cascade2L BiQuad3", BIQUAD_SIZE, 0x4a),
	COEFF_RAM_CTL("Cascade2L BiQuad4", BIQUAD_SIZE, 0x4f),
	COEFF_RAM_CTL("Cascade2L BiQuad5", BIQUAD_SIZE, 0x54),
	COEFF_RAM_CTL("Cascade2L BiQuad6", BIQUAD_SIZE, 0x59),

	COEFF_RAM_CTL("Cascade2R BiQuad1", BIQUAD_SIZE, 0x60),
	COEFF_RAM_CTL("Cascade2R BiQuad2", BIQUAD_SIZE, 0x65),
	COEFF_RAM_CTL("Cascade2R BiQuad3", BIQUAD_SIZE, 0x6a),
	COEFF_RAM_CTL("Cascade2R BiQuad4", BIQUAD_SIZE, 0x6f),
	COEFF_RAM_CTL("Cascade2R BiQuad5", BIQUAD_SIZE, 0x74),
	COEFF_RAM_CTL("Cascade2R BiQuad6", BIQUAD_SIZE, 0x79),

	COEFF_RAM_CTL("Cascade2L Prescale", COEFF_SIZE, 0x5f),
	COEFF_RAM_CTL("Cascade2R Prescale", COEFF_SIZE, 0x7f),

	COEFF_RAM_CTL("Bass Extraction BiQuad1", BIQUAD_SIZE, 0x80),
	COEFF_RAM_CTL("Bass Extraction BiQuad2", BIQUAD_SIZE, 0x85),

	COEFF_RAM_CTL("Bass Non Linear Function 1", COEFF_SIZE, 0x8a),
	COEFF_RAM_CTL("Bass Non Linear Function 2", COEFF_SIZE, 0x8b),

	COEFF_RAM_CTL("Bass Limiter BiQuad", BIQUAD_SIZE, 0x8c),

	COEFF_RAM_CTL("Bass Cut Off BiQuad", BIQUAD_SIZE, 0x91),

	COEFF_RAM_CTL("Bass Mix", COEFF_SIZE, 0x96),

	COEFF_RAM_CTL("Treb Extraction BiQuad1", BIQUAD_SIZE, 0x97),
	COEFF_RAM_CTL("Treb Extraction BiQuad2", BIQUAD_SIZE, 0x9c),

	COEFF_RAM_CTL("Treb Non Linear Function 1", COEFF_SIZE, 0xa1),
	COEFF_RAM_CTL("Treb Non Linear Function 2", COEFF_SIZE, 0xa2),

	COEFF_RAM_CTL("Treb Limiter BiQuad", BIQUAD_SIZE, 0xa3),

	COEFF_RAM_CTL("Treb Cut Off BiQuad", BIQUAD_SIZE, 0xa8),

	COEFF_RAM_CTL("Treb Mix", COEFF_SIZE, 0xad),

	COEFF_RAM_CTL("3D", COEFF_SIZE, 0xae),

	COEFF_RAM_CTL("3D Mix", COEFF_SIZE, 0xaf),

	COEFF_RAM_CTL("MBC1 BiQuad1", BIQUAD_SIZE, 0xb0),
	COEFF_RAM_CTL("MBC1 BiQuad2", BIQUAD_SIZE, 0xb5),

	COEFF_RAM_CTL("MBC2 BiQuad1", BIQUAD_SIZE, 0xba),
	COEFF_RAM_CTL("MBC2 BiQuad2", BIQUAD_SIZE, 0xbf),

	COEFF_RAM_CTL("MBC3 BiQuad1", BIQUAD_SIZE, 0xc4),
	COEFF_RAM_CTL("MBC3 BiQuad2", BIQUAD_SIZE, 0xc9),

	/* EQ */
	SOC_SINGLE("EQ1 Switch", R_CONFIG1, FB_CONFIG1_EQ1_EN, 1, 0),
	SOC_SINGLE("EQ2 Switch", R_CONFIG1, FB_CONFIG1_EQ2_EN, 1, 0),
	SOC_ENUM("EQ1 Band Enable", eq1_band_enable_enum),
	SOC_ENUM("EQ2 Band Enable", eq2_band_enable_enum),

	/* CLE */
	SOC_ENUM("CLE Level Detect",
		cle_level_detection_enum),
	SOC_ENUM("CLE Level Detect Win",
		cle_level_detection_window_enum),
	SOC_SINGLE("Expander Switch",
		R_CLECTL, FB_CLECTL_EXP_EN, 1, 0),
	SOC_SINGLE("Limiter Switch",
		R_CLECTL, FB_CLECTL_LIMIT_EN, 1, 0),
	SOC_SINGLE("Comp Switch",
		R_CLECTL, FB_CLECTL_COMP_EN, 1, 0),
	SOC_SINGLE_TLV("CLE Make-Up Gain Volume",
		R_MUGAIN, FB_MUGAIN_CLEMUG, 0x1f, 0, mugain_scale),
	SOC_SINGLE_TLV("Comp Thresh Volume",
		R_COMPTH, FB_COMPTH, 0xff, 0, compth_scale),
	SOC_ENUM("Comp Ratio", compressor_ratio_enum),
	SND_SOC_BYTES("Comp Atk Time", R_CATKTCL, 2),

	/* Effects */
	SOC_SINGLE("3D Switch", R_FXCTL, FB_FXCTL_3DEN, 1, 0),
	SOC_SINGLE("Treble Switch", R_FXCTL, FB_FXCTL_TEEN, 1, 0),
	SOC_SINGLE("Treble Bypass Switch", R_FXCTL, FB_FXCTL_TNLFBYPASS, 1, 0),
	SOC_SINGLE("Bass Switch", R_FXCTL, FB_FXCTL_BEEN, 1, 0),
	SOC_SINGLE("Bass Bypass Switch", R_FXCTL, FB_FXCTL_BNLFBYPASS, 1, 0),

	/* MBC */
	SOC_SINGLE("MBC Band1 Switch", R_DACMBCEN, FB_DACMBCEN_MBCEN1, 1, 0),
	SOC_SINGLE("MBC Band2 Switch", R_DACMBCEN, FB_DACMBCEN_MBCEN2, 1, 0),
	SOC_SINGLE("MBC Band3 Switch", R_DACMBCEN, FB_DACMBCEN_MBCEN3, 1, 0),
	SOC_ENUM("MBC Band1 Level Detect",
		mbc_level_detection_enums[0]),
	SOC_ENUM("MBC Band2 Level Detect",
		mbc_level_detection_enums[1]),
	SOC_ENUM("MBC Band3 Level Detect",
		mbc_level_detection_enums[2]),
	SOC_ENUM("MBC Band1 Level Detect Win",
		mbc_level_detection_window_enums[0]),
	SOC_ENUM("MBC Band2 Level Detect Win",
		mbc_level_detection_window_enums[1]),
	SOC_ENUM("MBC Band3 Level Detect Win",
		mbc_level_detection_window_enums[2]),

	SOC_SINGLE("MBC1 Phase Invert Switch",
		R_DACMBCMUG1, FB_DACMBCMUG1_PHASE, 1, 0),
	SOC_SINGLE_TLV("DAC MBC1 Make-Up Gain Volume",
		R_DACMBCMUG1, FB_DACMBCMUG1_MUGAIN, 0x1f, 0, mugain_scale),
	SOC_SINGLE_TLV("DAC MBC1 Comp Thresh Volume",
		R_DACMBCTHR1, FB_DACMBCTHR1_THRESH, 0xff, 0, compth_scale),
	SOC_ENUM("DAC MBC1 Comp Ratio",
		dac_mbc1_compressor_ratio_enum),
	SND_SOC_BYTES("DAC MBC1 Comp Atk Time", R_DACMBCATK1L, 2),
	SND_SOC_BYTES("DAC MBC1 Comp Rel Time Const",
		R_DACMBCREL1L, 2),

	SOC_SINGLE("MBC2 Phase Invert Switch",
		R_DACMBCMUG2, FB_DACMBCMUG2_PHASE, 1, 0),
	SOC_SINGLE_TLV("DAC MBC2 Make-Up Gain Volume",
		R_DACMBCMUG2, FB_DACMBCMUG2_MUGAIN, 0x1f, 0, mugain_scale),
	SOC_SINGLE_TLV("DAC MBC2 Comp Thresh Volume",
		R_DACMBCTHR2, FB_DACMBCTHR2_THRESH, 0xff, 0, compth_scale),
	SOC_ENUM("DAC MBC2 Comp Ratio",
		dac_mbc2_compressor_ratio_enum),
	SND_SOC_BYTES("DAC MBC2 Comp Atk Time", R_DACMBCATK2L, 2),
	SND_SOC_BYTES("DAC MBC2 Comp Rel Time Const",
		R_DACMBCREL2L, 2),

	SOC_SINGLE("MBC3 Phase Invert Switch",
		R_DACMBCMUG3, FB_DACMBCMUG3_PHASE, 1, 0),
	SOC_SINGLE_TLV("DAC MBC3 Make-Up Gain Volume",
		R_DACMBCMUG3, FB_DACMBCMUG3_MUGAIN, 0x1f, 0, mugain_scale),
	SOC_SINGLE_TLV("DAC MBC3 Comp Thresh Volume",
		R_DACMBCTHR3, FB_DACMBCTHR3_THRESH, 0xff, 0, compth_scale),
	SOC_ENUM("DAC MBC3 Comp Ratio",
		dac_mbc3_compressor_ratio_enum),
	SND_SOC_BYTES("DAC MBC3 Comp Atk Time", R_DACMBCATK3L, 2),
	SND_SOC_BYTES("DAC MBC3 Comp Rel Time Const",
		R_DACMBCREL3L, 2),
};

static int setup_sample_format(struct snd_soc_component *component,
		snd_pcm_format_t format)
{
	unsigned int width;
	int ret;

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		width = RV_AIC1_WL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		width = RV_AIC1_WL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		width = RV_AIC1_WL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		width = RV_AIC1_WL_32;
		break;
	default:
		ret = -EINVAL;
		dev_err(component->dev, "Unsupported format width (%d)\n", ret);
		return ret;
	}
	ret = snd_soc_component_update_bits(component,
			R_AIC1, RM_AIC1_WL, width);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to set sample width (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int setup_sample_rate(struct snd_soc_component *component,
		unsigned int rate)
{
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	unsigned int br, bm;
	int ret;

	switch (rate) {
	case 8000:
		br = RV_DACSR_DBR_32;
		bm = RV_DACSR_DBM_PT25;
		break;
	case 16000:
		br = RV_DACSR_DBR_32;
		bm = RV_DACSR_DBM_PT5;
		break;
	case 24000:
		br = RV_DACSR_DBR_48;
		bm = RV_DACSR_DBM_PT5;
		break;
	case 32000:
		br = RV_DACSR_DBR_32;
		bm = RV_DACSR_DBM_1;
		break;
	case 48000:
		br = RV_DACSR_DBR_48;
		bm = RV_DACSR_DBM_1;
		break;
	case 96000:
		br = RV_DACSR_DBR_48;
		bm = RV_DACSR_DBM_2;
		break;
	case 11025:
		br = RV_DACSR_DBR_44_1;
		bm = RV_DACSR_DBM_PT25;
		break;
	case 22050:
		br = RV_DACSR_DBR_44_1;
		bm = RV_DACSR_DBM_PT5;
		break;
	case 44100:
		br = RV_DACSR_DBR_44_1;
		bm = RV_DACSR_DBM_1;
		break;
	case 88200:
		br = RV_DACSR_DBR_44_1;
		bm = RV_DACSR_DBM_2;
		break;
	default:
		dev_err(component->dev, "Unsupported sample rate %d\n", rate);
		return -EINVAL;
	}

	/* DAC and ADC share bit and frame clock */
	ret = snd_soc_component_update_bits(component,
			R_DACSR, RM_DACSR_DBR, br);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to update register (%d)\n", ret);
		return ret;
	}
	ret = snd_soc_component_update_bits(component,
			R_DACSR, RM_DACSR_DBM, bm);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to update register (%d)\n", ret);
		return ret;
	}
	ret = snd_soc_component_update_bits(component,
			R_ADCSR, RM_DACSR_DBR, br);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to update register (%d)\n", ret);
		return ret;
	}
	ret = snd_soc_component_update_bits(component,
			R_ADCSR, RM_DACSR_DBM, bm);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to update register (%d)\n", ret);
		return ret;
	}

	mutex_lock(&tscs42xx->audio_params_lock);

	tscs42xx->samplerate = rate;

	mutex_unlock(&tscs42xx->audio_params_lock);

	return 0;
}

struct reg_setting {
	unsigned int addr;
	unsigned int val;
	unsigned int mask;
};

#define PLL_REG_SETTINGS_COUNT 13
struct pll_ctl {
	int input_freq;
	struct reg_setting settings[PLL_REG_SETTINGS_COUNT];
};

#define PLL_CTL(f, rt, rd, r1b_l, r9, ra, rb,		\
		rc, r12, r1b_h, re, rf, r10, r11)	\
	{						\
		.input_freq = f,			\
		.settings = {				\
			{R_TIMEBASE,  rt,   0xFF},	\
			{R_PLLCTLD,   rd,   0xFF},	\
			{R_PLLCTL1B, r1b_l, 0x0F},	\
			{R_PLLCTL9,   r9,   0xFF},	\
			{R_PLLCTLA,   ra,   0xFF},	\
			{R_PLLCTLB,   rb,   0xFF},	\
			{R_PLLCTLC,   rc,   0xFF},	\
			{R_PLLCTL12, r12,   0xFF},	\
			{R_PLLCTL1B, r1b_h, 0xF0},	\
			{R_PLLCTLE,   re,   0xFF},	\
			{R_PLLCTLF,   rf,   0xFF},	\
			{R_PLLCTL10, r10,   0xFF},	\
			{R_PLLCTL11, r11,   0xFF},	\
		},					\
	}

static const struct pll_ctl pll_ctls[] = {
	PLL_CTL(1411200, 0x05,
		0x39, 0x04, 0x07, 0x02, 0xC3, 0x04,
		0x1B, 0x10, 0x03, 0x03, 0xD0, 0x02),
	PLL_CTL(1536000, 0x05,
		0x1A, 0x04, 0x02, 0x03, 0xE0, 0x01,
		0x1A, 0x10, 0x02, 0x03, 0xB9, 0x01),
	PLL_CTL(2822400, 0x0A,
		0x23, 0x04, 0x07, 0x04, 0xC3, 0x04,
		0x22, 0x10, 0x05, 0x03, 0x58, 0x02),
	PLL_CTL(3072000, 0x0B,
		0x22, 0x04, 0x07, 0x03, 0x48, 0x03,
		0x1A, 0x10, 0x04, 0x03, 0xB9, 0x01),
	PLL_CTL(5644800, 0x15,
		0x23, 0x04, 0x0E, 0x04, 0xC3, 0x04,
		0x1A, 0x10, 0x08, 0x03, 0xE0, 0x01),
	PLL_CTL(6144000, 0x17,
		0x1A, 0x04, 0x08, 0x03, 0xE0, 0x01,
		0x1A, 0x10, 0x08, 0x03, 0xB9, 0x01),
	PLL_CTL(12000000, 0x2E,
		0x1B, 0x04, 0x19, 0x03, 0x00, 0x03,
		0x2A, 0x10, 0x19, 0x05, 0x98, 0x04),
	PLL_CTL(19200000, 0x4A,
		0x13, 0x04, 0x14, 0x03, 0x80, 0x01,
		0x1A, 0x10, 0x19, 0x03, 0xB9, 0x01),
	PLL_CTL(22000000, 0x55,
		0x2A, 0x04, 0x37, 0x05, 0x00, 0x06,
		0x22, 0x10, 0x26, 0x03, 0x49, 0x02),
	PLL_CTL(22579200, 0x57,
		0x22, 0x04, 0x31, 0x03, 0x20, 0x03,
		0x1A, 0x10, 0x1D, 0x03, 0xB3, 0x01),
	PLL_CTL(24000000, 0x5D,
		0x13, 0x04, 0x19, 0x03, 0x80, 0x01,
		0x1B, 0x10, 0x19, 0x05, 0x4C, 0x02),
	PLL_CTL(24576000, 0x5F,
		0x13, 0x04, 0x1D, 0x03, 0xB3, 0x01,
		0x22, 0x10, 0x40, 0x03, 0x72, 0x03),
	PLL_CTL(27000000, 0x68,
		0x22, 0x04, 0x4B, 0x03, 0x00, 0x04,
		0x2A, 0x10, 0x7D, 0x03, 0x20, 0x06),
	PLL_CTL(36000000, 0x8C,
		0x1B, 0x04, 0x4B, 0x03, 0x00, 0x03,
		0x2A, 0x10, 0x7D, 0x03, 0x98, 0x04),
	PLL_CTL(25000000, 0x61,
		0x1B, 0x04, 0x37, 0x03, 0x2B, 0x03,
		0x1A, 0x10, 0x2A, 0x03, 0x39, 0x02),
	PLL_CTL(26000000, 0x65,
		0x23, 0x04, 0x41, 0x05, 0x00, 0x06,
		0x1A, 0x10, 0x26, 0x03, 0xEF, 0x01),
	PLL_CTL(12288000, 0x2F,
		0x1A, 0x04, 0x12, 0x03, 0x1C, 0x02,
		0x22, 0x10, 0x20, 0x03, 0x72, 0x03),
	PLL_CTL(40000000, 0x9B,
		0x22, 0x08, 0x7D, 0x03, 0x80, 0x04,
		0x23, 0x10, 0x7D, 0x05, 0xE4, 0x06),
	PLL_CTL(512000, 0x01,
		0x22, 0x04, 0x01, 0x03, 0xD0, 0x02,
		0x1B, 0x10, 0x01, 0x04, 0x72, 0x03),
	PLL_CTL(705600, 0x02,
		0x22, 0x04, 0x02, 0x03, 0x15, 0x04,
		0x22, 0x10, 0x01, 0x04, 0x80, 0x02),
	PLL_CTL(1024000, 0x03,
		0x22, 0x04, 0x02, 0x03, 0xD0, 0x02,
		0x1B, 0x10, 0x02, 0x04, 0x72, 0x03),
	PLL_CTL(2048000, 0x07,
		0x22, 0x04, 0x04, 0x03, 0xD0, 0x02,
		0x1B, 0x10, 0x04, 0x04, 0x72, 0x03),
	PLL_CTL(2400000, 0x08,
		0x22, 0x04, 0x05, 0x03, 0x00, 0x03,
		0x23, 0x10, 0x05, 0x05, 0x98, 0x04),
};

static const struct pll_ctl *get_pll_ctl(int input_freq)
{
	int i;
	const struct pll_ctl *pll_ctl = NULL;

	for (i = 0; i < ARRAY_SIZE(pll_ctls); ++i)
		if (input_freq == pll_ctls[i].input_freq) {
			pll_ctl = &pll_ctls[i];
			break;
		}

	return pll_ctl;
}

static int set_pll_ctl_from_input_freq(struct snd_soc_component *component,
		const int input_freq)
{
	int ret;
	int i;
	const struct pll_ctl *pll_ctl;

	pll_ctl = get_pll_ctl(input_freq);
	if (!pll_ctl) {
		ret = -EINVAL;
		dev_err(component->dev, "No PLL input entry for %d (%d)\n",
			input_freq, ret);
		return ret;
	}

	for (i = 0; i < PLL_REG_SETTINGS_COUNT; ++i) {
		ret = snd_soc_component_update_bits(component,
			pll_ctl->settings[i].addr,
			pll_ctl->settings[i].mask,
			pll_ctl->settings[i].val);
		if (ret < 0) {
			dev_err(component->dev, "Failed to set pll ctl (%d)\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int tscs42xx_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *codec_dai)
{
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	ret = setup_sample_format(component, params_format(params));
	if (ret < 0) {
		dev_err(component->dev, "Failed to setup sample format (%d)\n",
			ret);
		return ret;
	}

	ret = setup_sample_rate(component, params_rate(params));
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to setup sample rate (%d)\n", ret);
		return ret;
	}

	return 0;
}

static inline int dac_mute(struct snd_soc_component *component)
{
	int ret;

	ret = snd_soc_component_update_bits(component,
			R_CNVRTR1, RM_CNVRTR1_DACMU,
		RV_CNVRTR1_DACMU_ENABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to mute DAC (%d)\n",
				ret);
		return ret;
	}

	return 0;
}

static inline int dac_unmute(struct snd_soc_component *component)
{
	int ret;

	ret = snd_soc_component_update_bits(component,
			R_CNVRTR1, RM_CNVRTR1_DACMU,
		RV_CNVRTR1_DACMU_DISABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to unmute DAC (%d)\n",
				ret);
		return ret;
	}

	return 0;
}

static inline int adc_mute(struct snd_soc_component *component)
{
	int ret;

	ret = snd_soc_component_update_bits(component,
			R_CNVRTR0, RM_CNVRTR0_ADCMU, RV_CNVRTR0_ADCMU_ENABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to mute ADC (%d)\n",
				ret);
		return ret;
	}

	return 0;
}

static inline int adc_unmute(struct snd_soc_component *component)
{
	int ret;

	ret = snd_soc_component_update_bits(component,
			R_CNVRTR0, RM_CNVRTR0_ADCMU, RV_CNVRTR0_ADCMU_DISABLE);
	if (ret < 0) {
		dev_err(component->dev, "Failed to unmute ADC (%d)\n",
				ret);
		return ret;
	}

	return 0;
}

static int tscs42xx_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	if (mute)
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = dac_mute(component);
		else
			ret = adc_mute(component);
	else
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = dac_unmute(component);
		else
			ret = adc_unmute(component);

	return ret;
}

static int tscs42xx_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	int ret;

	/* Slave mode not supported since it needs always-on frame clock */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ret = snd_soc_component_update_bits(component,
				R_AIC1, RM_AIC1_MS, RV_AIC1_MS_MASTER);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to set codec DAI master (%d)\n", ret);
			return ret;
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(component->dev, "Unsupported format (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int tscs42xx_set_dai_bclk_ratio(struct snd_soc_dai *codec_dai,
		unsigned int ratio)
{
	struct snd_soc_component *component = codec_dai->component;
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	unsigned int value;
	int ret = 0;

	switch (ratio) {
	case 32:
		value = RV_DACSR_DBCM_32;
		break;
	case 40:
		value = RV_DACSR_DBCM_40;
		break;
	case 64:
		value = RV_DACSR_DBCM_64;
		break;
	default:
		dev_err(component->dev, "Unsupported bclk ratio (%d)\n", ret);
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component,
			R_DACSR, RM_DACSR_DBCM, value);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to set DAC BCLK ratio (%d)\n", ret);
		return ret;
	}
	ret = snd_soc_component_update_bits(component,
			R_ADCSR, RM_ADCSR_ABCM, value);
	if (ret < 0) {
		dev_err(component->dev,
				"Failed to set ADC BCLK ratio (%d)\n", ret);
		return ret;
	}

	mutex_lock(&tscs42xx->audio_params_lock);

	tscs42xx->bclk_ratio = ratio;

	mutex_unlock(&tscs42xx->audio_params_lock);

	return 0;
}

static const struct snd_soc_dai_ops tscs42xx_dai_ops = {
	.hw_params	= tscs42xx_hw_params,
	.mute_stream	= tscs42xx_mute_stream,
	.set_fmt	= tscs42xx_set_dai_fmt,
	.set_bclk_ratio = tscs42xx_set_dai_bclk_ratio,
};

static int part_is_valid(struct tscs42xx *tscs42xx)
{
	int val;
	int ret;
	unsigned int reg;

	ret = regmap_read(tscs42xx->regmap, R_DEVIDH, &reg);
	if (ret < 0)
		return ret;

	val = reg << 8;
	ret = regmap_read(tscs42xx->regmap, R_DEVIDL, &reg);
	if (ret < 0)
		return ret;

	val |= reg;

	switch (val) {
	case 0x4A74:
	case 0x4A73:
		return true;
	default:
		return false;
	};
}

static int set_sysclk(struct snd_soc_component *component)
{
	struct tscs42xx *tscs42xx = snd_soc_component_get_drvdata(component);
	unsigned long freq;
	int ret;

	switch (tscs42xx->sysclk_src_id) {
	case TSCS42XX_PLL_SRC_XTAL:
	case TSCS42XX_PLL_SRC_MCLK1:
		ret = snd_soc_component_write(component, R_PLLREFSEL,
				RV_PLLREFSEL_PLL1_REF_SEL_XTAL_MCLK1 |
				RV_PLLREFSEL_PLL2_REF_SEL_XTAL_MCLK1);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to set pll reference input (%d)\n",
				ret);
			return ret;
		}
		break;
	case TSCS42XX_PLL_SRC_MCLK2:
		ret = snd_soc_component_write(component, R_PLLREFSEL,
				RV_PLLREFSEL_PLL1_REF_SEL_MCLK2 |
				RV_PLLREFSEL_PLL2_REF_SEL_MCLK2);
		if (ret < 0) {
			dev_err(component->dev,
				"Failed to set PLL reference (%d)\n", ret);
			return ret;
		}
		break;
	default:
		dev_err(component->dev, "pll src is unsupported\n");
		return -EINVAL;
	}

	freq = clk_get_rate(tscs42xx->sysclk);
	ret = set_pll_ctl_from_input_freq(component, freq);
	if (ret < 0) {
		dev_err(component->dev,
			"Failed to setup PLL input freq (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int tscs42xx_probe(struct snd_soc_component *component)
{
	return set_sysclk(component);
}

static const struct snd_soc_component_driver soc_codec_dev_tscs42xx = {
	.probe			= tscs42xx_probe,
	.dapm_widgets		= tscs42xx_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tscs42xx_dapm_widgets),
	.dapm_routes		= tscs42xx_intercon,
	.num_dapm_routes	= ARRAY_SIZE(tscs42xx_intercon),
	.controls		= tscs42xx_snd_controls,
	.num_controls		= ARRAY_SIZE(tscs42xx_snd_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static inline void init_coeff_ram_cache(struct tscs42xx *tscs42xx)
{
	static const u8 norm_addrs[] = {
		0x00, 0x05, 0x0a, 0x0f, 0x14, 0x19, 0x1f, 0x20, 0x25, 0x2a,
		0x2f, 0x34, 0x39, 0x3f, 0x40, 0x45, 0x4a, 0x4f, 0x54, 0x59,
		0x5f, 0x60, 0x65, 0x6a, 0x6f, 0x74, 0x79, 0x7f, 0x80, 0x85,
		0x8c, 0x91, 0x96, 0x97, 0x9c, 0xa3, 0xa8, 0xad, 0xaf, 0xb0,
		0xb5, 0xba, 0xbf, 0xc4, 0xc9,
	};
	u8 *coeff_ram = tscs42xx->coeff_ram;
	int i;

	for (i = 0; i < ARRAY_SIZE(norm_addrs); i++)
		coeff_ram[((norm_addrs[i] + 1) * COEFF_SIZE) - 1] = 0x40;
}

#define TSCS42XX_RATES SNDRV_PCM_RATE_8000_96000

#define TSCS42XX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
	| SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver tscs42xx_dai = {
	.name = "tscs42xx-HiFi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TSCS42XX_RATES,
		.formats = TSCS42XX_FORMATS,},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TSCS42XX_RATES,
		.formats = TSCS42XX_FORMATS,},
	.ops = &tscs42xx_dai_ops,
	.symmetric_rates = 1,
	.symmetric_channels = 1,
	.symmetric_samplebits = 1,
};

static const struct reg_sequence tscs42xx_patch[] = {
	{ R_AIC2, RV_AIC2_BLRCM_DAC_BCLK_LRCLK_SHARED },
};

static char const * const src_names[TSCS42XX_PLL_SRC_CNT] = {
	"xtal", "mclk1", "mclk2"};

static int tscs42xx_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct tscs42xx *tscs42xx;
	int src;
	int ret;

	tscs42xx = devm_kzalloc(&i2c->dev, sizeof(*tscs42xx), GFP_KERNEL);
	if (!tscs42xx) {
		ret = -ENOMEM;
		dev_err(&i2c->dev,
			"Failed to allocate memory for data (%d)\n", ret);
		return ret;
	}
	i2c_set_clientdata(i2c, tscs42xx);

	for (src = TSCS42XX_PLL_SRC_XTAL; src < TSCS42XX_PLL_SRC_CNT; src++) {
		tscs42xx->sysclk = devm_clk_get(&i2c->dev, src_names[src]);
		if (!IS_ERR(tscs42xx->sysclk)) {
			break;
		} else if (PTR_ERR(tscs42xx->sysclk) != -ENOENT) {
			ret = PTR_ERR(tscs42xx->sysclk);
			dev_err(&i2c->dev, "Failed to get sysclk (%d)\n", ret);
			return ret;
		}
	}
	if (src == TSCS42XX_PLL_SRC_CNT) {
		ret = -EINVAL;
		dev_err(&i2c->dev, "Failed to get a valid clock name (%d)\n",
				ret);
		return ret;
	}
	tscs42xx->sysclk_src_id = src;

	tscs42xx->regmap = devm_regmap_init_i2c(i2c, &tscs42xx_regmap);
	if (IS_ERR(tscs42xx->regmap)) {
		ret = PTR_ERR(tscs42xx->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap (%d)\n", ret);
		return ret;
	}

	init_coeff_ram_cache(tscs42xx);

	ret = part_is_valid(tscs42xx);
	if (ret <= 0) {
		dev_err(&i2c->dev, "No valid part (%d)\n", ret);
		ret = -ENODEV;
		return ret;
	}

	ret = regmap_write(tscs42xx->regmap, R_RESET, RV_RESET_ENABLE);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to reset device (%d)\n", ret);
		return ret;
	}

	ret = regmap_register_patch(tscs42xx->regmap, tscs42xx_patch,
			ARRAY_SIZE(tscs42xx_patch));
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to apply patch (%d)\n", ret);
		return ret;
	}

	mutex_init(&tscs42xx->audio_params_lock);
	mutex_init(&tscs42xx->coeff_ram_lock);
	mutex_init(&tscs42xx->pll_lock);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_tscs42xx, &tscs42xx_dai, 1);
	if (ret) {
		dev_err(&i2c->dev, "Failed to register codec (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id tscs42xx_i2c_id[] = {
	{ "tscs42A1", 0 },
	{ "tscs42A2", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tscs42xx_i2c_id);

static const struct of_device_id tscs42xx_of_match[] = {
	{ .compatible = "tempo,tscs42A1", },
	{ .compatible = "tempo,tscs42A2", },
	{ }
};
MODULE_DEVICE_TABLE(of, tscs42xx_of_match);

static struct i2c_driver tscs42xx_i2c_driver = {
	.driver = {
		.name = "tscs42xx",
		.of_match_table = tscs42xx_of_match,
	},
	.probe =    tscs42xx_i2c_probe,
	.id_table = tscs42xx_i2c_id,
};

module_i2c_driver(tscs42xx_i2c_driver);

MODULE_AUTHOR("Tempo Semiconductor <steven.eckhoff.opensource@gmail.com");
MODULE_DESCRIPTION("ASoC TSCS42xx driver");
MODULE_LICENSE("GPL");
