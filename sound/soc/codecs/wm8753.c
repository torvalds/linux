/*
 * wm8753.c  --  WM8753 ALSA Soc Audio driver
 *
 * Copyright 2003 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Notes:
 *  The WM8753 is a low power, high quality stereo codec with integrated PCM
 *  codec designed for portable digital telephony applications.
 *
 * Dual DAI:-
 *
 * This driver support 2 DAI PCM's. This makes the default PCM available for
 * HiFi audio (e.g. MP3, ogg) playback/capture and the other PCM available for
 * voice.
 *
 * Please note that the voice PCM can be connected directly to a Bluetooth
 * codec or GSM modem and thus cannot be read or written to, although it is
 * available to be configured with snd_hw_params(), etc and kcontrols in the
 * normal alsa manner.
 *
 * Fast DAI switching:-
 *
 * The driver can now fast switch between the DAI configurations via a
 * an alsa kcontrol. This allows the PCM to remain open.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include "wm8753.h"

static int caps_charge = 2000;
module_param(caps_charge, int, 0);
MODULE_PARM_DESC(caps_charge, "WM8753 cap charge time (msecs)");

static void wm8753_set_dai_mode(struct snd_soc_codec *codec,
		struct snd_soc_dai *dai, unsigned int hifi);

/*
 * wm8753 register cache
 * We can't read the WM8753 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8753_reg[] = {
	0x0008, 0x0000, 0x000a, 0x000a,
	0x0033, 0x0000, 0x0007, 0x00ff,
	0x00ff, 0x000f, 0x000f, 0x007b,
	0x0000, 0x0032, 0x0000, 0x00c3,
	0x00c3, 0x00c0, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0055,
	0x0005, 0x0050, 0x0055, 0x0050,
	0x0055, 0x0050, 0x0055, 0x0079,
	0x0079, 0x0079, 0x0079, 0x0079,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0097, 0x0097, 0x0000, 0x0004,
	0x0000, 0x0083, 0x0024, 0x01ba,
	0x0000, 0x0083, 0x0024, 0x01ba,
	0x0000, 0x0000, 0x0000
};

/* codec private data */
struct wm8753_priv {
	enum snd_soc_control_type control_type;
	unsigned int sysclk;
	unsigned int pcmclk;
	u16 reg_cache[ARRAY_SIZE(wm8753_reg)];
	int dai_func;
};

/*
 * read wm8753 register cache
 */
static inline unsigned int wm8753_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg < 1 || reg >= (ARRAY_SIZE(wm8753_reg) + 1))
		return -1;
	return cache[reg - 1];
}

/*
 * write wm8753 register cache
 */
static inline void wm8753_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg < 1 || reg >= (ARRAY_SIZE(wm8753_reg) + 1))
		return;
	cache[reg - 1] = value;
}

/*
 * write to the WM8753 register space
 */
static int wm8753_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8753 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8753_write_reg_cache(codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8753_reset(c) wm8753_write(c, WM8753_RESET, 0)

/*
 * WM8753 Controls
 */
static const char *wm8753_base[] = {"Linear Control", "Adaptive Boost"};
static const char *wm8753_base_filter[] =
	{"130Hz @ 48kHz", "200Hz @ 48kHz", "100Hz @ 16kHz", "400Hz @ 48kHz",
	"100Hz @ 8kHz", "200Hz @ 8kHz"};
static const char *wm8753_treble[] = {"8kHz", "4kHz"};
static const char *wm8753_alc_func[] = {"Off", "Right", "Left", "Stereo"};
static const char *wm8753_ng_type[] = {"Constant PGA Gain", "Mute ADC Output"};
static const char *wm8753_3d_func[] = {"Capture", "Playback"};
static const char *wm8753_3d_uc[] = {"2.2kHz", "1.5kHz"};
static const char *wm8753_3d_lc[] = {"200Hz", "500Hz"};
static const char *wm8753_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz"};
static const char *wm8753_mono_mix[] = {"Stereo", "Left", "Right", "Mono"};
static const char *wm8753_dac_phase[] = {"Non Inverted", "Inverted"};
static const char *wm8753_line_mix[] = {"Line 1 + 2", "Line 1 - 2",
	"Line 1", "Line 2"};
static const char *wm8753_mono_mux[] = {"Line Mix", "Rx Mix"};
static const char *wm8753_right_mux[] = {"Line 2", "Rx Mix"};
static const char *wm8753_left_mux[] = {"Line 1", "Rx Mix"};
static const char *wm8753_rxmsel[] = {"RXP - RXN", "RXP + RXN", "RXP", "RXN"};
static const char *wm8753_sidetone_mux[] = {"Left PGA", "Mic 1", "Mic 2",
	"Right PGA"};
static const char *wm8753_mono2_src[] = {"Inverted Mono 1", "Left", "Right",
	"Left + Right"};
static const char *wm8753_out3[] = {"VREF", "ROUT2", "Left + Right"};
static const char *wm8753_out4[] = {"VREF", "Capture ST", "LOUT2"};
static const char *wm8753_radcsel[] = {"PGA", "Line or RXP-RXN", "Sidetone"};
static const char *wm8753_ladcsel[] = {"PGA", "Line or RXP-RXN", "Line"};
static const char *wm8753_mono_adc[] = {"Stereo", "Analogue Mix Left",
	"Analogue Mix Right", "Digital Mono Mix"};
static const char *wm8753_adc_hp[] = {"3.4Hz @ 48kHz", "82Hz @ 16k",
	"82Hz @ 8kHz", "170Hz @ 8kHz"};
static const char *wm8753_adc_filter[] = {"HiFi", "Voice"};
static const char *wm8753_mic_sel[] = {"Mic 1", "Mic 2", "Mic 3"};
static const char *wm8753_dai_mode[] = {"DAI 0", "DAI 1", "DAI 2", "DAI 3"};
static const char *wm8753_dat_sel[] = {"Stereo", "Left ADC", "Right ADC",
	"Channel Swap"};
static const char *wm8753_rout2_phase[] = {"Non Inverted", "Inverted"};

static const struct soc_enum wm8753_enum[] = {
SOC_ENUM_SINGLE(WM8753_BASS, 7, 2, wm8753_base),
SOC_ENUM_SINGLE(WM8753_BASS, 4, 6, wm8753_base_filter),
SOC_ENUM_SINGLE(WM8753_TREBLE, 6, 2, wm8753_treble),
SOC_ENUM_SINGLE(WM8753_ALC1, 7, 4, wm8753_alc_func),
SOC_ENUM_SINGLE(WM8753_NGATE, 1, 2, wm8753_ng_type),
SOC_ENUM_SINGLE(WM8753_3D, 7, 2, wm8753_3d_func),
SOC_ENUM_SINGLE(WM8753_3D, 6, 2, wm8753_3d_uc),
SOC_ENUM_SINGLE(WM8753_3D, 5, 2, wm8753_3d_lc),
SOC_ENUM_SINGLE(WM8753_DAC, 1, 4, wm8753_deemp),
SOC_ENUM_SINGLE(WM8753_DAC, 4, 4, wm8753_mono_mix),
SOC_ENUM_SINGLE(WM8753_DAC, 6, 2, wm8753_dac_phase),
SOC_ENUM_SINGLE(WM8753_INCTL1, 3, 4, wm8753_line_mix),
SOC_ENUM_SINGLE(WM8753_INCTL1, 2, 2, wm8753_mono_mux),
SOC_ENUM_SINGLE(WM8753_INCTL1, 1, 2, wm8753_right_mux),
SOC_ENUM_SINGLE(WM8753_INCTL1, 0, 2, wm8753_left_mux),
SOC_ENUM_SINGLE(WM8753_INCTL2, 6, 4, wm8753_rxmsel),
SOC_ENUM_SINGLE(WM8753_INCTL2, 4, 4, wm8753_sidetone_mux),
SOC_ENUM_SINGLE(WM8753_OUTCTL, 7, 4, wm8753_mono2_src),
SOC_ENUM_SINGLE(WM8753_OUTCTL, 0, 3, wm8753_out3),
SOC_ENUM_SINGLE(WM8753_ADCTL2, 7, 3, wm8753_out4),
SOC_ENUM_SINGLE(WM8753_ADCIN, 2, 3, wm8753_radcsel),
SOC_ENUM_SINGLE(WM8753_ADCIN, 0, 3, wm8753_ladcsel),
SOC_ENUM_SINGLE(WM8753_ADCIN, 4, 4, wm8753_mono_adc),
SOC_ENUM_SINGLE(WM8753_ADC, 2, 4, wm8753_adc_hp),
SOC_ENUM_SINGLE(WM8753_ADC, 4, 2, wm8753_adc_filter),
SOC_ENUM_SINGLE(WM8753_MICBIAS, 6, 3, wm8753_mic_sel),
SOC_ENUM_SINGLE(WM8753_IOCTL, 2, 4, wm8753_dai_mode),
SOC_ENUM_SINGLE(WM8753_ADC, 7, 4, wm8753_dat_sel),
SOC_ENUM_SINGLE(WM8753_OUTCTL, 2, 2, wm8753_rout2_phase),
};


static int wm8753_get_dai(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
	int mode = wm8753_read_reg_cache(codec, WM8753_IOCTL);

	ucontrol->value.integer.value[0] = (mode & 0xc) >> 2;
	return 0;
}

static int wm8753_set_dai(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
	int mode = wm8753_read_reg_cache(codec, WM8753_IOCTL);
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);

	if (((mode & 0xc) >> 2) == ucontrol->value.integer.value[0])
		return 0;

	mode &= 0xfff3;
	mode |= (ucontrol->value.integer.value[0] << 2);

	wm8753->dai_func =  ucontrol->value.integer.value[0];
	return 1;
}

