// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver supports the analog controls for the internal codec
 * found in Allwinner's A64 SoC.
 *
 * Copyright (C) 2016 Chen-Yu Tsai <wens@csie.org>
 * Copyright (C) 2017 Marcus Cooper <codekipper@gmail.com>
 * Copyright (C) 2018 Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Based on sun8i-codec-analog.c
 *
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "sun8i-adda-pr-regmap.h"

/* Codec analog control register offsets and bit fields */
#define SUN50I_ADDA_HP_CTRL		0x00
#define SUN50I_ADDA_HP_CTRL_PA_CLK_GATE		7
#define SUN50I_ADDA_HP_CTRL_HPPA_EN		6
#define SUN50I_ADDA_HP_CTRL_HPVOL		0

#define SUN50I_ADDA_OL_MIX_CTRL		0x01
#define SUN50I_ADDA_OL_MIX_CTRL_MIC1		6
#define SUN50I_ADDA_OL_MIX_CTRL_MIC2		5
#define SUN50I_ADDA_OL_MIX_CTRL_PHONE		4
#define SUN50I_ADDA_OL_MIX_CTRL_PHONEN		3
#define SUN50I_ADDA_OL_MIX_CTRL_LINEINL		2
#define SUN50I_ADDA_OL_MIX_CTRL_DACL		1
#define SUN50I_ADDA_OL_MIX_CTRL_DACR		0

#define SUN50I_ADDA_OR_MIX_CTRL		0x02
#define SUN50I_ADDA_OR_MIX_CTRL_MIC1		6
#define SUN50I_ADDA_OR_MIX_CTRL_MIC2		5
#define SUN50I_ADDA_OR_MIX_CTRL_PHONE		4
#define SUN50I_ADDA_OR_MIX_CTRL_PHONEP		3
#define SUN50I_ADDA_OR_MIX_CTRL_LINEINR		2
#define SUN50I_ADDA_OR_MIX_CTRL_DACR		1
#define SUN50I_ADDA_OR_MIX_CTRL_DACL		0

#define SUN50I_ADDA_EARPIECE_CTRL0	0x03
#define SUN50I_ADDA_EARPIECE_CTRL0_EAR_RAMP_TIME	4
#define SUN50I_ADDA_EARPIECE_CTRL0_ESPSR		0

#define SUN50I_ADDA_EARPIECE_CTRL1	0x04
#define SUN50I_ADDA_EARPIECE_CTRL1_ESPPA_EN	7
#define SUN50I_ADDA_EARPIECE_CTRL1_ESPPA_MUTE	6
#define SUN50I_ADDA_EARPIECE_CTRL1_ESP_VOL	0

#define SUN50I_ADDA_LINEOUT_CTRL0	0x05
#define SUN50I_ADDA_LINEOUT_CTRL0_LEN		7
#define SUN50I_ADDA_LINEOUT_CTRL0_REN		6
#define SUN50I_ADDA_LINEOUT_CTRL0_LSRC_SEL	5
#define SUN50I_ADDA_LINEOUT_CTRL0_RSRC_SEL	4

#define SUN50I_ADDA_LINEOUT_CTRL1	0x06
#define SUN50I_ADDA_LINEOUT_CTRL1_VOL		0

#define SUN50I_ADDA_MIC1_CTRL		0x07
#define SUN50I_ADDA_MIC1_CTRL_MIC1G		4
#define SUN50I_ADDA_MIC1_CTRL_MIC1AMPEN		3
#define SUN50I_ADDA_MIC1_CTRL_MIC1BOOST		0

#define SUN50I_ADDA_MIC2_CTRL		0x08
#define SUN50I_ADDA_MIC2_CTRL_MIC2G		4
#define SUN50I_ADDA_MIC2_CTRL_MIC2AMPEN		3
#define SUN50I_ADDA_MIC2_CTRL_MIC2BOOST		0

#define SUN50I_ADDA_LINEIN_CTRL		0x09
#define SUN50I_ADDA_LINEIN_CTRL_LINEING		0

