/*
 * wm9712.c  --  ALSA Soc WM9712 codec support
 *
 * Copyright 2006-12 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mfd/wm97xx.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/ac97/codec.h>
#include <sound/ac97/compat.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define WM9712_VENDOR_ID 0x574d4c12
#define WM9712_VENDOR_ID_MASK 0xffffffff

struct wm9712_priv {
	struct snd_ac97 *ac97;
	unsigned int hp_mixer[2];
	struct mutex lock;
	struct wm97xx_platform_data *mfd_pdata;
};

static const struct reg_default wm9712_reg_defaults[] = {
	{ 0x02, 0x8000 },
	{ 0x04, 0x8000 },
	{ 0x06, 0x8000 },
	{ 0x08, 0x0f0f },
	{ 0x0a, 0xaaa0 },
	{ 0x0c, 0xc008 },
	{ 0x0e, 0x6808 },
	{ 0x10, 0xe808 },
	{ 0x12, 0xaaa0 },
	{ 0x14, 0xad00 },
	{ 0x16, 0x8000 },
	{ 0x18, 0xe808 },
	{ 0x1a, 0x3000 },
	{ 0x1c, 0x8000 },
	{ 0x20, 0x0000 },
	{ 0x22, 0x0000 },
	{ 0x26, 0x000f },
	{ 0x28, 0x0605 },
	{ 0x2a, 0x0410 },
	{ 0x2c, 0xbb80 },
	{ 0x2e, 0xbb80 },
	{ 0x32, 0xbb80 },
	{ 0x34, 0x2000 },
	{ 0x4c, 0xf83e },
	{ 0x4e, 0xffff },
	{ 0x50, 0x0000 },
	{ 0x52, 0x0000 },
	{ 0x56, 0xf83e },
	{ 0x58, 0x0008 },
	{ 0x5c, 0x0000 },
	{ 0x60, 0xb032 },
	{ 0x62, 0x3e00 },
	{ 0x64, 0x0000 },
	{ 0x76, 0x0006 },
	{ 0x78, 0x0001 },
	{ 0x7a, 0x0000 },
};

static bool wm9712_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_REC_GAIN:
		return true;
	default:
		return regmap_ac97_default_volatile(dev, reg);
	}
}

static const struct regmap_config wm9712_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x7e,
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm9712_volatile_reg,

	.reg_defaults = wm9712_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm9712_reg_defaults),
};

#define HPL_MIXER	0x0
#define HPR_MIXER	0x1

static const char *wm9712_alc_select[] = {"None", "Left", "Right", "Stereo"};
static const char *wm9712_alc_mux[] = {"Stereo", "Left", "Right", "None"};
static const char *wm9712_out3_src[] = {"Left", "VREF", "Left + Right",
	"Mono"};
static const char *wm9712_spk_src[] = {"Speaker Mix", "Headphone Mix"};
static const char *wm9712_rec_adc[] = {"Stereo", "Left", "Right", "Mute"};
static const char *wm9712_base[] = {"Linear Control", "Adaptive Boost"};
static const char *wm9712_rec_gain[] = {"+1.5dB Steps", "+0.75dB Steps"};
static const char *wm9712_mic[] = {"Mic 1", "Differential", "Mic 2",
	"Stereo"};
static const char *wm9712_rec_sel[] = {"Mic", "NC", "NC", "Speaker Mixer",
	"Line", "Headphone Mixer", "Phone Mixer", "Phone"};
static const char *wm9712_ng_type[] = {"Constant Gain", "Mute"};
static const char *wm9712_diff_sel[] = {"Mic", "Line"};

static const DECLARE_TLV_DB_SCALE(main_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, 0, 2000, 0);

static const struct soc_enum wm9712_enum[] = {
SOC_ENUM_SINGLE(AC97_PCI_SVID, 14, 4, wm9712_alc_select),
SOC_ENUM_SINGLE(AC97_VIDEO, 12, 4, wm9712_alc_mux),
SOC_ENUM_SINGLE(AC97_AUX, 9, 4, wm9712_out3_src),
SOC_ENUM_SINGLE(AC97_AUX, 8, 2, wm9712_spk_src),
SOC_ENUM_SINGLE(AC97_REC_SEL, 12, 4, wm9712_rec_adc),
SOC_ENUM_SINGLE(AC97_MASTER_TONE, 15, 2, wm9712_base),
SOC_ENUM_DOUBLE(AC97_REC_GAIN, 14, 6, 2, wm9712_rec_gain),
SOC_ENUM_SINGLE(AC97_MIC, 5, 4, wm9712_mic),
SOC_ENUM_SINGLE(AC97_REC_SEL, 8, 8, wm9712_rec_sel),
SOC_ENUM_SINGLE(AC97_REC_SEL, 0, 8, wm9712_rec_sel),
SOC_ENUM_SINGLE(AC97_PCI_SVID, 5, 2, wm9712_ng_type),
SOC_ENUM_SINGLE(0x5c, 8, 2, wm9712_diff_sel),
};

static const struct snd_kcontrol_new wm9712_snd_ac97_controls[] = {
SOC_DOUBLE("Speaker Playback Volume", AC97_MASTER, 8, 0, 31, 1),
SOC_SINGLE("Speaker Playback Switch", AC97_MASTER, 15, 1, 1),
SOC_DOUBLE("Headphone Playback Volume", AC97_HEADPHONE, 8, 0, 31, 1),
SOC_SINGLE("Headphone Playback Switch", AC97_HEADPHONE, 15, 1, 1),
SOC_DOUBLE("PCM Playback Volume", AC97_PCM, 8, 0, 31, 1),

SOC_SINGLE("Speaker Playback ZC Switch", AC97_MASTER, 7, 1, 0),
SOC_SINGLE("Speaker Playback Invert Switch", AC97_MASTER, 6, 1, 0),
SOC_SINGLE("Headphone Playback ZC Switch", AC97_HEADPHONE, 7, 1, 0),
SOC_SINGLE("Mono Playback ZC Switch", AC97_MASTER_MONO, 7, 1, 0),
SOC_SINGLE("Mono Playback Volume", AC97_MASTER_MONO, 0, 31, 1),
SOC_SINGLE("Mono Playback Switch", AC97_MASTER_MONO, 15, 1, 1),

SOC_SINGLE("ALC Target Volume", AC97_CODEC_CLASS_REV, 12, 15, 0),
SOC_SINGLE("ALC Hold Time", AC97_CODEC_CLASS_REV, 8, 15, 0),
SOC_SINGLE("ALC Decay Time", AC97_CODEC_CLASS_REV, 4, 15, 0),
SOC_SINGLE("ALC Attack Time", AC97_CODEC_CLASS_REV, 0, 15, 0),
SOC_ENUM("ALC Function", wm9712_enum[0]),
SOC_SINGLE("ALC Max Volume", AC97_PCI_SVID, 11, 7, 0),
SOC_SINGLE("ALC ZC Timeout", AC97_PCI_SVID, 9, 3, 1),
SOC_SINGLE("ALC ZC Switch", AC97_PCI_SVID, 8, 1, 0),
SOC_SINGLE("ALC NG Switch", AC97_PCI_SVID, 7, 1, 0),
SOC_ENUM("ALC NG Type", wm9712_enum[10]),
SOC_SINGLE("ALC NG Threshold", AC97_PCI_SVID, 0, 31, 1),

SOC_SINGLE("Mic Headphone  Volume", AC97_VIDEO, 12, 7, 1),
SOC_SINGLE("ALC Headphone Volume", AC97_VIDEO, 7, 7, 1),

SOC_SINGLE("Out3 Switch", AC97_AUX, 15, 1, 1),
SOC_SINGLE("Out3 ZC Switch", AC97_AUX, 7, 1, 1),
SOC_SINGLE("Out3 Volume", AC97_AUX, 0, 31, 1),

SOC_SINGLE("PCBeep Bypass Headphone Volume", AC97_PC_BEEP, 12, 7, 1),
SOC_SINGLE("PCBeep Bypass Speaker Volume", AC97_PC_BEEP, 8, 7, 1),
SOC_SINGLE("PCBeep Bypass Phone Volume", AC97_PC_BEEP, 4, 7, 1),

SOC_SINGLE("Aux Playback Headphone Volume", AC97_CD, 12, 7, 1),
SOC_SINGLE("Aux Playback Speaker Volume", AC97_CD, 8, 7, 1),
SOC_SINGLE("Aux Playback Phone Volume", AC97_CD, 4, 7, 1),

SOC_SINGLE("Phone Volume", AC97_PHONE, 0, 15, 1),
SOC_DOUBLE("Line Capture Volume", AC97_LINE, 8, 0, 31, 1),

SOC_SINGLE_TLV("Capture Boost Switch", AC97_REC_SEL, 14, 1, 0, boost_tlv),
SOC_SINGLE_TLV("Capture to Phone Boost Switch", AC97_REC_SEL, 11, 1, 1,
	       boost_tlv),

SOC_SINGLE("3D Upper Cut-off Switch", AC97_3D_CONTROL, 5, 1, 1),
SOC_SINGLE("3D Lower Cut-off Switch", AC97_3D_CONTROL, 4, 1, 1),
SOC_SINGLE("3D Playback Volume", AC97_3D_CONTROL, 0, 15, 0),

SOC_ENUM("Bass Control", wm9712_enum[5]),
SOC_SINGLE("Bass Cut-off Switch", AC97_MASTER_TONE, 12, 1, 1),
SOC_SINGLE("Tone Cut-off Switch", AC97_MASTER_TONE, 4, 1, 1),
SOC_SINGLE("Playback Attenuate (-6dB) Switch", AC97_MASTER_TONE, 6, 1, 0),
SOC_SINGLE("Bass Volume", AC97_MASTER_TONE, 8, 15, 1),
SOC_SINGLE("Treble Volume", AC97_MASTER_TONE, 0, 15, 1),

SOC_SINGLE("Capture Switch", AC97_REC_GAIN, 15, 1, 1),
SOC_ENUM("Capture Volume Steps", wm9712_enum[6]),
SOC_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 63, 0),
SOC_SINGLE("Capture ZC Switch", AC97_REC_GAIN, 7, 1, 0),

SOC_SINGLE_TLV("Mic 1 Volume", AC97_MIC, 8, 31, 1, main_tlv),
SOC_SINGLE_TLV("Mic 2 Volume", AC97_MIC, 0, 31, 1, main_tlv),
SOC_SINGLE_TLV("Mic Boost Volume", AC97_MIC, 7, 1, 0, boost_tlv),
};

static const unsigned int wm9712_mixer_mute_regs[] = {
	AC97_VIDEO,
	AC97_PCM,
	AC97_LINE,
	AC97_PHONE,
	AC97_CD,
	AC97_PC_BEEP,
};

/* We have to create a fake left and right HP mixers because
 * the codec only has a single control that is shared by both channels.
 * This makes it impossible to determine the audio path.
 */