static const DECLARE_TLV_DB_SCALE(rec_mix_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(mic_preamp_tlv, 1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9750, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
static const unsigned int out_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	/* 0000000 - 0101111 = "Analogue mute" */
	0, 48, TLV_DB_SCALE_ITEM(-25500, 0, 0),
	48, 127, TLV_DB_SCALE_ITEM(-7300, 100, 0),
};
static const DECLARE_TLV_DB_SCALE(mix_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(voice_mix_tlv, -1200, 300, 0);
static const DECLARE_TLV_DB_SCALE(pga_tlv, -1725, 75, 0);

static const struct snd_kcontrol_new wm8753_snd_controls[] = {
SOC_DOUBLE_R_TLV("PCM Volume", WM8753_LDAC, WM8753_RDAC, 0, 255, 0, dac_tlv),

SOC_DOUBLE_R_TLV("ADC Capture Volume", WM8753_LADC, WM8753_RADC, 0, 255, 0,
		 adc_tlv),

SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8753_LOUT1V, WM8753_ROUT1V,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R_TLV("Speaker Playback Volume", WM8753_LOUT2V, WM8753_ROUT2V, 0,
		 127, 0, out_tlv),

SOC_SINGLE_TLV("Mono Playback Volume", WM8753_MOUTV, 0, 127, 0, out_tlv),

SOC_DOUBLE_R_TLV("Bypass Playback Volume", WM8753_LOUTM1, WM8753_ROUTM1, 4, 7,
		 1, mix_tlv),
SOC_DOUBLE_R_TLV("Sidetone Playback Volume", WM8753_LOUTM2, WM8753_ROUTM2, 4,
		 7, 1, mix_tlv),
SOC_DOUBLE_R_TLV("Voice Playback Volume", WM8753_LOUTM2, WM8753_ROUTM2, 0, 7,
		 1, voice_mix_tlv),

SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8753_LOUT1V, WM8753_ROUT1V, 7,
	     1, 0),
SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8753_LOUT2V, WM8753_ROUT2V, 7,
	     1, 0),

SOC_SINGLE_TLV("Mono Bypass Playback Volume", WM8753_MOUTM1, 4, 7, 1, mix_tlv),
SOC_SINGLE_TLV("Mono Sidetone Playback Volume", WM8753_MOUTM2, 4, 7, 1,
	       mix_tlv),
SOC_SINGLE_TLV("Mono Voice Playback Volume", WM8753_MOUTM2, 0, 7, 1,
	       voice_mix_tlv),
SOC_SINGLE("Mono Playback ZC Switch", WM8753_MOUTV, 7, 1, 0),

SOC_ENUM("Bass Boost", wm8753_enum[0]),
SOC_ENUM("Bass Filter", wm8753_enum[1]),
SOC_SINGLE("Bass Volume", WM8753_BASS, 0, 15, 1),

SOC_SINGLE("Treble Volume", WM8753_TREBLE, 0, 15, 1),
SOC_ENUM("Treble Cut-off", wm8753_enum[2]),

SOC_DOUBLE_TLV("Sidetone Capture Volume", WM8753_RECMIX1, 0, 4, 7, 1,
	       rec_mix_tlv),
SOC_SINGLE_TLV("Voice Sidetone Capture Volume", WM8753_RECMIX2, 0, 7, 1,
	       rec_mix_tlv),

SOC_DOUBLE_R_TLV("Capture Volume", WM8753_LINVOL, WM8753_RINVOL, 0, 63, 0,
		 pga_tlv),
SOC_DOUBLE_R("Capture ZC Switch", WM8753_LINVOL, WM8753_RINVOL, 6, 1, 0),
SOC_DOUBLE_R("Capture Switch", WM8753_LINVOL, WM8753_RINVOL, 7, 1, 1),

SOC_ENUM("Capture Filter Select", wm8753_enum[23]),
SOC_ENUM("Capture Filter Cut-off", wm8753_enum[24]),
SOC_SINGLE("Capture Filter Switch", WM8753_ADC, 0, 1, 1),

SOC_SINGLE("ALC Capture Target Volume", WM8753_ALC1, 0, 7, 0),
SOC_SINGLE("ALC Capture Max Volume", WM8753_ALC1, 4, 7, 0),
SOC_ENUM("ALC Capture Function", wm8753_enum[3]),
SOC_SINGLE("ALC Capture ZC Switch", WM8753_ALC2, 8, 1, 0),
SOC_SINGLE("ALC Capture Hold Time", WM8753_ALC2, 0, 15, 1),
SOC_SINGLE("ALC Capture Decay Time", WM8753_ALC3, 4, 15, 1),
SOC_SINGLE("ALC Capture Attack Time", WM8753_ALC3, 0, 15, 0),
SOC_SINGLE("ALC Capture NG Threshold", WM8753_NGATE, 3, 31, 0),
SOC_ENUM("ALC Capture NG Type", wm8753_enum[4]),
SOC_SINGLE("ALC Capture NG Switch", WM8753_NGATE, 0, 1, 0),

SOC_ENUM("3D Function", wm8753_enum[5]),
SOC_ENUM("3D Upper Cut-off", wm8753_enum[6]),
SOC_ENUM("3D Lower Cut-off", wm8753_enum[7]),
SOC_SINGLE("3D Volume", WM8753_3D, 1, 15, 0),
SOC_SINGLE("3D Switch", WM8753_3D, 0, 1, 0),

SOC_SINGLE("Capture 6dB Attenuate", WM8753_ADCTL1, 2, 1, 0),
SOC_SINGLE("Playback 6dB Attenuate", WM8753_ADCTL1, 1, 1, 0),

SOC_ENUM("De-emphasis", wm8753_enum[8]),
SOC_ENUM("Playback Mono Mix", wm8753_enum[9]),
SOC_ENUM("Playback Phase", wm8753_enum[10]),

SOC_SINGLE_TLV("Mic2 Capture Volume", WM8753_INCTL1, 7, 3, 0, mic_preamp_tlv),
SOC_SINGLE_TLV("Mic1 Capture Volume", WM8753_INCTL1, 5, 3, 0, mic_preamp_tlv),

SOC_ENUM_EXT("DAI Mode", wm8753_enum[26], wm8753_get_dai, wm8753_set_dai),

SOC_ENUM("ADC Data Select", wm8753_enum[27]),
SOC_ENUM("ROUT2 Phase", wm8753_enum[28]),
};