#define SUN50I_ADDA_MIX_DAC_CTRL	0x0a
#define SUN50I_ADDA_MIX_DAC_CTRL_DACAREN	7
#define SUN50I_ADDA_MIX_DAC_CTRL_DACALEN	6
#define SUN50I_ADDA_MIX_DAC_CTRL_RMIXEN		5
#define SUN50I_ADDA_MIX_DAC_CTRL_LMIXEN		4
#define SUN50I_ADDA_MIX_DAC_CTRL_RHPPAMUTE	3
#define SUN50I_ADDA_MIX_DAC_CTRL_LHPPAMUTE	2
#define SUN50I_ADDA_MIX_DAC_CTRL_RHPIS		1
#define SUN50I_ADDA_MIX_DAC_CTRL_LHPIS		0

#define SUN50I_ADDA_L_ADCMIX_SRC	0x0b
#define SUN50I_ADDA_L_ADCMIX_SRC_MIC1		6
#define SUN50I_ADDA_L_ADCMIX_SRC_MIC2		5
#define SUN50I_ADDA_L_ADCMIX_SRC_PHONE		4
#define SUN50I_ADDA_L_ADCMIX_SRC_PHONEN		3
#define SUN50I_ADDA_L_ADCMIX_SRC_LINEINL	2
#define SUN50I_ADDA_L_ADCMIX_SRC_OMIXRL		1
#define SUN50I_ADDA_L_ADCMIX_SRC_OMIXRR		0

#define SUN50I_ADDA_R_ADCMIX_SRC	0x0c
#define SUN50I_ADDA_R_ADCMIX_SRC_MIC1		6
#define SUN50I_ADDA_R_ADCMIX_SRC_MIC2		5
#define SUN50I_ADDA_R_ADCMIX_SRC_PHONE		4
#define SUN50I_ADDA_R_ADCMIX_SRC_PHONEP		3
#define SUN50I_ADDA_R_ADCMIX_SRC_LINEINR	2
#define SUN50I_ADDA_R_ADCMIX_SRC_OMIXR		1
#define SUN50I_ADDA_R_ADCMIX_SRC_OMIXL		0

#define SUN50I_ADDA_ADC_CTRL		0x0d
#define SUN50I_ADDA_ADC_CTRL_ADCREN		7
#define SUN50I_ADDA_ADC_CTRL_ADCLEN		6
#define SUN50I_ADDA_ADC_CTRL_ADCG		0

#define SUN50I_ADDA_HS_MBIAS_CTRL	0x0e
#define SUN50I_ADDA_HS_MBIAS_CTRL_MMICBIASEN	7

#define SUN50I_ADDA_JACK_MIC_CTRL	0x1d
#define SUN50I_ADDA_JACK_MIC_CTRL_HMICBIASEN	5

/* mixer controls */
static const struct snd_kcontrol_new sun50i_a64_codec_mixer_controls[] = {
	SOC_DAPM_DOUBLE_R("Mic1 Playback Switch",
			  SUN50I_ADDA_OL_MIX_CTRL,
			  SUN50I_ADDA_OR_MIX_CTRL,
			  SUN50I_ADDA_OL_MIX_CTRL_MIC1, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic2 Playback Switch",
			  SUN50I_ADDA_OL_MIX_CTRL,
			  SUN50I_ADDA_OR_MIX_CTRL,
			  SUN50I_ADDA_OL_MIX_CTRL_MIC2, 1, 0),
	SOC_DAPM_DOUBLE_R("Line In Playback Switch",
			  SUN50I_ADDA_OL_MIX_CTRL,
			  SUN50I_ADDA_OR_MIX_CTRL,
			  SUN50I_ADDA_OL_MIX_CTRL_LINEINL, 1, 0),
	SOC_DAPM_DOUBLE_R("DAC Playback Switch",
			  SUN50I_ADDA_OL_MIX_CTRL,
			  SUN50I_ADDA_OR_MIX_CTRL,
			  SUN50I_ADDA_OL_MIX_CTRL_DACL, 1, 0),
	SOC_DAPM_DOUBLE_R("DAC Reversed Playback Switch",
			  SUN50I_ADDA_OL_MIX_CTRL,
			  SUN50I_ADDA_OR_MIX_CTRL,
			  SUN50I_ADDA_OL_MIX_CTRL_DACR, 1, 0),
};

