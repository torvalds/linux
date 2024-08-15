// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8960.c  --  WM8960 ALSA SoC Audio driver
 *
 * Copyright 2007-11 Wolfson Microelectronics, plc
 *
 * Author: Liam Girdwood
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8960.h>

#include "wm8960.h"

/* R25 - Power 1 */
#define WM8960_VMID_MASK 0x180
#define WM8960_VREF      0x40

/* R26 - Power 2 */
#define WM8960_PWR2_LOUT1	0x40
#define WM8960_PWR2_ROUT1	0x20
#define WM8960_PWR2_OUT3	0x02

/* R28 - Anti-pop 1 */
#define WM8960_POBCTRL   0x80
#define WM8960_BUFDCOPEN 0x10
#define WM8960_BUFIOEN   0x08
#define WM8960_SOFT_ST   0x04
#define WM8960_HPSTBY    0x01

/* R29 - Anti-pop 2 */
#define WM8960_DISOP     0x40
#define WM8960_DRES_MASK 0x30

#define WM8960_DSCH_TOUT	600 /* discharge timeout, ms */

static bool is_pll_freq_available(unsigned int source, unsigned int target);
static int wm8960_set_pll(struct snd_soc_component *component,
		unsigned int freq_in, unsigned int freq_out);
/*
 * wm8960 register cache
 * We can't read the WM8960 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const struct reg_default wm8960_reg_defaults[] = {
	{  0x0, 0x00a7 },
	{  0x1, 0x00a7 },
	{  0x2, 0x0000 },
	{  0x3, 0x0000 },
	{  0x4, 0x0000 },
	{  0x5, 0x0008 },
	{  0x6, 0x0000 },
	{  0x7, 0x000a },
	{  0x8, 0x01c0 },
	{  0x9, 0x0000 },
	{  0xa, 0x00ff },
	{  0xb, 0x00ff },

	{ 0x10, 0x0000 },
	{ 0x11, 0x007b },
	{ 0x12, 0x0100 },
	{ 0x13, 0x0032 },
	{ 0x14, 0x0000 },
	{ 0x15, 0x00c3 },
	{ 0x16, 0x00c3 },
	{ 0x17, 0x01c0 },
	{ 0x18, 0x0000 },
	{ 0x19, 0x0000 },
	{ 0x1a, 0x0000 },
	{ 0x1b, 0x0000 },
	{ 0x1c, 0x0000 },
	{ 0x1d, 0x0000 },

	{ 0x20, 0x0100 },
	{ 0x21, 0x0100 },
	{ 0x22, 0x0050 },

	{ 0x25, 0x0050 },
	{ 0x26, 0x0000 },
	{ 0x27, 0x0000 },
	{ 0x28, 0x0000 },
	{ 0x29, 0x0000 },
	{ 0x2a, 0x0040 },
	{ 0x2b, 0x0000 },
	{ 0x2c, 0x0000 },
	{ 0x2d, 0x0050 },
	{ 0x2e, 0x0050 },
	{ 0x2f, 0x0000 },
	{ 0x30, 0x0002 },
	{ 0x31, 0x0037 },

	{ 0x33, 0x0080 },
	{ 0x34, 0x0008 },
	{ 0x35, 0x0031 },
	{ 0x36, 0x0026 },
	{ 0x37, 0x00e9 },
};

static bool wm8960_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8960_RESET:
		return true;
	default:
		return false;
	}
}

struct wm8960_priv {
	struct clk *mclk;
	struct regmap *regmap;
	int (*set_bias_level)(struct snd_soc_component *,
			      enum snd_soc_bias_level level);
	struct snd_soc_dapm_widget *lout1;
	struct snd_soc_dapm_widget *rout1;
	struct snd_soc_dapm_widget *out3;
	bool deemph;
	int lrclk;
	int bclk;
	int sysclk;
	int clk_id;
	int freq_in;
	bool is_stream_in_use[2];
	struct wm8960_data pdata;
	ktime_t dsch_start;
};

#define wm8960_reset(c)	regmap_write(c, WM8960_RESET, 0)

/* enumerated controls */
static const char *wm8960_polarity[] = {"No Inversion", "Left Inverted",
	"Right Inverted", "Stereo Inversion"};
static const char *wm8960_3d_upper_cutoff[] = {"High", "Low"};
static const char *wm8960_3d_lower_cutoff[] = {"Low", "High"};
static const char *wm8960_alcfunc[] = {"Off", "Right", "Left", "Stereo"};
static const char *wm8960_alcmode[] = {"ALC", "Limiter"};
static const char *wm8960_adc_data_output_sel[] = {
	"Left Data = Left ADC;  Right Data = Right ADC",
	"Left Data = Left ADC;  Right Data = Left ADC",
	"Left Data = Right ADC; Right Data = Right ADC",
	"Left Data = Right ADC; Right Data = Left ADC",
};
static const char *wm8960_dmonomix[] = {"Stereo", "Mono"};

static const struct soc_enum wm8960_enum[] = {
	SOC_ENUM_SINGLE(WM8960_DACCTL1, 5, 4, wm8960_polarity),
	SOC_ENUM_SINGLE(WM8960_DACCTL2, 5, 4, wm8960_polarity),
	SOC_ENUM_SINGLE(WM8960_3D, 6, 2, wm8960_3d_upper_cutoff),
	SOC_ENUM_SINGLE(WM8960_3D, 5, 2, wm8960_3d_lower_cutoff),
	SOC_ENUM_SINGLE(WM8960_ALC1, 7, 4, wm8960_alcfunc),
	SOC_ENUM_SINGLE(WM8960_ALC3, 8, 2, wm8960_alcmode),
	SOC_ENUM_SINGLE(WM8960_ADDCTL1, 2, 4, wm8960_adc_data_output_sel),
	SOC_ENUM_SINGLE(WM8960_ADDCTL1, 4, 2, wm8960_dmonomix),
};

static const int deemph_settings[] = { 0, 32000, 44100, 48000 };

static int wm8960_set_deemph(struct snd_soc_component *component)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (wm8960->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(deemph_settings); i++) {
			if (abs(deemph_settings[i] - wm8960->lrclk) <
			    abs(deemph_settings[best] - wm8960->lrclk))
				best = i;
		}

		val = best << 1;
	} else {
		val = 0;
	}

	dev_dbg(component->dev, "Set deemphasis %d\n", val);

	return snd_soc_component_update_bits(component, WM8960_DACCTL1,
				   0x6, val);
}