/*
 * _DAPM_ Controls
 */

/* Left Mixer */
static const struct snd_kcontrol_new wm8753_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Voice Playback Switch", WM8753_LOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Sidetone Playback Switch", WM8753_LOUTM2, 7, 1, 0),
SOC_DAPM_SINGLE("Left Playback Switch", WM8753_LOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Bypass Playback Switch", WM8753_LOUTM1, 7, 1, 0),
};

/* Right mixer */
static const struct snd_kcontrol_new wm8753_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Voice Playback Switch", WM8753_ROUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Sidetone Playback Switch", WM8753_ROUTM2, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8753_ROUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Bypass Playback Switch", WM8753_ROUTM1, 7, 1, 0),
};

/* Mono mixer */
static const struct snd_kcontrol_new wm8753_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8753_MOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8753_MOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Voice Playback Switch", WM8753_MOUTM2, 3, 1, 0),
SOC_DAPM_SINGLE("Sidetone Playback Switch", WM8753_MOUTM2, 7, 1, 0),
SOC_DAPM_SINGLE("Bypass Playback Switch", WM8753_MOUTM1, 7, 1, 0),
};

/* Mono 2 Mux */
static const struct snd_kcontrol_new wm8753_mono2_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[17]);

/* Out 3 Mux */
static const struct snd_kcontrol_new wm8753_out3_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[18]);

/* Out 4 Mux */
static const struct snd_kcontrol_new wm8753_out4_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[19]);

/* ADC Mono Mix */
static const struct snd_kcontrol_new wm8753_adc_mono_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[22]);

/* Record mixer */
static const struct snd_kcontrol_new wm8753_record_mixer_controls[] = {
SOC_DAPM_SINGLE("Voice Capture Switch", WM8753_RECMIX2, 3, 1, 0),
SOC_DAPM_SINGLE("Left Capture Switch", WM8753_RECMIX1, 3, 1, 0),
SOC_DAPM_SINGLE("Right Capture Switch", WM8753_RECMIX1, 7, 1, 0),
};

/* Left ADC mux */
static const struct snd_kcontrol_new wm8753_adc_left_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[21]);

/* Right ADC mux */
static const struct snd_kcontrol_new wm8753_adc_right_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[20]);

/* MIC mux */
static const struct snd_kcontrol_new wm8753_mic_mux_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[16]);

/* ALC mixer */
static const struct snd_kcontrol_new wm8753_alc_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Capture Switch", WM8753_INCTL2, 3, 1, 0),
SOC_DAPM_SINGLE("Mic2 Capture Switch", WM8753_INCTL2, 2, 1, 0),
SOC_DAPM_SINGLE("Mic1 Capture Switch", WM8753_INCTL2, 1, 1, 0),
SOC_DAPM_SINGLE("Rx Capture Switch", WM8753_INCTL2, 0, 1, 0),
};

/* Left Line mux */
static const struct snd_kcontrol_new wm8753_line_left_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[14]);

/* Right Line mux */
static const struct snd_kcontrol_new wm8753_line_right_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[13]);

/* Mono Line mux */
static const struct snd_kcontrol_new wm8753_line_mono_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[12]);

/* Line mux and mixer */
static const struct snd_kcontrol_new wm8753_line_mux_mix_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[11]);

/* Rx mux and mixer */
static const struct snd_kcontrol_new wm8753_rx_mux_mix_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[15]);

/* Mic Selector Mux */
static const struct snd_kcontrol_new wm8753_mic_sel_mux_controls =
SOC_DAPM_ENUM("Route", wm8753_enum[25]);