static int wm9712_hp_mixer_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	struct wm9712_priv *wm9712 = snd_soc_component_get_drvdata(component);
	unsigned int val = ucontrol->value.integer.value[0];
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mixer, mask, shift, old;
	struct snd_soc_dapm_update update = {};
	bool change;

	mixer = mc->shift >> 8;
	shift = mc->shift & 0xff;
	mask = 1 << shift;

	mutex_lock(&wm9712->lock);
	old = wm9712->hp_mixer[mixer];
	if (ucontrol->value.integer.value[0])
		wm9712->hp_mixer[mixer] |= mask;
	else
		wm9712->hp_mixer[mixer] &= ~mask;

	change = old != wm9712->hp_mixer[mixer];
	if (change) {
		update.kcontrol = kcontrol;
		update.reg = wm9712_mixer_mute_regs[shift];
		update.mask = 0x8000;
		if ((wm9712->hp_mixer[0] & mask) ||
		    (wm9712->hp_mixer[1] & mask))
			update.val = 0x0;
		else
			update.val = 0x8000;

		snd_soc_dapm_mixer_update_power(dapm, kcontrol, val,
			&update);
	}

	mutex_unlock(&wm9712->lock);

	return change;
}

static int wm9712_hp_mixer_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *component = snd_soc_dapm_to_component(dapm);
	struct wm9712_priv *wm9712 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int shift, mixer;

	mixer = mc->shift >> 8;
	shift = mc->shift & 0xff;

	ucontrol->value.integer.value[0] =
		(wm9712->hp_mixer[mixer] >> shift) & 1;

	return 0;
}