static int wm8960_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wm8960->deemph;
	return 0;
}

static int wm8960_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	unsigned int deemph = ucontrol->value.integer.value[0];

	if (deemph > 1)
		return -EINVAL;

	wm8960->deemph = deemph;

	return wm8960_set_deemph(component);
}

static const DECLARE_TLV_DB_SCALE(adc_tlv, -9750, 50, 1);
static const DECLARE_TLV_DB_SCALE(inpga_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -2100, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(lineinboost_tlv, -1500, 300, 1);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(micboost_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 1300, 0),
	2, 3, TLV_DB_SCALE_ITEM(2000, 900, 0),
);

static const struct snd_kcontrol_new wm8960_snd_controls[] = {
SOC_DOUBLE_R_TLV("Capture Volume", WM8960_LINVOL, WM8960_RINVOL,
		 0, 63, 0, inpga_tlv),
SOC_DOUBLE_R("Capture Volume ZC Switch", WM8960_LINVOL, WM8960_RINVOL,
	6, 1, 0),
SOC_DOUBLE_R("Capture Switch", WM8960_LINVOL, WM8960_RINVOL,
	7, 1, 1),

SOC_SINGLE_TLV("Left Input Boost Mixer LINPUT3 Volume",
	       WM8960_INBMIX1, 4, 7, 0, lineinboost_tlv),
SOC_SINGLE_TLV("Left Input Boost Mixer LINPUT2 Volume",
	       WM8960_INBMIX1, 1, 7, 0, lineinboost_tlv),
SOC_SINGLE_TLV("Right Input Boost Mixer RINPUT3 Volume",
	       WM8960_INBMIX2, 4, 7, 0, lineinboost_tlv),
SOC_SINGLE_TLV("Right Input Boost Mixer RINPUT2 Volume",
	       WM8960_INBMIX2, 1, 7, 0, lineinboost_tlv),
SOC_SINGLE_TLV("Right Input Boost Mixer RINPUT1 Volume",
		WM8960_RINPATH, 4, 3, 0, micboost_tlv),
SOC_SINGLE_TLV("Left Input Boost Mixer LINPUT1 Volume",
		WM8960_LINPATH, 4, 3, 0, micboost_tlv),

SOC_DOUBLE_R_TLV("Playback Volume", WM8960_LDAC, WM8960_RDAC,
		 0, 255, 0, dac_tlv),

SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8960_LOUT1, WM8960_ROUT1,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8960_LOUT1, WM8960_ROUT1,
	7, 1, 0),

SOC_DOUBLE_R_TLV("Speaker Playback Volume", WM8960_LOUT2, WM8960_ROUT2,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8960_LOUT2, WM8960_ROUT2,
	7, 1, 0),
SOC_SINGLE("Speaker DC Volume", WM8960_CLASSD3, 3, 5, 0),
SOC_SINGLE("Speaker AC Volume", WM8960_CLASSD3, 0, 5, 0),

SOC_SINGLE("PCM Playback -6dB Switch", WM8960_DACCTL1, 7, 1, 0),
SOC_ENUM("ADC Polarity", wm8960_enum[0]),
SOC_SINGLE("ADC High Pass Filter Switch", WM8960_DACCTL1, 0, 1, 0),

SOC_ENUM("DAC Polarity", wm8960_enum[1]),
SOC_SINGLE_BOOL_EXT("DAC Deemphasis Switch", 0,
		    wm8960_get_deemph, wm8960_put_deemph),

SOC_ENUM("3D Filter Upper Cut-Off", wm8960_enum[2]),
SOC_ENUM("3D Filter Lower Cut-Off", wm8960_enum[3]),
SOC_SINGLE("3D Volume", WM8960_3D, 1, 15, 0),
SOC_SINGLE("3D Switch", WM8960_3D, 0, 1, 0),

SOC_ENUM("ALC Function", wm8960_enum[4]),
SOC_SINGLE("ALC Max Gain", WM8960_ALC1, 4, 7, 0),
SOC_SINGLE("ALC Target", WM8960_ALC1, 0, 15, 1),
SOC_SINGLE("ALC Min Gain", WM8960_ALC2, 4, 7, 0),
SOC_SINGLE("ALC Hold Time", WM8960_ALC2, 0, 15, 0),
SOC_ENUM("ALC Mode", wm8960_enum[5]),
SOC_SINGLE("ALC Decay", WM8960_ALC3, 4, 15, 0),
SOC_SINGLE("ALC Attack", WM8960_ALC3, 0, 15, 0),

SOC_SINGLE("Noise Gate Threshold", WM8960_NOISEG, 3, 31, 0),
SOC_SINGLE("Noise Gate Switch", WM8960_NOISEG, 0, 1, 0),

SOC_DOUBLE_R_TLV("ADC PCM Capture Volume", WM8960_LADC, WM8960_RADC,
	0, 255, 0, adc_tlv),

SOC_SINGLE_TLV("Left Output Mixer Boost Bypass Volume",
	       WM8960_BYPASS1, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Left Output Mixer LINPUT3 Volume",
	       WM8960_LOUTMIX, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Right Output Mixer Boost Bypass Volume",
	       WM8960_BYPASS2, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Right Output Mixer RINPUT3 Volume",
	       WM8960_ROUTMIX, 4, 7, 1, bypass_tlv),

SOC_ENUM("ADC Data Output Select", wm8960_enum[6]),
SOC_ENUM("DAC Mono Mix", wm8960_enum[7]),
};

static const struct snd_kcontrol_new wm8960_lin_boost[] = {
SOC_DAPM_SINGLE("LINPUT2 Switch", WM8960_LINPATH, 6, 1, 0),
SOC_DAPM_SINGLE("LINPUT3 Switch", WM8960_LINPATH, 7, 1, 0),
SOC_DAPM_SINGLE("LINPUT1 Switch", WM8960_LINPATH, 8, 1, 0),
};

static const struct snd_kcontrol_new wm8960_lin[] = {
SOC_DAPM_SINGLE("Boost Switch", WM8960_LINPATH, 3, 1, 0),
};