static const struct snd_soc_dapm_widget wm8753_dapm_widgets[] = {
SND_SOC_DAPM_MICBIAS("Mic Bias", WM8753_PWR1, 5, 0),
SND_SOC_DAPM_MIXER("Left Mixer", WM8753_PWR4, 0, 0,
	&wm8753_left_mixer_controls[0], ARRAY_SIZE(wm8753_left_mixer_controls)),
SND_SOC_DAPM_PGA("Left Out 1", WM8753_PWR3, 8, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left Out 2", WM8753_PWR3, 6, 0, NULL, 0),
SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback", WM8753_PWR1, 3, 0),
SND_SOC_DAPM_OUTPUT("LOUT1"),
SND_SOC_DAPM_OUTPUT("LOUT2"),
SND_SOC_DAPM_MIXER("Right Mixer", WM8753_PWR4, 1, 0,
	&wm8753_right_mixer_controls[0], ARRAY_SIZE(wm8753_right_mixer_controls)),
SND_SOC_DAPM_PGA("Right Out 1", WM8753_PWR3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Out 2", WM8753_PWR3, 5, 0, NULL, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback", WM8753_PWR1, 2, 0),
SND_SOC_DAPM_OUTPUT("ROUT1"),
SND_SOC_DAPM_OUTPUT("ROUT2"),
SND_SOC_DAPM_MIXER("Mono Mixer", WM8753_PWR4, 2, 0,
	&wm8753_mono_mixer_controls[0], ARRAY_SIZE(wm8753_mono_mixer_controls)),
SND_SOC_DAPM_PGA("Mono Out 1", WM8753_PWR3, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mono Out 2", WM8753_PWR3, 1, 0, NULL, 0),
SND_SOC_DAPM_DAC("Voice DAC", "Voice Playback", WM8753_PWR1, 4, 0),
SND_SOC_DAPM_OUTPUT("MONO1"),
SND_SOC_DAPM_MUX("Mono 2 Mux", SND_SOC_NOPM, 0, 0, &wm8753_mono2_controls),
SND_SOC_DAPM_OUTPUT("MONO2"),
SND_SOC_DAPM_MIXER("Out3 Left + Right", -1, 0, 0, NULL, 0),
SND_SOC_DAPM_MUX("Out3 Mux", SND_SOC_NOPM, 0, 0, &wm8753_out3_controls),
SND_SOC_DAPM_PGA("Out 3", WM8753_PWR3, 4, 0, NULL, 0),
SND_SOC_DAPM_OUTPUT("OUT3"),
SND_SOC_DAPM_MUX("Out4 Mux", SND_SOC_NOPM, 0, 0, &wm8753_out4_controls),
SND_SOC_DAPM_PGA("Out 4", WM8753_PWR3, 3, 0, NULL, 0),
SND_SOC_DAPM_OUTPUT("OUT4"),
SND_SOC_DAPM_MIXER("Playback Mixer", WM8753_PWR4, 3, 0,
	&wm8753_record_mixer_controls[0],
	ARRAY_SIZE(wm8753_record_mixer_controls)),
SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8753_PWR2, 3, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8753_PWR2, 2, 0),
SND_SOC_DAPM_MUX("Capture Left Mixer", SND_SOC_NOPM, 0, 0,
	&wm8753_adc_mono_controls),
SND_SOC_DAPM_MUX("Capture Right Mixer", SND_SOC_NOPM, 0, 0,
	&wm8753_adc_mono_controls),
SND_SOC_DAPM_MUX("Capture Left Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_adc_left_controls),
SND_SOC_DAPM_MUX("Capture Right Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_adc_right_controls),
SND_SOC_DAPM_MUX("Mic Sidetone Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_mic_mux_controls),
SND_SOC_DAPM_PGA("Left Capture Volume", WM8753_PWR2, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Capture Volume", WM8753_PWR2, 4, 0, NULL, 0),
SND_SOC_DAPM_MIXER("ALC Mixer", WM8753_PWR2, 6, 0,
	&wm8753_alc_mixer_controls[0], ARRAY_SIZE(wm8753_alc_mixer_controls)),
SND_SOC_DAPM_MUX("Line Left Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_line_left_controls),
SND_SOC_DAPM_MUX("Line Right Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_line_right_controls),
SND_SOC_DAPM_MUX("Line Mono Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_line_mono_controls),
SND_SOC_DAPM_MUX("Line Mixer", WM8753_PWR2, 0, 0,
	&wm8753_line_mux_mix_controls),
SND_SOC_DAPM_MUX("Rx Mixer", WM8753_PWR2, 1, 0,
	&wm8753_rx_mux_mix_controls),
SND_SOC_DAPM_PGA("Mic 1 Volume", WM8753_PWR2, 8, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic 2 Volume", WM8753_PWR2, 7, 0, NULL, 0),
SND_SOC_DAPM_MUX("Mic Selection Mux", SND_SOC_NOPM, 0, 0,
	&wm8753_mic_sel_mux_controls),
SND_SOC_DAPM_INPUT("LINE1"),
SND_SOC_DAPM_INPUT("LINE2"),
SND_SOC_DAPM_INPUT("RXP"),
SND_SOC_DAPM_INPUT("RXN"),
SND_SOC_DAPM_INPUT("ACIN"),
SND_SOC_DAPM_OUTPUT("ACOP"),
SND_SOC_DAPM_INPUT("MIC1N"),
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2N"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_VMID("VREF"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* left mixer */
	{"Left Mixer", "Left Playback Switch", "Left DAC"},
	{"Left Mixer", "Voice Playback Switch", "Voice DAC"},
	{"Left Mixer", "Sidetone Playback Switch", "Mic Sidetone Mux"},
	{"Left Mixer", "Bypass Playback Switch", "Line Left Mux"},

	/* right mixer */
	{"Right Mixer", "Right Playback Switch", "Right DAC"},
	{"Right Mixer", "Voice Playback Switch", "Voice DAC"},
	{"Right Mixer", "Sidetone Playback Switch", "Mic Sidetone Mux"},
	{"Right Mixer", "Bypass Playback Switch", "Line Right Mux"},

	/* mono mixer */
	{"Mono Mixer", "Voice Playback Switch", "Voice DAC"},
	{"Mono Mixer", "Left Playback Switch", "Left DAC"},
	{"Mono Mixer", "Right Playback Switch", "Right DAC"},
	{"Mono Mixer", "Sidetone Playback Switch", "Mic Sidetone Mux"},
	{"Mono Mixer", "Bypass Playback Switch", "Line Mono Mux"},

	/* left out */
	{"Left Out 1", NULL, "Left Mixer"},
	{"Left Out 2", NULL, "Left Mixer"},
	{"LOUT1", NULL, "Left Out 1"},
	{"LOUT2", NULL, "Left Out 2"},

	/* right out */
	{"Right Out 1", NULL, "Right Mixer"},
	{"Right Out 2", NULL, "Right Mixer"},
	{"ROUT1", NULL, "Right Out 1"},
	{"ROUT2", NULL, "Right Out 2"},

	/* mono 1 out */
	{"Mono Out 1", NULL, "Mono Mixer"},
	{"MONO1", NULL, "Mono Out 1"},

	/* mono 2 out */
	{"Mono 2 Mux", "Left + Right", "Out3 Left + Right"},
	{"Mono 2 Mux", "Inverted Mono 1", "MONO1"},
	{"Mono 2 Mux", "Left", "Left Mixer"},
	{"Mono 2 Mux", "Right", "Right Mixer"},
	{"Mono Out 2", NULL, "Mono 2 Mux"},
	{"MONO2", NULL, "Mono Out 2"},

	/* out 3 */
	{"Out3 Left + Right", NULL, "Left Mixer"},
	{"Out3 Left + Right", NULL, "Right Mixer"},
	{"Out3 Mux", "VREF", "VREF"},
	{"Out3 Mux", "Left + Right", "Out3 Left + Right"},
	{"Out3 Mux", "ROUT2", "ROUT2"},
	{"Out 3", NULL, "Out3 Mux"},
	{"OUT3", NULL, "Out 3"},

	/* out 4 */
	{"Out4 Mux", "VREF", "VREF"},
	{"Out4 Mux", "Capture ST", "Playback Mixer"},
	{"Out4 Mux", "LOUT2", "LOUT2"},
	{"Out 4", NULL, "Out4 Mux"},
	{"OUT4", NULL, "Out 4"},

	/* record mixer  */
	{"Playback Mixer", "Left Capture Switch", "Left Mixer"},
	{"Playback Mixer", "Voice Capture Switch", "Mono Mixer"},
	{"Playback Mixer", "Right Capture Switch", "Right Mixer"},

	/* Mic/SideTone Mux */
	{"Mic Sidetone Mux", "Left PGA", "Left Capture Volume"},
	{"Mic Sidetone Mux", "Right PGA", "Right Capture Volume"},
	{"Mic Sidetone Mux", "Mic 1", "Mic 1 Volume"},
	{"Mic Sidetone Mux", "Mic 2", "Mic 2 Volume"},

	/* Capture Left Mux */
	{"Capture Left Mux", "PGA", "Left Capture Volume"},
	{"Capture Left Mux", "Line or RXP-RXN", "Line Left Mux"},
	{"Capture Left Mux", "Line", "LINE1"},

	/* Capture Right Mux */
	{"Capture Right Mux", "PGA", "Right Capture Volume"},
	{"Capture Right Mux", "Line or RXP-RXN", "Line Right Mux"},
	{"Capture Right Mux", "Sidetone", "Playback Mixer"},

	/* Mono Capture mixer-mux */
	{"Capture Right Mixer", "Stereo", "Capture Right Mux"},
	{"Capture Left Mixer", "Stereo", "Capture Left Mux"},
	{"Capture Left Mixer", "Analogue Mix Left", "Capture Left Mux"},
	{"Capture Left Mixer", "Analogue Mix Left", "Capture Right Mux"},
	{"Capture Right Mixer", "Analogue Mix Right", "Capture Left Mux"},
	{"Capture Right Mixer", "Analogue Mix Right", "Capture Right Mux"},
	{"Capture Left Mixer", "Digital Mono Mix", "Capture Left Mux"},
	{"Capture Left Mixer", "Digital Mono Mix", "Capture Right Mux"},
	{"Capture Right Mixer", "Digital Mono Mix", "Capture Left Mux"},
	{"Capture Right Mixer", "Digital Mono Mix", "Capture Right Mux"},

	/* ADC */
	{"Left ADC", NULL, "Capture Left Mixer"},
	{"Right ADC", NULL, "Capture Right Mixer"},

	/* Left Capture Volume */
	{"Left Capture Volume", NULL, "ACIN"},

	/* Right Capture Volume */
	{"Right Capture Volume", NULL, "Mic 2 Volume"},

	/* ALC Mixer */
	{"ALC Mixer", "Line Capture Switch", "Line Mixer"},
	{"ALC Mixer", "Mic2 Capture Switch", "Mic 2 Volume"},
	{"ALC Mixer", "Mic1 Capture Switch", "Mic 1 Volume"},
	{"ALC Mixer", "Rx Capture Switch", "Rx Mixer"},

	/* Line Left Mux */
	{"Line Left Mux", "Line 1", "LINE1"},
	{"Line Left Mux", "Rx Mix", "Rx Mixer"},

	/* Line Right Mux */
	{"Line Right Mux", "Line 2", "LINE2"},
	{"Line Right Mux", "Rx Mix", "Rx Mixer"},

	/* Line Mono Mux */
	{"Line Mono Mux", "Line Mix", "Line Mixer"},
	{"Line Mono Mux", "Rx Mix", "Rx Mixer"},

	/* Line Mixer/Mux */
	{"Line Mixer", "Line 1 + 2", "LINE1"},
	{"Line Mixer", "Line 1 - 2", "LINE1"},
	{"Line Mixer", "Line 1 + 2", "LINE2"},
	{"Line Mixer", "Line 1 - 2", "LINE2"},
	{"Line Mixer", "Line 1", "LINE1"},
	{"Line Mixer", "Line 2", "LINE2"},

	/* Rx Mixer/Mux */
	{"Rx Mixer", "RXP - RXN", "RXP"},
	{"Rx Mixer", "RXP + RXN", "RXP"},
	{"Rx Mixer", "RXP - RXN", "RXN"},
	{"Rx Mixer", "RXP + RXN", "RXN"},
	{"Rx Mixer", "RXP", "RXP"},
	{"Rx Mixer", "RXN", "RXN"},

	/* Mic 1 Volume */
	{"Mic 1 Volume", NULL, "MIC1N"},
	{"Mic 1 Volume", NULL, "Mic Selection Mux"},

	/* Mic 2 Volume */
	{"Mic 2 Volume", NULL, "MIC2N"},
	{"Mic 2 Volume", NULL, "MIC2"},

	/* Mic Selector Mux */
	{"Mic Selection Mux", "Mic 1", "MIC1"},
	{"Mic Selection Mux", "Mic 2", "MIC2N"},
	{"Mic Selection Mux", "Mic 3", "MIC2"},

	/* ACOP */
	{"ACOP", NULL, "ALC Mixer"},
};

static int wm8753_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8753_dapm_widgets,
				  ARRAY_SIZE(wm8753_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

/* PLL divisors */
struct _pll_div {
	u32 div2:1;
	u32 n:4;
	u32 k:24;
};

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 22) * 10)

static void pll_factors(struct _pll_div *pll_div, unsigned int target,
	unsigned int source)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div->div2 = 1;
		Ndiv = target / source;
	} else
		pll_div->div2 = 0;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
			"wm8753: unsupported N = %u\n", Ndiv);

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
}

static int wm8753_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	u16 reg, enable;
	int offset;
	struct snd_soc_codec *codec = codec_dai->codec;

	if (pll_id < WM8753_PLL1 || pll_id > WM8753_PLL2)
		return -ENODEV;

	if (pll_id == WM8753_PLL1) {
		offset = 0;
		enable = 0x10;
		reg = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0xffef;
	} else {
		offset = 4;
		enable = 0x8;
		reg = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0xfff7;
	}

	if (!freq_in || !freq_out) {
		/* disable PLL  */
		wm8753_write(codec, WM8753_PLL1CTL1 + offset, 0x0026);
		wm8753_write(codec, WM8753_CLOCK, reg);
		return 0;
	} else {
		u16 value = 0;
		struct _pll_div pll_div;

		pll_factors(&pll_div, freq_out * 8, freq_in);

		/* set up N and K PLL divisor ratios */
		/* bits 8:5 = PLL_N, bits 3:0 = PLL_K[21:18] */
		value = (pll_div.n << 5) + ((pll_div.k & 0x3c0000) >> 18);
		wm8753_write(codec, WM8753_PLL1CTL2 + offset, value);

		/* bits 8:0 = PLL_K[17:9] */
		value = (pll_div.k & 0x03fe00) >> 9;
		wm8753_write(codec, WM8753_PLL1CTL3 + offset, value);

		/* bits 8:0 = PLL_K[8:0] */
		value = pll_div.k & 0x0001ff;
		wm8753_write(codec, WM8753_PLL1CTL4 + offset, value);

		/* set PLL as input and enable */
		wm8753_write(codec, WM8753_PLL1CTL1 + offset, 0x0027 |
			(pll_div.div2 << 3));
		wm8753_write(codec, WM8753_CLOCK, reg | enable);
	}
	return 0;
}

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u8 sr:5;
	u8 usb:1;
};

/* codec hifi mclk (after PLL) clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 0x6, 0x0},
	{11289600, 8000, 0x16, 0x0},
	{18432000, 8000, 0x7, 0x0},
	{16934400, 8000, 0x17, 0x0},
	{12000000, 8000, 0x6, 0x1},

	/* 11.025k */
	{11289600, 11025, 0x18, 0x0},
	{16934400, 11025, 0x19, 0x0},
	{12000000, 11025, 0x19, 0x1},

	/* 16k */
	{12288000, 16000, 0xa, 0x0},
	{18432000, 16000, 0xb, 0x0},
	{12000000, 16000, 0xa, 0x1},

	/* 22.05k */
	{11289600, 22050, 0x1a, 0x0},
	{16934400, 22050, 0x1b, 0x0},
	{12000000, 22050, 0x1b, 0x1},

	/* 32k */
	{12288000, 32000, 0xc, 0x0},
	{18432000, 32000, 0xd, 0x0},
	{12000000, 32000, 0xa, 0x1},

	/* 44.1k */
	{11289600, 44100, 0x10, 0x0},
	{16934400, 44100, 0x11, 0x0},
	{12000000, 44100, 0x11, 0x1},

	/* 48k */
	{12288000, 48000, 0x0, 0x0},
	{18432000, 48000, 0x1, 0x0},
	{12000000, 48000, 0x0, 0x1},

	/* 88.2k */
	{11289600, 88200, 0x1e, 0x0},
	{16934400, 88200, 0x1f, 0x0},
	{12000000, 88200, 0x1f, 0x1},

	/* 96k */
	{12288000, 96000, 0xe, 0x0},
	{18432000, 96000, 0xf, 0x0},
	{12000000, 96000, 0xe, 0x1},
};

static int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}

/*
 * Clock after PLL and dividers
 */
static int wm8753_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		if (clk_id == WM8753_MCLK) {
			wm8753->sysclk = freq;
			return 0;
		} else if (clk_id == WM8753_PCMCLK) {
			wm8753->pcmclk = freq;
			return 0;
		}
		break;
	}
	return -EINVAL;
}