#define WM9712_HP_MIXER_CTRL(xname, xmixer, xshift) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = wm9712_hp_mixer_get, .put = wm9712_hp_mixer_put, \
	.private_value = SOC_SINGLE_VALUE(SND_SOC_NOPM, \
		(xmixer << 8) | xshift, 1, 0, 0) \
}

/* Left Headphone Mixers */
static const struct snd_kcontrol_new wm9712_hpl_mixer_controls[] = {
	WM9712_HP_MIXER_CTRL("PCBeep Bypass Switch", HPL_MIXER, 5),
	WM9712_HP_MIXER_CTRL("Aux Playback Switch", HPL_MIXER, 4),
	WM9712_HP_MIXER_CTRL("Phone Bypass Switch", HPL_MIXER, 3),
	WM9712_HP_MIXER_CTRL("Line Bypass Switch", HPL_MIXER, 2),
	WM9712_HP_MIXER_CTRL("PCM Playback Switch", HPL_MIXER, 1),
	WM9712_HP_MIXER_CTRL("Mic Sidetone Switch", HPL_MIXER, 0),
};

/* Right Headphone Mixers */
static const struct snd_kcontrol_new wm9712_hpr_mixer_controls[] = {
	WM9712_HP_MIXER_CTRL("PCBeep Bypass Switch", HPR_MIXER, 5),
	WM9712_HP_MIXER_CTRL("Aux Playback Switch", HPR_MIXER, 4),
	WM9712_HP_MIXER_CTRL("Phone Bypass Switch", HPR_MIXER, 3),
	WM9712_HP_MIXER_CTRL("Line Bypass Switch", HPR_MIXER, 2),
	WM9712_HP_MIXER_CTRL("PCM Playback Switch", HPR_MIXER, 1),
	WM9712_HP_MIXER_CTRL("Mic Sidetone Switch", HPR_MIXER, 0),
};