/* ADC mixer controls */
static const struct snd_kcontrol_new sun50i_codec_adc_mixer_controls[] = {
	SOC_DAPM_DOUBLE_R("Mic1 Capture Switch",
			  SUN50I_ADDA_L_ADCMIX_SRC,
			  SUN50I_ADDA_R_ADCMIX_SRC,
			  SUN50I_ADDA_L_ADCMIX_SRC_MIC1, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic2 Capture Switch",
			  SUN50I_ADDA_L_ADCMIX_SRC,
			  SUN50I_ADDA_R_ADCMIX_SRC,
			  SUN50I_ADDA_L_ADCMIX_SRC_MIC2, 1, 0),
	SOC_DAPM_DOUBLE_R("Line In Capture Switch",
			  SUN50I_ADDA_L_ADCMIX_SRC,
			  SUN50I_ADDA_R_ADCMIX_SRC,
			  SUN50I_ADDA_L_ADCMIX_SRC_LINEINL, 1, 0),
	SOC_DAPM_DOUBLE_R("Mixer Capture Switch",
			  SUN50I_ADDA_L_ADCMIX_SRC,
			  SUN50I_ADDA_R_ADCMIX_SRC,
			  SUN50I_ADDA_L_ADCMIX_SRC_OMIXRL, 1, 0),
	SOC_DAPM_DOUBLE_R("Mixer Reversed Capture Switch",
			  SUN50I_ADDA_L_ADCMIX_SRC,
			  SUN50I_ADDA_R_ADCMIX_SRC,
			  SUN50I_ADDA_L_ADCMIX_SRC_OMIXRR, 1, 0),
};

static const DECLARE_TLV_DB_SCALE(sun50i_codec_out_mixer_pregain_scale,
				  -450, 150, 0);
static const DECLARE_TLV_DB_RANGE(sun50i_codec_mic_gain_scale,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 7, TLV_DB_SCALE_ITEM(2400, 300, 0),
);

static const DECLARE_TLV_DB_SCALE(sun50i_codec_hp_vol_scale, -6300, 100, 1);

static const DECLARE_TLV_DB_RANGE(sun50i_codec_lineout_vol_scale,
	0, 1, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	2, 31, TLV_DB_SCALE_ITEM(-4350, 150, 0),
);

static const DECLARE_TLV_DB_RANGE(sun50i_codec_earpiece_vol_scale,
	0, 1, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	2, 31, TLV_DB_SCALE_ITEM(-4350, 150, 0),
);