static const struct snd_kcontrol_new wm8960_rin_boost[] = {
SOC_DAPM_SINGLE("RINPUT2 Switch", WM8960_RINPATH, 6, 1, 0),
SOC_DAPM_SINGLE("RINPUT3 Switch", WM8960_RINPATH, 7, 1, 0),
SOC_DAPM_SINGLE("RINPUT1 Switch", WM8960_RINPATH, 8, 1, 0),
};

static const struct snd_kcontrol_new wm8960_rin[] = {
SOC_DAPM_SINGLE("Boost Switch", WM8960_RINPATH, 3, 1, 0),
};

static const struct snd_kcontrol_new wm8960_loutput_mixer[] = {
SOC_DAPM_SINGLE("PCM Playback Switch", WM8960_LOUTMIX, 8, 1, 0),
SOC_DAPM_SINGLE("LINPUT3 Switch", WM8960_LOUTMIX, 7, 1, 0),
SOC_DAPM_SINGLE("Boost Bypass Switch", WM8960_BYPASS1, 7, 1, 0),
};

static const struct snd_kcontrol_new wm8960_routput_mixer[] = {
SOC_DAPM_SINGLE("PCM Playback Switch", WM8960_ROUTMIX, 8, 1, 0),
SOC_DAPM_SINGLE("RINPUT3 Switch", WM8960_ROUTMIX, 7, 1, 0),
SOC_DAPM_SINGLE("Boost Bypass Switch", WM8960_BYPASS2, 7, 1, 0),
};