/*
 * Set's ADC and Voice DAC format.
 */
static int wm8753_vdac_adc_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 voice = wm8753_read_reg_cache(codec, WM8753_PCM) & 0x01ec;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		voice |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		voice |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		voice |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		voice |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	wm8753_write(codec, WM8753_PCM, voice);
	return 0;
}

static int wm8753_pcm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	wm8753_set_dai_mode(dai->codec, dai, 0);
	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8753_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);
	u16 voice = wm8753_read_reg_cache(codec, WM8753_PCM) & 0x01f3;
	u16 srate = wm8753_read_reg_cache(codec, WM8753_SRATE1) & 0x017f;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		voice |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		voice |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		voice |= 0x000c;
		break;
	}

	/* sample rate */
	if (params_rate(params) * 384 == wm8753->pcmclk)
		srate |= 0x80;
	wm8753_write(codec, WM8753_SRATE1, srate);

	wm8753_write(codec, WM8753_PCM, voice);
	return 0;
}

/*
 * Set's PCM dai fmt and BCLK.
 */
static int wm8753_pcm_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 voice, ioctl;

	voice = wm8753_read_reg_cache(codec, WM8753_PCM) & 0x011f;
	ioctl = wm8753_read_reg_cache(codec, WM8753_IOCTL) & 0x015d;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ioctl |= 0x2;
	case SND_SOC_DAIFMT_CBM_CFS:
		voice |= 0x0040;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			voice |= 0x0080;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		voice &= ~0x0010;
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			voice |= 0x0090;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			voice |= 0x0080;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			voice |= 0x0010;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	wm8753_write(codec, WM8753_PCM, voice);
	wm8753_write(codec, WM8753_IOCTL, ioctl);
	return 0;
}