/* Speaker Mixer */
static const struct snd_kcontrol_new wm9712_speaker_mixer_controls[] = {
	SOC_DAPM_SINGLE("PCBeep Bypass Switch", AC97_PC_BEEP, 11, 1, 1),
	SOC_DAPM_SINGLE("Aux Playback Switch", AC97_CD, 11, 1, 1),
	SOC_DAPM_SINGLE("Phone Bypass Switch", AC97_PHONE, 14, 1, 1),
	SOC_DAPM_SINGLE("Line Bypass Switch", AC97_LINE, 14, 1, 1),
	SOC_DAPM_SINGLE("PCM Playback Switch", AC97_PCM, 14, 1, 1),
};

/* Phone Mixer */
static const struct snd_kcontrol_new wm9712_phone_mixer_controls[] = {
	SOC_DAPM_SINGLE("PCBeep Bypass Switch", AC97_PC_BEEP, 7, 1, 1),
	SOC_DAPM_SINGLE("Aux Playback Switch", AC97_CD, 7, 1, 1),
	SOC_DAPM_SINGLE("Line Bypass Switch", AC97_LINE, 13, 1, 1),
	SOC_DAPM_SINGLE("PCM Playback Switch", AC97_PCM, 13, 1, 1),
	SOC_DAPM_SINGLE("Mic 1 Sidetone Switch", AC97_MIC, 14, 1, 1),
	SOC_DAPM_SINGLE("Mic 2 Sidetone Switch", AC97_MIC, 13, 1, 1),
};

/* ALC headphone mux */
static const struct snd_kcontrol_new wm9712_alc_mux_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[1]);

/* out 3 mux */
static const struct snd_kcontrol_new wm9712_out3_mux_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[2]);

/* spk mux */
static const struct snd_kcontrol_new wm9712_spk_mux_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[3]);

/* Capture to Phone mux */
static const struct snd_kcontrol_new wm9712_capture_phone_mux_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[4]);