static const struct snd_kcontrol_new wm8960_mono_out[] = {
SOC_DAPM_SINGLE("Left Switch", WM8960_MONOMIX1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Switch", WM8960_MONOMIX2, 7, 1, 0),
};

static const struct snd_soc_dapm_widget wm8960_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("LINPUT1"),
SND_SOC_DAPM_INPUT("RINPUT1"),
SND_SOC_DAPM_INPUT("LINPUT2"),
SND_SOC_DAPM_INPUT("RINPUT2"),
SND_SOC_DAPM_INPUT("LINPUT3"),
SND_SOC_DAPM_INPUT("RINPUT3"),

SND_SOC_DAPM_SUPPLY("MICB", WM8960_POWER1, 1, 0, NULL, 0),

SND_SOC_DAPM_MIXER("Left Boost Mixer", WM8960_POWER1, 5, 0,
		   wm8960_lin_boost, ARRAY_SIZE(wm8960_lin_boost)),
SND_SOC_DAPM_MIXER("Right Boost Mixer", WM8960_POWER1, 4, 0,
		   wm8960_rin_boost, ARRAY_SIZE(wm8960_rin_boost)),

SND_SOC_DAPM_MIXER("Left Input Mixer", WM8960_POWER3, 5, 0,
		   wm8960_lin, ARRAY_SIZE(wm8960_lin)),
SND_SOC_DAPM_MIXER("Right Input Mixer", WM8960_POWER3, 4, 0,
		   wm8960_rin, ARRAY_SIZE(wm8960_rin)),

SND_SOC_DAPM_ADC("Left ADC", "Capture", WM8960_POWER1, 3, 0),
SND_SOC_DAPM_ADC("Right ADC", "Capture", WM8960_POWER1, 2, 0),

SND_SOC_DAPM_DAC("Left DAC", "Playback", WM8960_POWER2, 8, 0),
SND_SOC_DAPM_DAC("Right DAC", "Playback", WM8960_POWER2, 7, 0),

SND_SOC_DAPM_MIXER("Left Output Mixer", WM8960_POWER3, 3, 0,
	&wm8960_loutput_mixer[0],
	ARRAY_SIZE(wm8960_loutput_mixer)),
SND_SOC_DAPM_MIXER("Right Output Mixer", WM8960_POWER3, 2, 0,
	&wm8960_routput_mixer[0],
	ARRAY_SIZE(wm8960_routput_mixer)),

SND_SOC_DAPM_PGA("LOUT1 PGA", WM8960_POWER2, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("ROUT1 PGA", WM8960_POWER2, 5, 0, NULL, 0),

SND_SOC_DAPM_PGA("Left Speaker PGA", WM8960_POWER2, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Speaker PGA", WM8960_POWER2, 3, 0, NULL, 0),

SND_SOC_DAPM_PGA("Right Speaker Output", WM8960_CLASSD1, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left Speaker Output", WM8960_CLASSD1, 6, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("SPK_LP"),
SND_SOC_DAPM_OUTPUT("SPK_LN"),
SND_SOC_DAPM_OUTPUT("HP_L"),
SND_SOC_DAPM_OUTPUT("HP_R"),
SND_SOC_DAPM_OUTPUT("SPK_RP"),
SND_SOC_DAPM_OUTPUT("SPK_RN"),
SND_SOC_DAPM_OUTPUT("OUT3"),
};

static const struct snd_soc_dapm_widget wm8960_dapm_widgets_out3[] = {
SND_SOC_DAPM_MIXER("Mono Output Mixer", WM8960_POWER2, 1, 0,
	&wm8960_mono_out[0],
	ARRAY_SIZE(wm8960_mono_out)),
};

/* Represent OUT3 as a PGA so that it gets turned on with LOUT1/ROUT1 */
static const struct snd_soc_dapm_widget wm8960_dapm_widgets_capless[] = {
SND_SOC_DAPM_PGA("OUT3 VMID", WM8960_POWER2, 1, 0, NULL, 0),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "Left Boost Mixer", "LINPUT1 Switch", "LINPUT1" },
	{ "Left Boost Mixer", "LINPUT2 Switch", "LINPUT2" },
	{ "Left Boost Mixer", "LINPUT3 Switch", "LINPUT3" },

	{ "Left Input Mixer", "Boost Switch", "Left Boost Mixer" },
	{ "Left Input Mixer", "Boost Switch", "LINPUT1" },  /* Really Boost Switch */
	{ "Left Input Mixer", NULL, "LINPUT2" },
	{ "Left Input Mixer", NULL, "LINPUT3" },

	{ "Right Boost Mixer", "RINPUT1 Switch", "RINPUT1" },
	{ "Right Boost Mixer", "RINPUT2 Switch", "RINPUT2" },
	{ "Right Boost Mixer", "RINPUT3 Switch", "RINPUT3" },

	{ "Right Input Mixer", "Boost Switch", "Right Boost Mixer" },
	{ "Right Input Mixer", "Boost Switch", "RINPUT1" },  /* Really Boost Switch */
	{ "Right Input Mixer", NULL, "RINPUT2" },
	{ "Right Input Mixer", NULL, "RINPUT3" },

	{ "Left ADC", NULL, "Left Input Mixer" },
	{ "Right ADC", NULL, "Right Input Mixer" },

	{ "Left Output Mixer", "LINPUT3 Switch", "LINPUT3" },
	{ "Left Output Mixer", "Boost Bypass Switch", "Left Boost Mixer" },
	{ "Left Output Mixer", "PCM Playback Switch", "Left DAC" },

	{ "Right Output Mixer", "RINPUT3 Switch", "RINPUT3" },
	{ "Right Output Mixer", "Boost Bypass Switch", "Right Boost Mixer" },
	{ "Right Output Mixer", "PCM Playback Switch", "Right DAC" },

	{ "LOUT1 PGA", NULL, "Left Output Mixer" },
	{ "ROUT1 PGA", NULL, "Right Output Mixer" },

	{ "HP_L", NULL, "LOUT1 PGA" },
	{ "HP_R", NULL, "ROUT1 PGA" },

	{ "Left Speaker PGA", NULL, "Left Output Mixer" },
	{ "Right Speaker PGA", NULL, "Right Output Mixer" },

	{ "Left Speaker Output", NULL, "Left Speaker PGA" },
	{ "Right Speaker Output", NULL, "Right Speaker PGA" },

	{ "SPK_LN", NULL, "Left Speaker Output" },
	{ "SPK_LP", NULL, "Left Speaker Output" },
	{ "SPK_RN", NULL, "Right Speaker Output" },
	{ "SPK_RP", NULL, "Right Speaker Output" },
};

static const struct snd_soc_dapm_route audio_paths_out3[] = {
	{ "Mono Output Mixer", "Left Switch", "Left Output Mixer" },
	{ "Mono Output Mixer", "Right Switch", "Right Output Mixer" },

	{ "OUT3", NULL, "Mono Output Mixer", }
};

static const struct snd_soc_dapm_route audio_paths_capless[] = {
	{ "HP_L", NULL, "OUT3 VMID" },
	{ "HP_R", NULL, "OUT3 VMID" },

	{ "OUT3 VMID", NULL, "Left Output Mixer" },
	{ "OUT3 VMID", NULL, "Right Output Mixer" },
};

static int wm8960_add_widgets(struct snd_soc_component *component)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	struct wm8960_data *pdata = &wm8960->pdata;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct snd_soc_dapm_widget *w;

	snd_soc_dapm_new_controls(dapm, wm8960_dapm_widgets,
				  ARRAY_SIZE(wm8960_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, audio_paths, ARRAY_SIZE(audio_paths));

	/* In capless mode OUT3 is used to provide VMID for the
	 * headphone outputs, otherwise it is used as a mono mixer.
	 */
	if (pdata && pdata->capless) {
		snd_soc_dapm_new_controls(dapm, wm8960_dapm_widgets_capless,
					  ARRAY_SIZE(wm8960_dapm_widgets_capless));

		snd_soc_dapm_add_routes(dapm, audio_paths_capless,
					ARRAY_SIZE(audio_paths_capless));
	} else {
		snd_soc_dapm_new_controls(dapm, wm8960_dapm_widgets_out3,
					  ARRAY_SIZE(wm8960_dapm_widgets_out3));

		snd_soc_dapm_add_routes(dapm, audio_paths_out3,
					ARRAY_SIZE(audio_paths_out3));
	}

	/* We need to power up the headphone output stage out of
	 * sequence for capless mode.  To save scanning the widget
	 * list each time to find the desired power state do so now
	 * and save the result.
	 */
	list_for_each_entry(w, &component->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (strcmp(w->name, "LOUT1 PGA") == 0)
			wm8960->lout1 = w;
		if (strcmp(w->name, "ROUT1 PGA") == 0)
			wm8960->rout1 = w;
		if (strcmp(w->name, "OUT3 VMID") == 0)
			wm8960->out3 = w;
	}
	
	return 0;
}

static int wm8960_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
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
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
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
	snd_soc_component_write(component, WM8960_IFACE1, iface);
	return 0;
}

static struct {
	int rate;
	unsigned int val;
} alc_rates[] = {
	{ 48000, 0 },
	{ 44100, 0 },
	{ 32000, 1 },
	{ 22050, 2 },
	{ 24000, 2 },
	{ 16000, 3 },
	{ 11025, 4 },
	{ 12000, 4 },
	{  8000, 5 },
};

/* -1 for reserved value */
static const int sysclk_divs[] = { 1, -1, 2, -1 };

/* Multiply 256 for internal 256 div */
static const int dac_divs[] = { 256, 384, 512, 768, 1024, 1408, 1536 };

/* Multiply 10 to eliminate decimials */
static const int bclk_divs[] = {
	10, 15, 20, 30, 40, 55, 60, 80, 110,
	120, 160, 220, 240, 320, 320, 320
};

/**
 * wm8960_configure_sysclk - checks if there is a sysclk frequency available
 *	The sysclk must be chosen such that:
 *		- sysclk     = MCLK / sysclk_divs
 *		- lrclk      = sysclk / dac_divs
 *		- 10 * bclk  = sysclk / bclk_divs
 *
 * @wm8960: codec private data
 * @mclk: MCLK used to derive sysclk
 * @sysclk_idx: sysclk_divs index for found sysclk
 * @dac_idx: dac_divs index for found lrclk
 * @bclk_idx: bclk_divs index for found bclk
 *
 * Returns:
 *  -1, in case no sysclk frequency available found
 * >=0, in case we could derive bclk and lrclk from sysclk using
 *      (@sysclk_idx, @dac_idx, @bclk_idx) dividers
 */
static
int wm8960_configure_sysclk(struct wm8960_priv *wm8960, int mclk,
			    int *sysclk_idx, int *dac_idx, int *bclk_idx)
{
	int sysclk, bclk, lrclk;
	int i, j, k;
	int diff;

	/* marker for no match */
	*bclk_idx = -1;

	bclk = wm8960->bclk;
	lrclk = wm8960->lrclk;

	/* check if the sysclk frequency is available. */
	for (i = 0; i < ARRAY_SIZE(sysclk_divs); ++i) {
		if (sysclk_divs[i] == -1)
			continue;
		sysclk = mclk / sysclk_divs[i];
		for (j = 0; j < ARRAY_SIZE(dac_divs); ++j) {
			if (sysclk != dac_divs[j] * lrclk)
				continue;
			for (k = 0; k < ARRAY_SIZE(bclk_divs); ++k) {
				diff = sysclk - bclk * bclk_divs[k] / 10;
				if (diff == 0) {
					*sysclk_idx = i;
					*dac_idx = j;
					*bclk_idx = k;
					break;
				}
			}
			if (k != ARRAY_SIZE(bclk_divs))
				break;
		}
		if (j != ARRAY_SIZE(dac_divs))
			break;
	}
	return *bclk_idx;
}

/**
 * wm8960_configure_pll - checks if there is a PLL out frequency available
 *	The PLL out frequency must be chosen such that:
 *		- sysclk      = lrclk * dac_divs
 *		- freq_out    = sysclk * sysclk_divs
 *		- 10 * sysclk = bclk * bclk_divs
 *
 * 	If we cannot find an exact match for (sysclk, lrclk, bclk)
 * 	triplet, we relax the bclk such that bclk is chosen as the
 * 	closest available frequency greater than expected bclk.
 *
 * @component: component structure
 * @freq_in: input frequency used to derive freq out via PLL
 * @sysclk_idx: sysclk_divs index for found sysclk
 * @dac_idx: dac_divs index for found lrclk
 * @bclk_idx: bclk_divs index for found bclk
 *
 * Returns:
 * < 0, in case no PLL frequency out available was found
 * >=0, in case we could derive bclk, lrclk, sysclk from PLL out using
 *      (@sysclk_idx, @dac_idx, @bclk_idx) dividers
 */
static
int wm8960_configure_pll(struct snd_soc_component *component, int freq_in,
			 int *sysclk_idx, int *dac_idx, int *bclk_idx)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	int sysclk, bclk, lrclk, freq_out;
	int diff, closest, best_freq_out;
	int i, j, k;

	bclk = wm8960->bclk;
	lrclk = wm8960->lrclk;
	closest = freq_in;

	best_freq_out = -EINVAL;
	*sysclk_idx = *dac_idx = *bclk_idx = -1;

	/*
	 * From Datasheet, the PLL performs best when f2 is between
	 * 90MHz and 100MHz, the desired sysclk output is 11.2896MHz
	 * or 12.288MHz, then sysclkdiv = 2 is the best choice.
	 * So search sysclk_divs from 2 to 1 other than from 1 to 2.
	 */
	for (i = ARRAY_SIZE(sysclk_divs) - 1; i >= 0; --i) {
		if (sysclk_divs[i] == -1)
			continue;
		for (j = 0; j < ARRAY_SIZE(dac_divs); ++j) {
			sysclk = lrclk * dac_divs[j];
			freq_out = sysclk * sysclk_divs[i];

			for (k = 0; k < ARRAY_SIZE(bclk_divs); ++k) {
				if (!is_pll_freq_available(freq_in, freq_out))
					continue;

				diff = sysclk - bclk * bclk_divs[k] / 10;
				if (diff == 0) {
					*sysclk_idx = i;
					*dac_idx = j;
					*bclk_idx = k;
					return freq_out;
				}
				if (diff > 0 && closest > diff) {
					*sysclk_idx = i;
					*dac_idx = j;
					*bclk_idx = k;
					closest = diff;
					best_freq_out = freq_out;
				}
			}
		}
	}

	return best_freq_out;
}
static int wm8960_configure_clocking(struct snd_soc_component *component)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	int freq_out, freq_in;
	u16 iface1 = snd_soc_component_read(component, WM8960_IFACE1);
	int i, j, k;
	int ret;

	/*
	 * For Slave mode clocking should still be configured,
	 * so this if statement should be removed, but some platform
	 * may not work if the sysclk is not configured, to avoid such
	 * compatible issue, just add '!wm8960->sysclk' condition in
	 * this if statement.
	 */
	if (!(iface1 & (1 << 6)) && !wm8960->sysclk) {
		dev_warn(component->dev,
			 "slave mode, but proceeding with no clock configuration\n");
		return 0;
	}

	if (wm8960->clk_id != WM8960_SYSCLK_MCLK && !wm8960->freq_in) {
		dev_err(component->dev, "No MCLK configured\n");
		return -EINVAL;
	}

	freq_in = wm8960->freq_in;
	/*
	 * If it's sysclk auto mode, check if the MCLK can provide sysclk or
	 * not. If MCLK can provide sysclk, using MCLK to provide sysclk
	 * directly. Otherwise, auto select a available pll out frequency
	 * and set PLL.
	 */
	if (wm8960->clk_id == WM8960_SYSCLK_AUTO) {
		/* disable the PLL and using MCLK to provide sysclk */
		wm8960_set_pll(component, 0, 0);
		freq_out = freq_in;
	} else if (wm8960->sysclk) {
		freq_out = wm8960->sysclk;
	} else {
		dev_err(component->dev, "No SYSCLK configured\n");
		return -EINVAL;
	}

	if (wm8960->clk_id != WM8960_SYSCLK_PLL) {
		ret = wm8960_configure_sysclk(wm8960, freq_out, &i, &j, &k);
		if (ret >= 0) {
			goto configure_clock;
		} else if (wm8960->clk_id != WM8960_SYSCLK_AUTO) {
			dev_err(component->dev, "failed to configure clock\n");
			return -EINVAL;
		}
	}

	freq_out = wm8960_configure_pll(component, freq_in, &i, &j, &k);
	if (freq_out < 0) {
		dev_err(component->dev, "failed to configure clock via PLL\n");
		return freq_out;
	}
	wm8960_set_pll(component, freq_in, freq_out);