static int wm8753_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8753_PCMDIV:
		reg = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0x003f;
		wm8753_write(codec, WM8753_CLOCK, reg | div);
		break;
	case WM8753_BCLKDIV:
		reg = wm8753_read_reg_cache(codec, WM8753_SRATE2) & 0x01c7;
		wm8753_write(codec, WM8753_SRATE2, reg | div);
		break;
	case WM8753_VXCLKDIV:
		reg = wm8753_read_reg_cache(codec, WM8753_SRATE2) & 0x003f;
		wm8753_write(codec, WM8753_SRATE2, reg | div);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Set's HiFi DAC format.
 */
static int wm8753_hdac_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 hifi = wm8753_read_reg_cache(codec, WM8753_HIFI) & 0x01e0;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		hifi |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		hifi |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		hifi |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		hifi |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	wm8753_write(codec, WM8753_HIFI, hifi);
	return 0;
}

/*
 * Set's I2S DAI format.
 */
static int wm8753_i2s_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 ioctl, hifi;

	hifi = wm8753_read_reg_cache(codec, WM8753_HIFI) & 0x011f;
	ioctl = wm8753_read_reg_cache(codec, WM8753_IOCTL) & 0x00ae;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ioctl |= 0x1;
	case SND_SOC_DAIFMT_CBM_CFS:
		hifi |= 0x0040;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			hifi |= 0x0080;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		hifi &= ~0x0010;
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			hifi |= 0x0090;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			hifi |= 0x0080;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			hifi |= 0x0010;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	wm8753_write(codec, WM8753_HIFI, hifi);
	wm8753_write(codec, WM8753_IOCTL, ioctl);
	return 0;
}