/* Capture left select */
static const struct snd_kcontrol_new wm9712_capture_selectl_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[8]);

/* Capture right select */
static const struct snd_kcontrol_new wm9712_capture_selectr_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[9]);

/* Mic select */
static const struct snd_kcontrol_new wm9712_mic_src_controls =
SOC_DAPM_ENUM("Mic Source Select", wm9712_enum[7]);

/* diff select */
static const struct snd_kcontrol_new wm9712_diff_sel_controls =
SOC_DAPM_ENUM("Route", wm9712_enum[11]);

static const struct snd_soc_dapm_widget wm9712_dapm_widgets[] = {
SND_SOC_DAPM_MUX("ALC Sidetone Mux", SND_SOC_NOPM, 0, 0,
	&wm9712_alc_mux_controls),
SND_SOC_DAPM_MUX("Out3 Mux", SND_SOC_NOPM, 0, 0,
	&wm9712_out3_mux_controls),
SND_SOC_DAPM_MUX("Speaker Mux", SND_SOC_NOPM, 0, 0,
	&wm9712_spk_mux_controls),
SND_SOC_DAPM_MUX("Capture Phone Mux", SND_SOC_NOPM, 0, 0,
	&wm9712_capture_phone_mux_controls),
SND_SOC_DAPM_MUX("Left Capture Select", SND_SOC_NOPM, 0, 0,
	&wm9712_capture_selectl_controls),
SND_SOC_DAPM_MUX("Right Capture Select", SND_SOC_NOPM, 0, 0,
	&wm9712_capture_selectr_controls),
SND_SOC_DAPM_MUX("Left Mic Select Source", SND_SOC_NOPM, 0, 0,
	&wm9712_mic_src_controls),
SND_SOC_DAPM_MUX("Right Mic Select Source", SND_SOC_NOPM, 0, 0,
	&wm9712_mic_src_controls),
SND_SOC_DAPM_MUX("Differential Source", SND_SOC_NOPM, 0, 0,
	&wm9712_diff_sel_controls),
SND_SOC_DAPM_MIXER("AC97 Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Left HP Mixer", AC97_INT_PAGING, 9, 1,
	&wm9712_hpl_mixer_controls[0], ARRAY_SIZE(wm9712_hpl_mixer_controls)),
SND_SOC_DAPM_MIXER("Right HP Mixer", AC97_INT_PAGING, 8, 1,
	&wm9712_hpr_mixer_controls[0], ARRAY_SIZE(wm9712_hpr_mixer_controls)),
SND_SOC_DAPM_MIXER("Phone Mixer", AC97_INT_PAGING, 6, 1,
	&wm9712_phone_mixer_controls[0], ARRAY_SIZE(wm9712_phone_mixer_controls)),
SND_SOC_DAPM_MIXER("Speaker Mixer", AC97_INT_PAGING, 7, 1,
	&wm9712_speaker_mixer_controls[0],
	ARRAY_SIZE(wm9712_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("Mono Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback", AC97_INT_PAGING, 14, 1),
SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback", AC97_INT_PAGING, 13, 1),
SND_SOC_DAPM_DAC("Aux DAC", "Aux Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_ADC("Left ADC", "Left HiFi Capture", AC97_INT_PAGING, 12, 1),
SND_SOC_DAPM_ADC("Right ADC", "Right HiFi Capture", AC97_INT_PAGING, 11, 1),
SND_SOC_DAPM_PGA("Headphone PGA", AC97_INT_PAGING, 4, 1, NULL, 0),
SND_SOC_DAPM_PGA("Speaker PGA", AC97_INT_PAGING, 3, 1, NULL, 0),
SND_SOC_DAPM_PGA("Out 3 PGA", AC97_INT_PAGING, 5, 1, NULL, 0),
SND_SOC_DAPM_PGA("Line PGA", AC97_INT_PAGING, 2, 1, NULL, 0),
SND_SOC_DAPM_PGA("Phone PGA", AC97_INT_PAGING, 1, 1, NULL, 0),
SND_SOC_DAPM_PGA("Mic PGA", AC97_INT_PAGING, 0, 1, NULL, 0),
SND_SOC_DAPM_PGA("Differential Mic", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias", AC97_INT_PAGING, 10, 1),
SND_SOC_DAPM_OUTPUT("MONOOUT"),
SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
SND_SOC_DAPM_OUTPUT("LOUT2"),
SND_SOC_DAPM_OUTPUT("ROUT2"),
SND_SOC_DAPM_OUTPUT("OUT3"),
SND_SOC_DAPM_INPUT("LINEINL"),
SND_SOC_DAPM_INPUT("LINEINR"),
SND_SOC_DAPM_INPUT("PHONE"),
SND_SOC_DAPM_INPUT("PCBEEP"),
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
};

static const struct snd_soc_dapm_route wm9712_audio_map[] = {
	/* virtual mixer - mixes left & right channels for spk and mono */
	{"AC97 Mixer", NULL, "Left DAC"},
	{"AC97 Mixer", NULL, "Right DAC"},

	/* Left HP mixer */
	{"Left HP Mixer", "PCBeep Bypass Switch", "PCBEEP"},
	{"Left HP Mixer", "Aux Playback Switch",  "Aux DAC"},
	{"Left HP Mixer", "Phone Bypass Switch",  "Phone PGA"},
	{"Left HP Mixer", "Line Bypass Switch",   "Line PGA"},
	{"Left HP Mixer", "PCM Playback Switch",  "Left DAC"},
	{"Left HP Mixer", "Mic Sidetone Switch",  "Mic PGA"},
	{"Left HP Mixer", NULL,  "ALC Sidetone Mux"},

	/* Right HP mixer */
	{"Right HP Mixer", "PCBeep Bypass Switch", "PCBEEP"},
	{"Right HP Mixer", "Aux Playback Switch",  "Aux DAC"},
	{"Right HP Mixer", "Phone Bypass Switch",  "Phone PGA"},
	{"Right HP Mixer", "Line Bypass Switch",   "Line PGA"},
	{"Right HP Mixer", "PCM Playback Switch",  "Right DAC"},
	{"Right HP Mixer", "Mic Sidetone Switch",  "Mic PGA"},
	{"Right HP Mixer", NULL,  "ALC Sidetone Mux"},

	/* speaker mixer */
	{"Speaker Mixer", "PCBeep Bypass Switch", "PCBEEP"},
	{"Speaker Mixer", "Line Bypass Switch",   "Line PGA"},
	{"Speaker Mixer", "PCM Playback Switch",  "AC97 Mixer"},
	{"Speaker Mixer", "Phone Bypass Switch",  "Phone PGA"},
	{"Speaker Mixer", "Aux Playback Switch",  "Aux DAC"},

	/* Phone mixer */
	{"Phone Mixer", "PCBeep Bypass Switch",  "PCBEEP"},
	{"Phone Mixer", "Line Bypass Switch",    "Line PGA"},
	{"Phone Mixer", "Aux Playback Switch",   "Aux DAC"},
	{"Phone Mixer", "PCM Playback Switch",   "AC97 Mixer"},
	{"Phone Mixer", "Mic 1 Sidetone Switch", "Mic PGA"},
	{"Phone Mixer", "Mic 2 Sidetone Switch", "Mic PGA"},

	/* inputs */
	{"Line PGA", NULL, "LINEINL"},
	{"Line PGA", NULL, "LINEINR"},
	{"Phone PGA", NULL, "PHONE"},
	{"Mic PGA", NULL, "MIC1"},
	{"Mic PGA", NULL, "MIC2"},

	/* microphones */
	{"Differential Mic", NULL, "MIC1"},
	{"Differential Mic", NULL, "MIC2"},
	{"Left Mic Select Source", "Mic 1", "MIC1"},
	{"Left Mic Select Source", "Mic 2", "MIC2"},
	{"Left Mic Select Source", "Stereo", "MIC1"},
	{"Left Mic Select Source", "Differential", "Differential Mic"},
	{"Right Mic Select Source", "Mic 1", "MIC1"},
	{"Right Mic Select Source", "Mic 2", "MIC2"},
	{"Right Mic Select Source", "Stereo", "MIC2"},
	{"Right Mic Select Source", "Differential", "Differential Mic"},

	/* left capture selector */
	{"Left Capture Select", "Mic", "MIC1"},
	{"Left Capture Select", "Speaker Mixer", "Speaker Mixer"},
	{"Left Capture Select", "Line", "LINEINL"},
	{"Left Capture Select", "Headphone Mixer", "Left HP Mixer"},
	{"Left Capture Select", "Phone Mixer", "Phone Mixer"},
	{"Left Capture Select", "Phone", "PHONE"},

	/* right capture selector */
	{"Right Capture Select", "Mic", "MIC2"},
	{"Right Capture Select", "Speaker Mixer", "Speaker Mixer"},
	{"Right Capture Select", "Line", "LINEINR"},
	{"Right Capture Select", "Headphone Mixer", "Right HP Mixer"},
	{"Right Capture Select", "Phone Mixer", "Phone Mixer"},
	{"Right Capture Select", "Phone", "PHONE"},

	/* ALC Sidetone */
	{"ALC Sidetone Mux", "Stereo", "Left Capture Select"},
	{"ALC Sidetone Mux", "Stereo", "Right Capture Select"},
	{"ALC Sidetone Mux", "Left", "Left Capture Select"},
	{"ALC Sidetone Mux", "Right", "Right Capture Select"},

	/* ADC's */
	{"Left ADC", NULL, "Left Capture Select"},
	{"Right ADC", NULL, "Right Capture Select"},

	/* outputs */
	{"MONOOUT", NULL, "Phone Mixer"},
	{"HPOUTL", NULL, "Headphone PGA"},
	{"Headphone PGA", NULL, "Left HP Mixer"},
	{"HPOUTR", NULL, "Headphone PGA"},
	{"Headphone PGA", NULL, "Right HP Mixer"},

	/* mono mixer */
	{"Mono Mixer", NULL, "Left HP Mixer"},
	{"Mono Mixer", NULL, "Right HP Mixer"},

	/* Out3 Mux */
	{"Out3 Mux", "Left", "Left HP Mixer"},
	{"Out3 Mux", "Mono", "Phone Mixer"},
	{"Out3 Mux", "Left + Right", "Mono Mixer"},
	{"Out 3 PGA", NULL, "Out3 Mux"},
	{"OUT3", NULL, "Out 3 PGA"},

	/* speaker Mux */
	{"Speaker Mux", "Speaker Mix", "Speaker Mixer"},
	{"Speaker Mux", "Headphone Mix", "Mono Mixer"},
	{"Speaker PGA", NULL, "Speaker Mux"},
	{"LOUT2", NULL, "Speaker PGA"},
	{"ROUT2", NULL, "Speaker PGA"},
};

static int ac97_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int reg;
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_soc_component_update_bits(component, AC97_EXTENDED_STATUS, 0x1, 0x1);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = AC97_PCM_FRONT_DAC_RATE;
	else
		reg = AC97_PCM_LR_ADC_RATE;

	return snd_soc_component_write(component, reg, runtime->rate);
}

static int ac97_aux_prepare(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_soc_component_update_bits(component, AC97_EXTENDED_STATUS, 0x1, 0x1);
	snd_soc_component_update_bits(component, AC97_PCI_SID, 0x8000, 0x8000);

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -ENODEV;

	return snd_soc_component_write(component, AC97_PCM_SURR_DAC_RATE, runtime->rate);
}

#define WM9712_AC97_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 |\
		SNDRV_PCM_RATE_48000)

static const struct snd_soc_dai_ops wm9712_dai_ops_hifi = {
	.prepare	= ac97_prepare,
};

static const struct snd_soc_dai_ops wm9712_dai_ops_aux = {
	.prepare	= ac97_aux_prepare,
};

static struct snd_soc_dai_driver wm9712_dai[] = {
{
	.name = "wm9712-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM9712_AC97_RATES,
		.formats = SND_SOC_STD_AC97_FMTS,},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM9712_AC97_RATES,
		.formats = SND_SOC_STD_AC97_FMTS,},
	.ops = &wm9712_dai_ops_hifi,
},
{
	.name = "wm9712-aux",
	.playback = {
		.stream_name = "Aux Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM9712_AC97_RATES,
		.formats = SND_SOC_STD_AC97_FMTS,},
	.ops = &wm9712_dai_ops_aux,
}
};