configure_clock:
	/* configure sysclk clock */
	snd_soc_component_update_bits(component, WM8960_CLOCK1, 3 << 1, i << 1);

	/* configure frame clock */
	snd_soc_component_update_bits(component, WM8960_CLOCK1, 0x7 << 3, j << 3);
	snd_soc_component_update_bits(component, WM8960_CLOCK1, 0x7 << 6, j << 6);

	/* configure bit clock */
	snd_soc_component_update_bits(component, WM8960_CLOCK2, 0xf, k);

	return 0;
}

static int wm8960_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	u16 iface = snd_soc_component_read(component, WM8960_IFACE1) & 0xfff3;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int i;

	wm8960->bclk = snd_soc_params_to_bclk(params);
	if (params_channels(params) == 1)
		wm8960->bclk *= 2;

	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface |= 0x0004;
		break;
	case 24:
		iface |= 0x0008;
		break;
	case 32:
		/* right justify mode does not support 32 word length */
		if ((iface & 0x3) != 0) {
			iface |= 0x000c;
			break;
		}
		fallthrough;
	default:
		dev_err(component->dev, "unsupported width %d\n",
			params_width(params));
		return -EINVAL;
	}

	wm8960->lrclk = params_rate(params);
	/* Update filters for the new rate */
	if (tx) {
		wm8960_set_deemph(component);
	} else {
		for (i = 0; i < ARRAY_SIZE(alc_rates); i++)
			if (alc_rates[i].rate == params_rate(params))
				snd_soc_component_update_bits(component,
						    WM8960_ADDCTL3, 0x7,
						    alc_rates[i].val);
	}

	/* set iface */
	snd_soc_component_write(component, WM8960_IFACE1, iface);

	wm8960->is_stream_in_use[tx] = true;

	if (!wm8960->is_stream_in_use[!tx])
		return wm8960_configure_clocking(component);

	return 0;
}