static int wm8753_i2s_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	wm8753_set_dai_mode(dai->codec, dai, 1);
	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8753_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);
	u16 srate = wm8753_read_reg_cache(codec, WM8753_SRATE1) & 0x01c0;
	u16 hifi = wm8753_read_reg_cache(codec, WM8753_HIFI) & 0x01f3;
	int coeff;

	/* is digital filter coefficient valid ? */
	coeff = get_coeff(wm8753->sysclk, params_rate(params));
	if (coeff < 0) {
		printk(KERN_ERR "wm8753 invalid MCLK or rate\n");
		return coeff;
	}
	wm8753_write(codec, WM8753_SRATE1, srate | (coeff_div[coeff].sr << 1) |
		coeff_div[coeff].usb);

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		hifi |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		hifi |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		hifi |= 0x000c;
		break;
	}

	wm8753_write(codec, WM8753_HIFI, hifi);
	return 0;
}

static int wm8753_mode1v_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 clock;

	/* set clk source as pcmclk */
	clock = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0xfffb;
	wm8753_write(codec, WM8753_CLOCK, clock);

	if (wm8753_vdac_adc_set_dai_fmt(codec_dai, fmt) < 0)
		return -EINVAL;
	return wm8753_pcm_set_dai_fmt(codec_dai, fmt);
}

static int wm8753_mode1h_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	if (wm8753_hdac_set_dai_fmt(codec_dai, fmt) < 0)
		return -EINVAL;
	return wm8753_i2s_set_dai_fmt(codec_dai, fmt);
}

static int wm8753_mode2_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 clock;

	/* set clk source as pcmclk */
	clock = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0xfffb;
	wm8753_write(codec, WM8753_CLOCK, clock);

	if (wm8753_vdac_adc_set_dai_fmt(codec_dai, fmt) < 0)
		return -EINVAL;
	return wm8753_i2s_set_dai_fmt(codec_dai, fmt);
}

static int wm8753_mode3_4_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 clock;

	/* set clk source as mclk */
	clock = wm8753_read_reg_cache(codec, WM8753_CLOCK) & 0xfffb;
	wm8753_write(codec, WM8753_CLOCK, clock | 0x4);

	if (wm8753_hdac_set_dai_fmt(codec_dai, fmt) < 0)
		return -EINVAL;
	if (wm8753_vdac_adc_set_dai_fmt(codec_dai, fmt) < 0)
		return -EINVAL;
	return wm8753_i2s_set_dai_fmt(codec_dai, fmt);
}

static int wm8753_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8753_read_reg_cache(codec, WM8753_DAC) & 0xfff7;
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);

	/* the digital mute covers the HiFi and Voice DAC's on the WM8753.
	 * make sure we check if they are not both active when we mute */
	if (mute && wm8753->dai_func == 1) {
		if (!codec->active)
			wm8753_write(codec, WM8753_DAC, mute_reg | 0x8);
	} else {
		if (mute)
			wm8753_write(codec, WM8753_DAC, mute_reg | 0x8);
		else
			wm8753_write(codec, WM8753_DAC, mute_reg);
	}

	return 0;
}

static int wm8753_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 pwr_reg = wm8753_read_reg_cache(codec, WM8753_PWR1) & 0xfe3e;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* set vmid to 50k and unmute dac */
		wm8753_write(codec, WM8753_PWR1, pwr_reg | 0x00c0);
		break;
	case SND_SOC_BIAS_PREPARE:
		/* set vmid to 5k for quick power up */
		wm8753_write(codec, WM8753_PWR1, pwr_reg | 0x01c1);
		break;
	case SND_SOC_BIAS_STANDBY:
		/* mute dac and set vmid to 500k, enable VREF */
		wm8753_write(codec, WM8753_PWR1, pwr_reg | 0x0141);
		break;
	case SND_SOC_BIAS_OFF:
		wm8753_write(codec, WM8753_PWR1, 0x0001);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8753_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
		SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define WM8753_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

/*
 * The WM8753 supports upto 4 different and mutually exclusive DAI
 * configurations. This gives 2 PCM's available for use, hifi and voice.
 * NOTE: The Voice PCM cannot play or capture audio to the CPU as it's DAI
 * is connected between the wm8753 and a BT codec or GSM modem.
 *
 * 1. Voice over PCM DAI - HIFI DAC over HIFI DAI
 * 2. Voice over HIFI DAI - HIFI disabled
 * 3. Voice disabled - HIFI over HIFI
 * 4. Voice disabled - HIFI over HIFI, uses voice DAI LRC for capture
 */
static struct snd_soc_dai_ops wm8753_dai_ops_hifi_mode1 = {
	.startup = wm8753_i2s_startup,
	.hw_params	= wm8753_i2s_hw_params,
	.digital_mute	= wm8753_mute,
	.set_fmt	= wm8753_mode1h_set_dai_fmt,
	.set_clkdiv	= wm8753_set_dai_clkdiv,
	.set_pll	= wm8753_set_dai_pll,
	.set_sysclk	= wm8753_set_dai_sysclk,
};

static struct snd_soc_dai_ops wm8753_dai_ops_voice_mode1 = {
	.startup = wm8753_pcm_startup,
	.hw_params	= wm8753_pcm_hw_params,
	.digital_mute	= wm8753_mute,
	.set_fmt	= wm8753_mode1v_set_dai_fmt,
	.set_clkdiv	= wm8753_set_dai_clkdiv,
	.set_pll	= wm8753_set_dai_pll,
	.set_sysclk	= wm8753_set_dai_sysclk,
};

static struct snd_soc_dai_ops wm8753_dai_ops_voice_mode2 = {
	.startup = wm8753_pcm_startup,
	.hw_params	= wm8753_pcm_hw_params,
	.digital_mute	= wm8753_mute,
	.set_fmt	= wm8753_mode2_set_dai_fmt,
	.set_clkdiv	= wm8753_set_dai_clkdiv,
	.set_pll	= wm8753_set_dai_pll,
	.set_sysclk	= wm8753_set_dai_sysclk,
};

static struct snd_soc_dai_ops wm8753_dai_ops_hifi_mode3	= {
	.startup = wm8753_i2s_startup,
	.hw_params	= wm8753_i2s_hw_params,
	.digital_mute	= wm8753_mute,
	.set_fmt	= wm8753_mode3_4_set_dai_fmt,
	.set_clkdiv	= wm8753_set_dai_clkdiv,
	.set_pll	= wm8753_set_dai_pll,
	.set_sysclk	= wm8753_set_dai_sysclk,
};

static struct snd_soc_dai_ops wm8753_dai_ops_hifi_mode4	= {
	.startup = wm8753_i2s_startup,
	.hw_params	= wm8753_i2s_hw_params,
	.digital_mute	= wm8753_mute,
	.set_fmt	= wm8753_mode3_4_set_dai_fmt,
	.set_clkdiv	= wm8753_set_dai_clkdiv,
	.set_pll	= wm8753_set_dai_pll,
	.set_sysclk	= wm8753_set_dai_sysclk,
};