/* volume / mute controls */
static const struct snd_kcontrol_new sun50i_a64_codec_controls[] = {
	SOC_SINGLE_TLV("Headphone Playback Volume",
		       SUN50I_ADDA_HP_CTRL,
		       SUN50I_ADDA_HP_CTRL_HPVOL, 0x3f, 0,
		       sun50i_codec_hp_vol_scale),

	/* Mixer pre-gain */
	SOC_SINGLE_TLV("Mic1 Playback Volume", SUN50I_ADDA_MIC1_CTRL,
		       SUN50I_ADDA_MIC1_CTRL_MIC1G,
		       0x7, 0, sun50i_codec_out_mixer_pregain_scale),

	/* Microphone Amp boost gain */
	SOC_SINGLE_TLV("Mic1 Boost Volume", SUN50I_ADDA_MIC1_CTRL,
		       SUN50I_ADDA_MIC1_CTRL_MIC1BOOST, 0x7, 0,
		       sun50i_codec_mic_gain_scale),

	/* Mixer pre-gain */
	SOC_SINGLE_TLV("Mic2 Playback Volume",
		       SUN50I_ADDA_MIC2_CTRL, SUN50I_ADDA_MIC2_CTRL_MIC2G,
		       0x7, 0, sun50i_codec_out_mixer_pregain_scale),

	/* Microphone Amp boost gain */
	SOC_SINGLE_TLV("Mic2 Boost Volume", SUN50I_ADDA_MIC2_CTRL,
		       SUN50I_ADDA_MIC2_CTRL_MIC2BOOST, 0x7, 0,
		       sun50i_codec_mic_gain_scale),

	/* ADC */
	SOC_SINGLE_TLV("ADC Gain Capture Volume", SUN50I_ADDA_ADC_CTRL,
		       SUN50I_ADDA_ADC_CTRL_ADCG, 0x7, 0,
		       sun50i_codec_out_mixer_pregain_scale),

	/* Mixer pre-gain */
	SOC_SINGLE_TLV("Line In Playback Volume", SUN50I_ADDA_LINEIN_CTRL,
		       SUN50I_ADDA_LINEIN_CTRL_LINEING,
		       0x7, 0, sun50i_codec_out_mixer_pregain_scale),

	SOC_SINGLE_TLV("Line Out Playback Volume",
		       SUN50I_ADDA_LINEOUT_CTRL1,
		       SUN50I_ADDA_LINEOUT_CTRL1_VOL, 0x1f, 0,
		       sun50i_codec_lineout_vol_scale),

	SOC_DOUBLE("Line Out Playback Switch",
		   SUN50I_ADDA_LINEOUT_CTRL0,
		   SUN50I_ADDA_LINEOUT_CTRL0_LEN,
		   SUN50I_ADDA_LINEOUT_CTRL0_REN, 1, 0),

	SOC_SINGLE_TLV("Earpiece Playback Volume",
		       SUN50I_ADDA_EARPIECE_CTRL1,
		       SUN50I_ADDA_EARPIECE_CTRL1_ESP_VOL, 0x1f, 0,
		       sun50i_codec_earpiece_vol_scale),

	SOC_SINGLE("Earpiece Playback Switch",
		   SUN50I_ADDA_EARPIECE_CTRL1,
		   SUN50I_ADDA_EARPIECE_CTRL1_ESPPA_MUTE, 1, 0),

};

static const char * const sun50i_codec_hp_src_enum_text[] = {
	"DAC", "Mixer",
};

static SOC_ENUM_DOUBLE_DECL(sun50i_codec_hp_src_enum,
			    SUN50I_ADDA_MIX_DAC_CTRL,
			    SUN50I_ADDA_MIX_DAC_CTRL_LHPIS,
			    SUN50I_ADDA_MIX_DAC_CTRL_RHPIS,
			    sun50i_codec_hp_src_enum_text);

static const struct snd_kcontrol_new sun50i_codec_hp_src[] = {
	SOC_DAPM_ENUM("Headphone Source Playback Route",
		      sun50i_codec_hp_src_enum),
};

static const struct snd_kcontrol_new sun50i_codec_hp_switch =
	SOC_DAPM_DOUBLE("Headphone Playback Switch",
			SUN50I_ADDA_MIX_DAC_CTRL,
			SUN50I_ADDA_MIX_DAC_CTRL_LHPPAMUTE,
			SUN50I_ADDA_MIX_DAC_CTRL_RHPPAMUTE, 1, 0);

static const char * const sun50i_codec_lineout_src_enum_text[] = {
	"Stereo", "Mono Differential",
};

static SOC_ENUM_DOUBLE_DECL(sun50i_codec_lineout_src_enum,
			    SUN50I_ADDA_LINEOUT_CTRL0,
			    SUN50I_ADDA_LINEOUT_CTRL0_LSRC_SEL,
			    SUN50I_ADDA_LINEOUT_CTRL0_RSRC_SEL,
			    sun50i_codec_lineout_src_enum_text);

static const struct snd_kcontrol_new sun50i_codec_lineout_src[] = {
	SOC_DAPM_ENUM("Line Out Source Playback Route",
		      sun50i_codec_lineout_src_enum),
};

static const char * const sun50i_codec_earpiece_src_enum_text[] = {
	"DACR", "DACL", "Right Mixer", "Left Mixer",
};

static SOC_ENUM_SINGLE_DECL(sun50i_codec_earpiece_src_enum,
			    SUN50I_ADDA_EARPIECE_CTRL0,
			    SUN50I_ADDA_EARPIECE_CTRL0_ESPSR,
			    sun50i_codec_earpiece_src_enum_text);