static int wm8960_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	wm8960->is_stream_in_use[tx] = false;

	return 0;
}

static int wm8960_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	if (mute)
		snd_soc_component_update_bits(component, WM8960_DACCTL1, 0x8, 0x8);
	else
		snd_soc_component_update_bits(component, WM8960_DACCTL1, 0x8, 0);
	return 0;
}

static int wm8960_set_bias_level_out3(struct snd_soc_component *component,
				      enum snd_soc_bias_level level)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	u16 pm2 = snd_soc_component_read(component, WM8960_POWER2);
	int ret;
	ktime_t tout;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		switch (snd_soc_component_get_bias_level(component)) {
		case SND_SOC_BIAS_STANDBY:
			if (!IS_ERR(wm8960->mclk)) {
				ret = clk_prepare_enable(wm8960->mclk);
				if (ret) {
					dev_err(component->dev,
						"Failed to enable MCLK: %d\n",
						ret);
					return ret;
				}
			}

			ret = wm8960_configure_clocking(component);
			if (ret)
				return ret;

			/* Set VMID to 2x50k */
			snd_soc_component_update_bits(component, WM8960_POWER1, 0x180, 0x80);
			break;

		case SND_SOC_BIAS_ON:
			/*
			 * If it's sysclk auto mode, and the pll is enabled,
			 * disable the pll
			 */
			if (wm8960->clk_id == WM8960_SYSCLK_AUTO && (pm2 & 0x1))
				wm8960_set_pll(component, 0, 0);

			if (!IS_ERR(wm8960->mclk))
				clk_disable_unprepare(wm8960->mclk);
			break;

		default:
			break;
		}

		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/* ensure discharge is complete */
			tout = WM8960_DSCH_TOUT - ktime_ms_delta(ktime_get(), wm8960->dsch_start);
			if (tout > 0)
				msleep(tout);

			regcache_sync(wm8960->regmap);

			/* Enable anti-pop features */
			snd_soc_component_write(component, WM8960_APOP1,
				      WM8960_POBCTRL | WM8960_SOFT_ST |
				      WM8960_BUFDCOPEN | WM8960_BUFIOEN);

			/* Enable & ramp VMID at 2x50k */
			snd_soc_component_update_bits(component, WM8960_POWER1, 0x80, 0x80);
			msleep(100);

			/* Enable VREF */
			snd_soc_component_update_bits(component, WM8960_POWER1, WM8960_VREF,
					    WM8960_VREF);

			/* Disable anti-pop features */
			snd_soc_component_write(component, WM8960_APOP1, WM8960_BUFIOEN);
		}

		/* Set VMID to 2x250k */
		snd_soc_component_update_bits(component, WM8960_POWER1, 0x180, 0x100);
		break;

	case SND_SOC_BIAS_OFF:
		/* Enable anti-pop features */
		snd_soc_component_write(component, WM8960_APOP1,
			     WM8960_POBCTRL | WM8960_SOFT_ST |
			     WM8960_BUFDCOPEN | WM8960_BUFIOEN);

		/* Disable VMID and VREF, mark discharge */
		snd_soc_component_write(component, WM8960_POWER1, 0);
		wm8960->dsch_start = ktime_get();
		break;
	}

	return 0;
}

static int wm8960_set_bias_level_capless(struct snd_soc_component *component,
					 enum snd_soc_bias_level level)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	u16 pm2 = snd_soc_component_read(component, WM8960_POWER2);
	int reg, ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		switch (snd_soc_component_get_bias_level(component)) {
		case SND_SOC_BIAS_STANDBY:
			/* Enable anti pop mode */
			snd_soc_component_update_bits(component, WM8960_APOP1,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN);

			/* Enable LOUT1, ROUT1 and OUT3 if they're enabled */
			reg = 0;
			if (wm8960->lout1 && wm8960->lout1->power)
				reg |= WM8960_PWR2_LOUT1;
			if (wm8960->rout1 && wm8960->rout1->power)
				reg |= WM8960_PWR2_ROUT1;
			if (wm8960->out3 && wm8960->out3->power)
				reg |= WM8960_PWR2_OUT3;
			snd_soc_component_update_bits(component, WM8960_POWER2,
					    WM8960_PWR2_LOUT1 |
					    WM8960_PWR2_ROUT1 |
					    WM8960_PWR2_OUT3, reg);

			/* Enable VMID at 2*50k */
			snd_soc_component_update_bits(component, WM8960_POWER1,
					    WM8960_VMID_MASK, 0x80);

			/* Ramp */
			msleep(100);

			/* Enable VREF */
			snd_soc_component_update_bits(component, WM8960_POWER1,
					    WM8960_VREF, WM8960_VREF);

			msleep(100);

			if (!IS_ERR(wm8960->mclk)) {
				ret = clk_prepare_enable(wm8960->mclk);
				if (ret) {
					dev_err(component->dev,
						"Failed to enable MCLK: %d\n",
						ret);
					return ret;
				}
			}

			ret = wm8960_configure_clocking(component);
			if (ret)
				return ret;

			break;

		case SND_SOC_BIAS_ON:
			/*
			 * If it's sysclk auto mode, and the pll is enabled,
			 * disable the pll
			 */
			if (wm8960->clk_id == WM8960_SYSCLK_AUTO && (pm2 & 0x1))
				wm8960_set_pll(component, 0, 0);

			if (!IS_ERR(wm8960->mclk))
				clk_disable_unprepare(wm8960->mclk);

			/* Enable anti-pop mode */
			snd_soc_component_update_bits(component, WM8960_APOP1,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN);

			/* Disable VMID and VREF */
			snd_soc_component_update_bits(component, WM8960_POWER1,
					    WM8960_VREF | WM8960_VMID_MASK, 0);
			break;

		case SND_SOC_BIAS_OFF:
			regcache_sync(wm8960->regmap);
			break;
		default:
			break;
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		switch (snd_soc_component_get_bias_level(component)) {
		case SND_SOC_BIAS_PREPARE:
			/* Disable HP discharge */
			snd_soc_component_update_bits(component, WM8960_APOP2,
					    WM8960_DISOP | WM8960_DRES_MASK,
					    0);

			/* Disable anti-pop features */
			snd_soc_component_update_bits(component, WM8960_APOP1,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN,
					    WM8960_POBCTRL | WM8960_SOFT_ST |
					    WM8960_BUFDCOPEN);
			break;

		default:
			break;
		}
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}

	return 0;
}