static struct snd_soc_dai_driver wm8753_all_dai[] = {
/* DAI HiFi mode 1 */
{	.name = "wm8753-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS},
	.capture = { /* dummy for fast DAI switching */
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS},
	.ops = &wm8753_dai_ops_hifi_mode1,
},
/* DAI Voice mode 1 */
{	.name = "wm8753-voice",
	.playback = {
		.stream_name = "Voice Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.ops = &wm8753_dai_ops_voice_mode1,
},
/* DAI HiFi mode 2 - dummy */
{	.name = "wm8753-hifi",
},
/* DAI Voice mode 2 */
{	.name = "wm8753-voice",
	.playback = {
		.stream_name = "Voice Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.ops = &wm8753_dai_ops_voice_mode2,
},
/* DAI HiFi mode 3 */
{	.name = "wm8753-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.ops = &wm8753_dai_ops_hifi_mode3,
},
/* DAI Voice mode 3 - dummy */
{	.name = "wm8753-voice",
},
/* DAI HiFi mode 4 */
{	.name = "wm8753-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8753_RATES,
		.formats = WM8753_FORMATS,},
	.ops = &wm8753_dai_ops_hifi_mode4,
},
/* DAI Voice mode 4 - dummy */
{	.name = "wm8753-voice",
},
};

static struct snd_soc_dai_driver wm8753_dai[] = {
	{
		.name = "wm8753-aif0",
	},
	{
		.name = "wm8753-aif1",
	},
};

static void wm8753_set_dai_mode(struct snd_soc_codec *codec,
		struct snd_soc_dai *dai, unsigned int hifi)
{
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);

	if (wm8753->dai_func < 4) {
		if (hifi)
			dai->driver = &wm8753_all_dai[wm8753->dai_func << 1];
		else
			dai->driver = &wm8753_all_dai[(wm8753->dai_func << 1) + 1];
	}
	wm8753_write(codec, WM8753_IOCTL, wm8753->dai_func);
}

static void wm8753_work(struct work_struct *work)
{
	struct snd_soc_dapm_context *dapm =
		container_of(work, struct snd_soc_dapm_context,
			     delayed_work.work);
	struct snd_soc_codec *codec = dapm->codec;
	wm8753_set_bias_level(codec, dapm->bias_level);
}

static int wm8753_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	wm8753_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8753_resume(struct snd_soc_codec *codec)
{
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8753_reg); i++) {
		if (i + 1 == WM8753_RESET)
			continue;

		/* No point in writing hardware default values back */
		if (cache[i] == wm8753_reg[i])
			continue;

		data[0] = ((i + 1) << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}

	wm8753_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* charge wm8753 caps */
	if (codec->dapm.suspend_bias_level == SND_SOC_BIAS_ON) {
		wm8753_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
		codec->dapm.bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&codec->dapm.delayed_work,
			msecs_to_jiffies(caps_charge));
	}

	return 0;
}

static int wm8753_probe(struct snd_soc_codec *codec)
{
	struct wm8753_priv *wm8753 = snd_soc_codec_get_drvdata(codec);
	int ret = 0, reg;

	INIT_DELAYED_WORK(&codec->dapm.delayed_work, wm8753_work);

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, wm8753->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = wm8753_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	wm8753_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8753->dai_func = 0;

	/* charge output caps */
	wm8753_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	schedule_delayed_work(&codec->dapm.delayed_work,
			      msecs_to_jiffies(caps_charge));

	/* set the update bits */
	reg = wm8753_read_reg_cache(codec, WM8753_LDAC);
	wm8753_write(codec, WM8753_LDAC, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_RDAC);
	wm8753_write(codec, WM8753_RDAC, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_LADC);
	wm8753_write(codec, WM8753_LADC, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_RADC);
	wm8753_write(codec, WM8753_RADC, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_LOUT1V);
	wm8753_write(codec, WM8753_LOUT1V, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_ROUT1V);
	wm8753_write(codec, WM8753_ROUT1V, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_LOUT2V);
	wm8753_write(codec, WM8753_LOUT2V, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_ROUT2V);
	wm8753_write(codec, WM8753_ROUT2V, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_LINVOL);
	wm8753_write(codec, WM8753_LINVOL, reg | 0x0100);
	reg = wm8753_read_reg_cache(codec, WM8753_RINVOL);
	wm8753_write(codec, WM8753_RINVOL, reg | 0x0100);

	snd_soc_add_controls(codec, wm8753_snd_controls,
			     ARRAY_SIZE(wm8753_snd_controls));
	wm8753_add_widgets(codec);

	return 0;
}

/* power down chip */
static int wm8753_remove(struct snd_soc_codec *codec)
{
	flush_delayed_work_sync(&codec->dapm.delayed_work);
	wm8753_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8753 = {
	.probe =	wm8753_probe,
	.remove =	wm8753_remove,
	.suspend =	wm8753_suspend,
	.resume =	wm8753_resume,
	.set_bias_level = wm8753_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(wm8753_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8753_reg,
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8753_spi_probe(struct spi_device *spi)
{
	struct wm8753_priv *wm8753;
	int ret;

	wm8753 = kzalloc(sizeof(struct wm8753_priv), GFP_KERNEL);
	if (wm8753 == NULL)
		return -ENOMEM;

	wm8753->control_type = SND_SOC_SPI;
	spi_set_drvdata(spi, wm8753);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_wm8753, wm8753_dai, ARRAY_SIZE(wm8753_dai));
	if (ret < 0)
		kfree(wm8753);
	return ret;
}

static int __devexit wm8753_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver wm8753_spi_driver = {
	.driver = {
		.name	= "wm8753-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= wm8753_spi_probe,
	.remove		= __devexit_p(wm8753_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8753_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8753_priv *wm8753;
	int ret;

	wm8753 = kzalloc(sizeof(struct wm8753_priv), GFP_KERNEL);
	if (wm8753 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8753);
	wm8753->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8753, wm8753_dai, ARRAY_SIZE(wm8753_dai));
	if (ret < 0)
		kfree(wm8753);
	return ret;
}

static __devexit int wm8753_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8753_i2c_id[] = {
	{ "wm8753", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8753_i2c_id);

static struct i2c_driver wm8753_i2c_driver = {
	.driver = {
		.name = "wm8753-codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8753_i2c_probe,
	.remove =   __devexit_p(wm8753_i2c_remove),
	.id_table = wm8753_i2c_id,
};
#endif

static int __init wm8753_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8753_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8753 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8753_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8753 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8753_modinit);

static void __exit wm8753_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8753_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8753_spi_driver);
#endif
}
module_exit(wm8753_exit);

MODULE_DESCRIPTION("ASoC WM8753 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