static int wm9712_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_write(component, AC97_POWERDOWN, 0x0000);
		break;
	case SND_SOC_BIAS_OFF:
		/* disable everything including AC link */
		snd_soc_component_write(component, AC97_EXTENDED_MSTATUS, 0xffff);
		snd_soc_component_write(component, AC97_POWERDOWN, 0xffff);
		break;
	}
	return 0;
}

static int wm9712_soc_resume(struct snd_soc_component *component)
{
	struct wm9712_priv *wm9712 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = snd_ac97_reset(wm9712->ac97, true, WM9712_VENDOR_ID,
		WM9712_VENDOR_ID_MASK);
	if (ret < 0)
		return ret;

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);

	if (ret == 0)
		snd_soc_component_cache_sync(component);

	return ret;
}

static int wm9712_soc_probe(struct snd_soc_component *component)
{
	struct wm9712_priv *wm9712 = snd_soc_component_get_drvdata(component);
	struct regmap *regmap;

	if (wm9712->mfd_pdata) {
		wm9712->ac97 = wm9712->mfd_pdata->ac97;
		regmap = wm9712->mfd_pdata->regmap;
	} else if (IS_ENABLED(CONFIG_SND_SOC_AC97_BUS)) {
		int ret;

		wm9712->ac97 = snd_soc_new_ac97_component(component, WM9712_VENDOR_ID,
						      WM9712_VENDOR_ID_MASK);
		if (IS_ERR(wm9712->ac97)) {
			ret = PTR_ERR(wm9712->ac97);
			dev_err(component->dev,
				"Failed to register AC97 codec: %d\n", ret);
			return ret;
		}

		regmap = regmap_init_ac97(wm9712->ac97, &wm9712_regmap_config);
		if (IS_ERR(regmap)) {
			snd_soc_free_ac97_component(wm9712->ac97);
			return PTR_ERR(regmap);
		}
	} else {
		return -ENXIO;
	}

	snd_soc_component_init_regmap(component, regmap);

	/* set alc mux to none */
	snd_soc_component_update_bits(component, AC97_VIDEO, 0x3000, 0x3000);

	return 0;
}