/* PLL divisors */
struct _pll_div {
	u32 pre_div:1;
	u32 n:4;
	u32 k:24;
};

static bool is_pll_freq_available(unsigned int source, unsigned int target)
{
	unsigned int Ndiv;

	if (source == 0 || target == 0)
		return false;

	/* Scale up target to PLL operating frequency */
	target *= 4;
	Ndiv = target / source;

	if (Ndiv < 6) {
		source >>= 1;
		Ndiv = target / source;
	}

	if ((Ndiv < 6) || (Ndiv > 12))
		return false;

	return true;
}

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 24) * 10)

static int pll_factors(unsigned int source, unsigned int target,
		       struct _pll_div *pll_div)
{
	unsigned long long Kpart;
	unsigned int K, Ndiv, Nmod;

	pr_debug("WM8960 PLL: setting %dHz->%dHz\n", source, target);

	/* Scale up target to PLL operating frequency */
	target *= 4;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div->pre_div = 1;
		Ndiv = target / source;
	} else
		pll_div->pre_div = 0;

	if ((Ndiv < 6) || (Ndiv > 12)) {
		pr_err("WM8960 PLL: Unsupported N=%d\n", Ndiv);
		return -EINVAL;
	}

	pll_div->n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div->k = K;

	pr_debug("WM8960 PLL: N=%x K=%x pre_div=%d\n",
		 pll_div->n, pll_div->k, pll_div->pre_div);

	return 0;
}

static int wm8960_set_pll(struct snd_soc_component *component,
		unsigned int freq_in, unsigned int freq_out)
{
	u16 reg;
	static struct _pll_div pll_div;
	int ret;

	if (freq_in && freq_out) {
		ret = pll_factors(freq_in, freq_out, &pll_div);
		if (ret != 0)
			return ret;
	}

	/* Disable the PLL: even if we are changing the frequency the
	 * PLL needs to be disabled while we do so. */
	snd_soc_component_update_bits(component, WM8960_CLOCK1, 0x1, 0);
	snd_soc_component_update_bits(component, WM8960_POWER2, 0x1, 0);

	if (!freq_in || !freq_out)
		return 0;

	reg = snd_soc_component_read(component, WM8960_PLL1) & ~0x3f;
	reg |= pll_div.pre_div << 4;
	reg |= pll_div.n;

	if (pll_div.k) {
		reg |= 0x20;

		snd_soc_component_write(component, WM8960_PLL2, (pll_div.k >> 16) & 0xff);
		snd_soc_component_write(component, WM8960_PLL3, (pll_div.k >> 8) & 0xff);
		snd_soc_component_write(component, WM8960_PLL4, pll_div.k & 0xff);
	}
	snd_soc_component_write(component, WM8960_PLL1, reg);

	/* Turn it on */
	snd_soc_component_update_bits(component, WM8960_POWER2, 0x1, 0x1);
	msleep(250);
	snd_soc_component_update_bits(component, WM8960_CLOCK1, 0x1, 0x1);

	return 0;
}

static int wm8960_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);

	wm8960->freq_in = freq_in;

	if (pll_id == WM8960_SYSCLK_AUTO)
		return 0;

	return wm8960_set_pll(component, freq_in, freq_out);
}

static int wm8960_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 reg;

	switch (div_id) {
	case WM8960_SYSCLKDIV:
		reg = snd_soc_component_read(component, WM8960_CLOCK1) & 0x1f9;
		snd_soc_component_write(component, WM8960_CLOCK1, reg | div);
		break;
	case WM8960_DACDIV:
		reg = snd_soc_component_read(component, WM8960_CLOCK1) & 0x1c7;
		snd_soc_component_write(component, WM8960_CLOCK1, reg | div);
		break;
	case WM8960_OPCLKDIV:
		reg = snd_soc_component_read(component, WM8960_PLL1) & 0x03f;
		snd_soc_component_write(component, WM8960_PLL1, reg | div);
		break;
	case WM8960_DCLKDIV:
		reg = snd_soc_component_read(component, WM8960_CLOCK2) & 0x03f;
		snd_soc_component_write(component, WM8960_CLOCK2, reg | div);
		break;
	case WM8960_TOCLKSEL:
		reg = snd_soc_component_read(component, WM8960_ADDCTL1) & 0x1fd;
		snd_soc_component_write(component, WM8960_ADDCTL1, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8960_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);

	return wm8960->set_bias_level(component, level);
}

static int wm8960_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
					unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case WM8960_SYSCLK_MCLK:
		snd_soc_component_update_bits(component, WM8960_CLOCK1,
					0x1, WM8960_SYSCLK_MCLK);
		break;
	case WM8960_SYSCLK_PLL:
		snd_soc_component_update_bits(component, WM8960_CLOCK1,
					0x1, WM8960_SYSCLK_PLL);
		break;
	case WM8960_SYSCLK_AUTO:
		break;
	default:
		return -EINVAL;
	}

	wm8960->sysclk = freq;
	wm8960->clk_id = clk_id;

	return 0;
}