static const struct snd_kcontrol_new sun50i_codec_earpiece_src[] = {
	SOC_DAPM_ENUM("Earpiece Source Playback Route",
		      sun50i_codec_earpiece_src_enum),
};

static const struct snd_soc_dapm_widget sun50i_a64_codec_widgets[] = {
	/* DAC */
	SND_SOC_DAPM_DAC("Left DAC", NULL, SUN50I_ADDA_MIX_DAC_CTRL,
			 SUN50I_ADDA_MIX_DAC_CTRL_DACALEN, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, SUN50I_ADDA_MIX_DAC_CTRL,
			 SUN50I_ADDA_MIX_DAC_CTRL_DACAREN, 0),
	/* ADC */
	SND_SOC_DAPM_ADC("Left ADC", NULL, SUN50I_ADDA_ADC_CTRL,
			 SUN50I_ADDA_ADC_CTRL_ADCLEN, 0),
	SND_SOC_DAPM_ADC("Right ADC", NULL, SUN50I_ADDA_ADC_CTRL,
			 SUN50I_ADDA_ADC_CTRL_ADCREN, 0),
	/*
	 * Due to this component and the codec belonging to separate DAPM
	 * contexts, we need to manually link the above widgets to their
	 * stream widgets at the card level.
	 */

	SND_SOC_DAPM_REGULATOR_SUPPLY("cpvdd", 0, 0),
	SND_SOC_DAPM_MUX("Left Headphone Source",
			 SND_SOC_NOPM, 0, 0, sun50i_codec_hp_src),
	SND_SOC_DAPM_MUX("Right Headphone Source",
			 SND_SOC_NOPM, 0, 0, sun50i_codec_hp_src),
	SND_SOC_DAPM_SWITCH("Left Headphone Switch",
			    SND_SOC_NOPM, 0, 0, &sun50i_codec_hp_switch),
	SND_SOC_DAPM_SWITCH("Right Headphone Switch",
			    SND_SOC_NOPM, 0, 0, &sun50i_codec_hp_switch),
	SND_SOC_DAPM_OUT_DRV("Left Headphone Amp",
			     SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Right Headphone Amp",
			     SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone Amp", SUN50I_ADDA_HP_CTRL,
			     SUN50I_ADDA_HP_CTRL_HPPA_EN, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("HP"),

	SND_SOC_DAPM_MUX("Left Line Out Source",
			 SND_SOC_NOPM, 0, 0, sun50i_codec_lineout_src),
	SND_SOC_DAPM_MUX("Right Line Out Source",
			 SND_SOC_NOPM, 0, 0, sun50i_codec_lineout_src),
	SND_SOC_DAPM_OUTPUT("LINEOUT"),

	SND_SOC_DAPM_MUX("Earpiece Source Playback Route",
			 SND_SOC_NOPM, 0, 0, sun50i_codec_earpiece_src),
	SND_SOC_DAPM_OUT_DRV("Earpiece Amp", SUN50I_ADDA_EARPIECE_CTRL1,
			     SUN50I_ADDA_EARPIECE_CTRL1_ESPPA_EN, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("EARPIECE"),

	/* Microphone inputs */
	SND_SOC_DAPM_INPUT("MIC1"),

	/* Microphone Bias */
	SND_SOC_DAPM_SUPPLY("MBIAS", SUN50I_ADDA_HS_MBIAS_CTRL,
			    SUN50I_ADDA_HS_MBIAS_CTRL_MMICBIASEN,
			    0, NULL, 0),

	/* Mic input path */
	SND_SOC_DAPM_PGA("Mic1 Amplifier", SUN50I_ADDA_MIC1_CTRL,
			 SUN50I_ADDA_MIC1_CTRL_MIC1AMPEN, 0, NULL, 0),

	/* Microphone input */
	SND_SOC_DAPM_INPUT("MIC2"),

	/* Microphone Bias */
	SND_SOC_DAPM_SUPPLY("HBIAS", SUN50I_ADDA_JACK_MIC_CTRL,
			    SUN50I_ADDA_JACK_MIC_CTRL_HMICBIASEN,
			    0, NULL, 0),

	/* Mic input path */
	SND_SOC_DAPM_PGA("Mic2 Amplifier", SUN50I_ADDA_MIC2_CTRL,
			 SUN50I_ADDA_MIC2_CTRL_MIC2AMPEN, 0, NULL, 0),

	/* Line input */
	SND_SOC_DAPM_INPUT("LINEIN"),

	/* Mixers */
	SND_SOC_DAPM_MIXER("Left Mixer", SUN50I_ADDA_MIX_DAC_CTRL,
			   SUN50I_ADDA_MIX_DAC_CTRL_LMIXEN, 0,
			   sun50i_a64_codec_mixer_controls,
			   ARRAY_SIZE(sun50i_a64_codec_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SUN50I_ADDA_MIX_DAC_CTRL,
			   SUN50I_ADDA_MIX_DAC_CTRL_RMIXEN, 0,
			   sun50i_a64_codec_mixer_controls,
			   ARRAY_SIZE(sun50i_a64_codec_mixer_controls)),
	SND_SOC_DAPM_MIXER("Left ADC Mixer", SND_SOC_NOPM, 0, 0,
			   sun50i_codec_adc_mixer_controls,
			   ARRAY_SIZE(sun50i_codec_adc_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right ADC Mixer", SND_SOC_NOPM, 0, 0,
			   sun50i_codec_adc_mixer_controls,
			   ARRAY_SIZE(sun50i_codec_adc_mixer_controls)),
};

static const struct snd_soc_dapm_route sun50i_a64_codec_routes[] = {
	/* Left Mixer Routes */
	{ "Left Mixer", "Mic1 Playback Switch", "Mic1 Amplifier" },
	{ "Left Mixer", "Mic2 Playback Switch", "Mic2 Amplifier" },
	{ "Left Mixer", "Line In Playback Switch", "LINEIN" },
	{ "Left Mixer", "DAC Playback Switch", "Left DAC" },
	{ "Left Mixer", "DAC Reversed Playback Switch", "Right DAC" },

	/* Right Mixer Routes */
	{ "Right Mixer", "Mic1 Playback Switch", "Mic1 Amplifier" },
	{ "Right Mixer", "Mic2 Playback Switch", "Mic2 Amplifier" },
	{ "Right Mixer", "Line In Playback Switch", "LINEIN" },
	{ "Right Mixer", "DAC Playback Switch", "Right DAC" },
	{ "Right Mixer", "DAC Reversed Playback Switch", "Left DAC" },

	/* Left ADC Mixer Routes */
	{ "Left ADC Mixer", "Mic1 Capture Switch", "Mic1 Amplifier" },
	{ "Left ADC Mixer", "Mic2 Capture Switch", "Mic2 Amplifier" },
	{ "Left ADC Mixer", "Line In Capture Switch", "LINEIN" },
	{ "Left ADC Mixer", "Mixer Capture Switch", "Left Mixer" },
	{ "Left ADC Mixer", "Mixer Reversed Capture Switch", "Right Mixer" },

	/* Right ADC Mixer Routes */
	{ "Right ADC Mixer", "Mic1 Capture Switch", "Mic1 Amplifier" },
	{ "Right ADC Mixer", "Mic2 Capture Switch", "Mic2 Amplifier" },
	{ "Right ADC Mixer", "Line In Capture Switch", "LINEIN" },
	{ "Right ADC Mixer", "Mixer Capture Switch", "Right Mixer" },
	{ "Right ADC Mixer", "Mixer Reversed Capture Switch", "Left Mixer" },

	/* ADC Routes */
	{ "Left ADC", NULL, "Left ADC Mixer" },
	{ "Right ADC", NULL, "Right ADC Mixer" },

	/* Headphone Routes */
	{ "Left Headphone Source", "DAC", "Left DAC" },
	{ "Left Headphone Source", "Mixer", "Left Mixer" },
	{ "Left Headphone Switch", "Headphone Playback Switch", "Left Headphone Source" },
	{ "Left Headphone Amp", NULL, "Left Headphone Switch" },
	{ "Left Headphone Amp", NULL, "Headphone Amp" },
	{ "HP", NULL, "Left Headphone Amp" },

	{ "Right Headphone Source", "DAC", "Right DAC" },
	{ "Right Headphone Source", "Mixer", "Right Mixer" },
	{ "Right Headphone Switch", "Headphone Playback Switch", "Right Headphone Source" },
	{ "Right Headphone Amp", NULL, "Right Headphone Switch" },
	{ "Right Headphone Amp", NULL, "Headphone Amp" },
	{ "HP", NULL, "Right Headphone Amp" },

	{ "Headphone Amp", NULL, "cpvdd" },

	/* Microphone Routes */
	{ "Mic1 Amplifier", NULL, "MIC1"},

	/* Microphone Routes */
	{ "Mic2 Amplifier", NULL, "MIC2"},

	/* Line-out Routes */
	{ "Left Line Out Source", "Stereo", "Left Mixer" },
	{ "Left Line Out Source", "Mono Differential", "Left Mixer" },
	{ "Left Line Out Source", "Mono Differential", "Right Mixer" },
	{ "LINEOUT", NULL, "Left Line Out Source" },

	{ "Right Line Out Source", "Stereo", "Right Mixer" },
	{ "Right Line Out Source", "Mono Differential", "Left Line Out Source" },
	{ "LINEOUT", NULL, "Right Line Out Source" },

	/* Earpiece Routes */
	{ "Earpiece Source Playback Route", "DACL", "Left DAC" },
	{ "Earpiece Source Playback Route", "DACR", "Right DAC" },
	{ "Earpiece Source Playback Route", "Left Mixer", "Left Mixer" },
	{ "Earpiece Source Playback Route", "Right Mixer", "Right Mixer" },
	{ "Earpiece Amp", NULL, "Earpiece Source Playback Route" },
	{ "EARPIECE", NULL, "Earpiece Amp" },
};

static int sun50i_a64_codec_suspend(struct snd_soc_component *component)
{
	return regmap_update_bits(component->regmap, SUN50I_ADDA_HP_CTRL,
				  BIT(SUN50I_ADDA_HP_CTRL_PA_CLK_GATE),
				  BIT(SUN50I_ADDA_HP_CTRL_PA_CLK_GATE));
}

static int sun50i_a64_codec_resume(struct snd_soc_component *component)
{
	return regmap_update_bits(component->regmap, SUN50I_ADDA_HP_CTRL,
				  BIT(SUN50I_ADDA_HP_CTRL_PA_CLK_GATE), 0);
}

static const struct snd_soc_component_driver sun50i_codec_analog_cmpnt_drv = {
	.controls		= sun50i_a64_codec_controls,
	.num_controls		= ARRAY_SIZE(sun50i_a64_codec_controls),
	.dapm_widgets		= sun50i_a64_codec_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun50i_a64_codec_widgets),
	.dapm_routes		= sun50i_a64_codec_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun50i_a64_codec_routes),
	.suspend		= sun50i_a64_codec_suspend,
	.resume			= sun50i_a64_codec_resume,
};

static const struct of_device_id sun50i_codec_analog_of_match[] = {
	{
		.compatible = "allwinner,sun50i-a64-codec-analog",
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun50i_codec_analog_of_match);

static int sun50i_codec_analog_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	regmap = sun8i_adda_pr_regmap_init(&pdev->dev, base);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "Failed to create regmap\n");
		return PTR_ERR(regmap);
	}

	return devm_snd_soc_register_component(&pdev->dev,
					       &sun50i_codec_analog_cmpnt_drv,
					       NULL, 0);
}

static struct platform_driver sun50i_codec_analog_driver = {
	.driver = {
		.name = "sun50i-codec-analog",
		.of_match_table = sun50i_codec_analog_of_match,
	},
	.probe = sun50i_codec_analog_probe,
};
module_platform_driver(sun50i_codec_analog_driver);

MODULE_DESCRIPTION("Allwinner internal codec analog controls driver for A64");
MODULE_AUTHOR("Vasily Khoruzhick <anarsoul@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun50i-codec-analog");