static void wm9712_soc_remove(struct snd_soc_component *component)
{
	struct wm9712_priv *wm9712 = snd_soc_component_get_drvdata(component);

	if (IS_ENABLED(CONFIG_SND_SOC_AC97_BUS) && !wm9712->mfd_pdata) {
		snd_soc_component_exit_regmap(component);
		snd_soc_free_ac97_component(wm9712->ac97);
	}
}

static const struct snd_soc_component_driver soc_component_dev_wm9712 = {
	.probe			= wm9712_soc_probe,
	.remove			= wm9712_soc_remove,
	.resume			= wm9712_soc_resume,
	.set_bias_level		= wm9712_set_bias_level,
	.controls		= wm9712_snd_ac97_controls,
	.num_controls		= ARRAY_SIZE(wm9712_snd_ac97_controls),
	.dapm_widgets		= wm9712_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm9712_dapm_widgets),
	.dapm_routes		= wm9712_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(wm9712_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int wm9712_probe(struct platform_device *pdev)
{
	struct wm9712_priv *wm9712;

	wm9712 = devm_kzalloc(&pdev->dev, sizeof(*wm9712), GFP_KERNEL);
	if (wm9712 == NULL)
		return -ENOMEM;

	mutex_init(&wm9712->lock);

	wm9712->mfd_pdata = dev_get_platdata(&pdev->dev);
	platform_set_drvdata(pdev, wm9712);

	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_wm9712, wm9712_dai, ARRAY_SIZE(wm9712_dai));
}

static struct platform_driver wm9712_component_driver = {
	.driver = {
		.name = "wm9712-codec",
	},

	.probe = wm9712_probe,
};

module_platform_driver(wm9712_component_driver);

MODULE_DESCRIPTION("ASoC WM9711/WM9712 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