#define WM8960_RATES SNDRV_PCM_RATE_8000_48000

#define WM8960_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8960_dai_ops = {
	.hw_params = wm8960_hw_params,
	.hw_free = wm8960_hw_free,
	.mute_stream = wm8960_mute,
	.set_fmt = wm8960_set_dai_fmt,
	.set_clkdiv = wm8960_set_dai_clkdiv,
	.set_pll = wm8960_set_dai_pll,
	.set_sysclk = wm8960_set_dai_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8960_dai = {
	.name = "wm8960-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8960_RATES,
		.formats = WM8960_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8960_RATES,
		.formats = WM8960_FORMATS,},
	.ops = &wm8960_dai_ops,
	.symmetric_rate = 1,
};

static int wm8960_probe(struct snd_soc_component *component)
{
	struct wm8960_priv *wm8960 = snd_soc_component_get_drvdata(component);
	struct wm8960_data *pdata = &wm8960->pdata;

	if (pdata->capless)
		wm8960->set_bias_level = wm8960_set_bias_level_capless;
	else
		wm8960->set_bias_level = wm8960_set_bias_level_out3;

	snd_soc_add_component_controls(component, wm8960_snd_controls,
				     ARRAY_SIZE(wm8960_snd_controls));
	wm8960_add_widgets(component);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8960 = {
	.probe			= wm8960_probe,
	.set_bias_level		= wm8960_set_bias_level,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config wm8960_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8960_PLL4,

	.reg_defaults = wm8960_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8960_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm8960_volatile,
};

static void wm8960_set_pdata_from_of(struct i2c_client *i2c,
				struct wm8960_data *pdata)
{
	const struct device_node *np = i2c->dev.of_node;

	if (of_property_read_bool(np, "wlf,capless"))
		pdata->capless = true;

	if (of_property_read_bool(np, "wlf,shared-lrclk"))
		pdata->shared_lrclk = true;

	of_property_read_u32_array(np, "wlf,gpio-cfg", pdata->gpio_cfg,
				   ARRAY_SIZE(pdata->gpio_cfg));

	of_property_read_u32_array(np, "wlf,hp-cfg", pdata->hp_cfg,
				   ARRAY_SIZE(pdata->hp_cfg));
}

static int wm8960_i2c_probe(struct i2c_client *i2c)
{
	struct wm8960_data *pdata = dev_get_platdata(&i2c->dev);
	struct wm8960_priv *wm8960;
	int ret;

	wm8960 = devm_kzalloc(&i2c->dev, sizeof(struct wm8960_priv),
			      GFP_KERNEL);
	if (wm8960 == NULL)
		return -ENOMEM;

	wm8960->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (IS_ERR(wm8960->mclk)) {
		if (PTR_ERR(wm8960->mclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	}

	wm8960->regmap = devm_regmap_init_i2c(i2c, &wm8960_regmap);
	if (IS_ERR(wm8960->regmap))
		return PTR_ERR(wm8960->regmap);

	if (pdata)
		memcpy(&wm8960->pdata, pdata, sizeof(struct wm8960_data));
	else if (i2c->dev.of_node)
		wm8960_set_pdata_from_of(i2c, &wm8960->pdata);

	ret = wm8960_reset(wm8960->regmap);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to issue reset\n");
		return ret;
	}

	if (wm8960->pdata.shared_lrclk) {
		ret = regmap_update_bits(wm8960->regmap, WM8960_ADDCTL2,
					 0x4, 0x4);
		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to enable LRCM: %d\n",
				ret);
			return ret;
		}
	}

	/* Latch the update bits */
	regmap_update_bits(wm8960->regmap, WM8960_LINVOL, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_RINVOL, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_LADC, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_RADC, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_LDAC, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_RDAC, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_LOUT1, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_ROUT1, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_LOUT2, 0x100, 0x100);
	regmap_update_bits(wm8960->regmap, WM8960_ROUT2, 0x100, 0x100);

	/* ADCLRC pin configured as GPIO. */
	regmap_update_bits(wm8960->regmap, WM8960_IFACE2, 1 << 6,
			   wm8960->pdata.gpio_cfg[0] << 6);
	regmap_update_bits(wm8960->regmap, WM8960_ADDCTL4, 0xF << 4,
			   wm8960->pdata.gpio_cfg[1] << 4);

	/* Enable headphone jack detect */
	regmap_update_bits(wm8960->regmap, WM8960_ADDCTL4, 3 << 2,
			   wm8960->pdata.hp_cfg[0] << 2);
	regmap_update_bits(wm8960->regmap, WM8960_ADDCTL2, 3 << 5,
			   wm8960->pdata.hp_cfg[1] << 5);
	regmap_update_bits(wm8960->regmap, WM8960_ADDCTL1, 3,
			   wm8960->pdata.hp_cfg[2]);

	i2c_set_clientdata(i2c, wm8960);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8960, &wm8960_dai, 1);

	return ret;
}

static void wm8960_i2c_remove(struct i2c_client *client)
{}

static const struct i2c_device_id wm8960_i2c_id[] = {
	{ "wm8960", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8960_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id wm8960_of_match[] = {
       { .compatible = "wlf,wm8960", },
       { }
};
MODULE_DEVICE_TABLE(of, wm8960_of_match);
#endif

#if defined(CONFIG_ACPI)
static const struct acpi_device_id wm8960_acpi_match[] = {
	{ "1AEC8960", 0 }, /* Wolfson PCI ID + part ID */
	{ "10138960", 0 }, /* Cirrus Logic PCI ID + part ID */
	{ },
};
MODULE_DEVICE_TABLE(acpi, wm8960_acpi_match);
#endif

static struct i2c_driver wm8960_i2c_driver = {
	.driver = {
		.name = "wm8960",
		.of_match_table = of_match_ptr(wm8960_of_match),
		.acpi_match_table = ACPI_PTR(wm8960_acpi_match),
	},
	.probe_new = wm8960_i2c_probe,
	.remove =   wm8960_i2c_remove,
	.id_table = wm8960_i2c_id,
};

module_i2c_driver(wm8960_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8960 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
