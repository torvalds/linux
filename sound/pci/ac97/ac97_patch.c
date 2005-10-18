/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com) and to datasheets
 *  for specific codecs.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>
#include "ac97_patch.h"
#include "ac97_id.h"
#include "ac97_local.h"

/*
 *  Chip specific initialization
 */

static int patch_build_controls(ac97_t * ac97, const snd_kcontrol_new_t *controls, int count)
{
	int idx, err;

	for (idx = 0; idx < count; idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&controls[idx], ac97))) < 0)
			return err;
	return 0;
}

/* set to the page, update bits and restore the page */
static int ac97_update_bits_page(ac97_t *ac97, unsigned short reg, unsigned short mask, unsigned short value, unsigned short page)
{
	unsigned short page_save;
	int ret;

	down(&ac97->page_mutex);
	page_save = snd_ac97_read(ac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page);
	ret = snd_ac97_update_bits(ac97, reg, mask, value);
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page_save);
	up(&ac97->page_mutex); /* unlock paging */
	return ret;
}

/*
 * shared line-in/mic controls
 */
static int ac97_enum_text_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo,
			       const char **texts, unsigned int nums)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = nums;
	if (uinfo->value.enumerated.item > nums - 1)
		uinfo->value.enumerated.item = nums - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int ac97_surround_jack_mode_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static const char *texts[] = { "Shared", "Independent" };
	return ac97_enum_text_info(kcontrol, uinfo, texts, 2);
}

static int ac97_surround_jack_mode_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->indep_surround;
	return 0;
}

static int ac97_surround_jack_mode_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char indep = !!ucontrol->value.enumerated.item[0];

	if (indep != ac97->indep_surround) {
		ac97->indep_surround = indep;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

static int ac97_channel_mode_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static const char *texts[] = { "2ch", "4ch", "6ch" };
	if (kcontrol->private_value)
		return ac97_enum_text_info(kcontrol, uinfo, texts, 2); /* 4ch only */
	return ac97_enum_text_info(kcontrol, uinfo, texts, 3);
}

static int ac97_channel_mode_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->channel_mode;
	return 0;
}

static int ac97_channel_mode_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char mode = ucontrol->value.enumerated.item[0];

	if (mode != ac97->channel_mode) {
		ac97->channel_mode = mode;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

#define AC97_SURROUND_JACK_MODE_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Surround Jack Mode", \
		.info = ac97_surround_jack_mode_info, \
		.get = ac97_surround_jack_mode_get, \
		.put = ac97_surround_jack_mode_put, \
	}
#define AC97_CHANNEL_MODE_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
	}
#define AC97_CHANNEL_MODE_4CH_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 1, \
	}

static inline int is_shared_linein(ac97_t *ac97)
{
	return ! ac97->indep_surround && ac97->channel_mode >= 1;
}

static inline int is_shared_micin(ac97_t *ac97)
{
	return ! ac97->indep_surround && ac97->channel_mode >= 2;
}


/* The following snd_ac97_ymf753_... items added by David Shust (dshust@shustring.com) */

/* It is possible to indicate to the Yamaha YMF753 the type of speakers being used. */
static int snd_ac97_ymf753_info_speaker(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[3] = {
		"Standard", "Small", "Smaller"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf753_get_speaker(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF753_3D_MODE_SEL];
	val = (val >> 10) & 3;
	if (val > 0)    /* 0 = invalid */
		val--;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_ymf753_put_speaker(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] + 1) << 10;
	return snd_ac97_update(ac97, AC97_YMF753_3D_MODE_SEL, val);
}

static const snd_kcontrol_new_t snd_ac97_ymf753_controls_speaker =
{
	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name   = "3D Control - Speaker",
	.info   = snd_ac97_ymf753_info_speaker,
	.get    = snd_ac97_ymf753_get_speaker,
	.put    = snd_ac97_ymf753_put_speaker,
};

/* It is possible to indicate to the Yamaha YMF753 the source to direct to the S/PDIF output. */
static int snd_ac97_ymf753_spdif_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[2] = { "AC-Link", "A/D Converter" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf753_spdif_source_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF753_DIT_CTRL2];
	ucontrol->value.enumerated.item[0] = (val >> 1) & 1;
	return 0;
}

static int snd_ac97_ymf753_spdif_source_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 1;
	return snd_ac97_update_bits(ac97, AC97_YMF753_DIT_CTRL2, 0x0002, val);
}

/* The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
   The YMF753 will output the S/PDIF signal to pin 43, 47 (EAPD), or 48.
   By default, no output pin is selected, and the S/PDIF signal is not output.
   There is also a bit to mute S/PDIF output in a vendor-specific register. */
static int snd_ac97_ymf753_spdif_output_pin_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[3] = { "Disabled", "Pin 43", "Pin 48" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ymf753_spdif_output_pin_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF753_DIT_CTRL2];
	ucontrol->value.enumerated.item[0] = (val & 0x0008) ? 2 : (val & 0x0020) ? 1 : 0;
	return 0;
}

static int snd_ac97_ymf753_spdif_output_pin_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] == 2) ? 0x0008 :
	      (ucontrol->value.enumerated.item[0] == 1) ? 0x0020 : 0;
	return snd_ac97_update_bits(ac97, AC97_YMF753_DIT_CTRL2, 0x0028, val);
	/* The following can be used to direct S/PDIF output to pin 47 (EAPD).
	   snd_ac97_write_cache(ac97, 0x62, snd_ac97_read(ac97, 0x62) | 0x0008); */
}

static const snd_kcontrol_new_t snd_ac97_ymf753_controls_spdif[3] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info	= snd_ac97_ymf753_spdif_source_info,
		.get	= snd_ac97_ymf753_spdif_source_get,
		.put	= snd_ac97_ymf753_spdif_source_put,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Output Pin",
		.info	= snd_ac97_ymf753_spdif_output_pin_info,
		.get	= snd_ac97_ymf753_spdif_output_pin_get,
		.put	= snd_ac97_ymf753_spdif_output_pin_put,
	},
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",NONE,NONE) "Mute", AC97_YMF753_DIT_CTRL2, 2, 1, 1)
};

static int patch_yamaha_ymf753_3d(ac97_t * ac97)
{
	snd_kcontrol_t *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control - Wide");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 9, 7, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&snd_ac97_ymf753_controls_speaker, ac97))) < 0)
		return err;
	snd_ac97_write_cache(ac97, AC97_YMF753_3D_MODE_SEL, 0x0c00);
	return 0;
}

static int patch_yamaha_ymf753_post_spdif(ac97_t * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_ymf753_controls_spdif, ARRAY_SIZE(snd_ac97_ymf753_controls_spdif))) < 0)
		return err;
	return 0;
}

static struct snd_ac97_build_ops patch_yamaha_ymf753_ops = {
	.build_3d	= patch_yamaha_ymf753_3d,
	.build_post_spdif = patch_yamaha_ymf753_post_spdif
};

int patch_yamaha_ymf753(ac97_t * ac97)
{
	/* Patch for Yamaha YMF753, Copyright (c) by David Shust, dshust@shustring.com.
	   This chip has nonstandard and extended behaviour with regard to its S/PDIF output.
	   The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
	   The YMF753 will ouput the S/PDIF signal to pin 43, 47 (EAPD), or 48.
	   By default, no output pin is selected, and the S/PDIF signal is not output.
	   There is also a bit to mute S/PDIF output in a vendor-specific register.
	*/
	ac97->build_ops = &patch_yamaha_ymf753_ops;
	ac97->caps |= AC97_BC_BASS_TREBLE;
	ac97->caps |= 0x04 << 10; /* Yamaha 3D enhancement */
	return 0;
}

/*
 * May 2, 2003 Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *  removed broken wolfson00 patch.
 *  added support for WM9705,WM9708,WM9709,WM9710,WM9711,WM9712 and WM9717.
 */

static const snd_kcontrol_new_t wm97xx_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
};

static int patch_wolfson_wm9703_specific(ac97_t * ac97)
{
	/* This is known to work for the ViewSonic ViewPad 1000
	 * Randolph Bentson <bentson@holmsjoen.com>
	 * WM9703/9707/9708/9717 
	 */
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm97xx_snd_ac97_controls); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm97xx_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97,  AC97_WM97XX_FMIXER_VOL, 0x0808);
	return 0;
}

static struct snd_ac97_build_ops patch_wolfson_wm9703_ops = {
	.build_specific = patch_wolfson_wm9703_specific,
};

int patch_wolfson03(ac97_t * ac97)
{
	ac97->build_ops = &patch_wolfson_wm9703_ops;
	return 0;
}

static const snd_kcontrol_new_t wm9704_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear Playback Volume", AC97_WM9704_RMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Rear Playback Switch", AC97_WM9704_RMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear DAC Volume", AC97_WM9704_RPCM_VOL, 8, 0, 31, 1),
AC97_DOUBLE("Surround Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
};

static int patch_wolfson_wm9704_specific(ac97_t * ac97)
{
	int err, i;
	for (i = 0; i < ARRAY_SIZE(wm9704_snd_ac97_controls); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm9704_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	/* patch for DVD noise */
	snd_ac97_write_cache(ac97, AC97_WM9704_TEST, 0x0200);
	return 0;
}

static struct snd_ac97_build_ops patch_wolfson_wm9704_ops = {
	.build_specific = patch_wolfson_wm9704_specific,
};

int patch_wolfson04(ac97_t * ac97)
{
	/* WM9704M/9704Q */
	ac97->build_ops = &patch_wolfson_wm9704_ops;
	return 0;
}

static int patch_wolfson_wm9705_specific(ac97_t * ac97)
{
	int err, i;
	for (i = 0; i < ARRAY_SIZE(wm97xx_snd_ac97_controls); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm97xx_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97,  0x72, 0x0808);
	return 0;
}

static struct snd_ac97_build_ops patch_wolfson_wm9705_ops = {
	.build_specific = patch_wolfson_wm9705_specific,
};

int patch_wolfson05(ac97_t * ac97)
{
	/* WM9705, WM9710 */
	ac97->build_ops = &patch_wolfson_wm9705_ops;
	return 0;
}

static const char* wm9711_alc_select[] = {"None", "Left", "Right", "Stereo"};
static const char* wm9711_alc_mix[] = {"Stereo", "Right", "Left", "None"};
static const char* wm9711_out3_src[] = {"Left", "VREF", "Left + Right", "Mono"};
static const char* wm9711_out3_lrsrc[] = {"Master Mix", "Headphone Mix"};
static const char* wm9711_rec_adc[] = {"Stereo", "Left", "Right", "Mute"};
static const char* wm9711_base[] = {"Linear Control", "Adaptive Boost"};
static const char* wm9711_rec_gain[] = {"+1.5dB Steps", "+0.75dB Steps"};
static const char* wm9711_mic[] = {"Mic 1", "Differential", "Mic 2", "Stereo"};
static const char* wm9711_rec_sel[] = 
	{"Mic 1", "NC", "NC", "Master Mix", "Line", "Headphone Mix", "Phone Mix", "Phone"};
static const char* wm9711_ng_type[] = {"Constant Gain", "Mute"};

static const struct ac97_enum wm9711_enum[] = {
AC97_ENUM_SINGLE(AC97_PCI_SVID, 14, 4, wm9711_alc_select),
AC97_ENUM_SINGLE(AC97_VIDEO, 10, 4, wm9711_alc_mix),
AC97_ENUM_SINGLE(AC97_AUX, 9, 4, wm9711_out3_src),
AC97_ENUM_SINGLE(AC97_AUX, 8, 2, wm9711_out3_lrsrc),
AC97_ENUM_SINGLE(AC97_REC_SEL, 12, 4, wm9711_rec_adc),
AC97_ENUM_SINGLE(AC97_MASTER_TONE, 15, 2, wm9711_base),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 14, 6, 2, wm9711_rec_gain),
AC97_ENUM_SINGLE(AC97_MIC, 5, 4, wm9711_mic),
AC97_ENUM_DOUBLE(AC97_REC_SEL, 8, 0, 8, wm9711_rec_sel),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 5, 2, wm9711_ng_type),
};

static const snd_kcontrol_new_t wm9711_snd_ac97_controls[] = {
AC97_SINGLE("ALC Target Volume", AC97_CODEC_CLASS_REV, 12, 15, 0),
AC97_SINGLE("ALC Hold Time", AC97_CODEC_CLASS_REV, 8, 15, 0),
AC97_SINGLE("ALC Decay Time", AC97_CODEC_CLASS_REV, 4, 15, 0),
AC97_SINGLE("ALC Attack Time", AC97_CODEC_CLASS_REV, 0, 15, 0),
AC97_ENUM("ALC Function", wm9711_enum[0]),
AC97_SINGLE("ALC Max Volume", AC97_PCI_SVID, 11, 7, 1),
AC97_SINGLE("ALC ZC Timeout", AC97_PCI_SVID, 9, 3, 1),
AC97_SINGLE("ALC ZC Switch", AC97_PCI_SVID, 8, 1, 0),
AC97_SINGLE("ALC NG Switch", AC97_PCI_SVID, 7, 1, 0),
AC97_ENUM("ALC NG Type", wm9711_enum[9]),
AC97_SINGLE("ALC NG Threshold", AC97_PCI_SVID, 0, 31, 1),

AC97_SINGLE("Side Tone Switch", AC97_VIDEO, 15, 1, 1),
AC97_SINGLE("Side Tone Volume", AC97_VIDEO, 12, 7, 1),
AC97_ENUM("ALC Headphone Mux", wm9711_enum[1]),
AC97_SINGLE("ALC Headphone Volume", AC97_VIDEO, 7, 7, 1),

AC97_SINGLE("Out3 Switch", AC97_AUX, 15, 1, 1),
AC97_SINGLE("Out3 ZC Switch", AC97_AUX, 7, 1, 1),
AC97_ENUM("Out3 Mux", wm9711_enum[2]),
AC97_ENUM("Out3 LR Mux", wm9711_enum[3]),
AC97_SINGLE("Out3 Volume", AC97_AUX, 0, 31, 1),

AC97_SINGLE("Beep to Headphone Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("Beep to Headphone Volume", AC97_PC_BEEP, 12, 7, 1),
AC97_SINGLE("Beep to Side Tone Switch", AC97_PC_BEEP, 11, 1, 1),
AC97_SINGLE("Beep to Side Tone Volume", AC97_PC_BEEP, 8, 7, 1),
AC97_SINGLE("Beep to Phone Switch", AC97_PC_BEEP, 7, 1, 1),
AC97_SINGLE("Beep to Phone Volume", AC97_PC_BEEP, 4, 7, 1),

AC97_SINGLE("Aux to Headphone Switch", AC97_CD, 15, 1, 1),
AC97_SINGLE("Aux to Headphone Volume", AC97_CD, 12, 7, 1),
AC97_SINGLE("Aux to Side Tone Switch", AC97_CD, 11, 1, 1),
AC97_SINGLE("Aux to Side Tone Volume", AC97_CD, 8, 7, 1),
AC97_SINGLE("Aux to Phone Switch", AC97_CD, 7, 1, 1),
AC97_SINGLE("Aux to Phone Volume", AC97_CD, 4, 7, 1),

AC97_SINGLE("Phone to Headphone Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("Phone to Master Switch", AC97_PHONE, 14, 1, 1),

AC97_SINGLE("Line to Headphone Switch", AC97_LINE, 15, 1, 1),
AC97_SINGLE("Line to Master Switch", AC97_LINE, 14, 1, 1),
AC97_SINGLE("Line to Phone Switch", AC97_LINE, 13, 1, 1),

AC97_SINGLE("PCM Playback to Headphone Switch", AC97_PCM, 15, 1, 1),
AC97_SINGLE("PCM Playback to Master Switch", AC97_PCM, 14, 1, 1),
AC97_SINGLE("PCM Playback to Phone Switch", AC97_PCM, 13, 1, 1),

AC97_SINGLE("Capture 20dB Boost Switch", AC97_REC_SEL, 14, 1, 0),
AC97_ENUM("Capture to Phone Mux", wm9711_enum[4]),
AC97_SINGLE("Capture to Phone 20dB Boost Switch", AC97_REC_SEL, 11, 1, 1),
AC97_ENUM("Capture Select", wm9711_enum[8]),

AC97_SINGLE("3D Upper Cut-off Switch", AC97_3D_CONTROL, 5, 1, 1),
AC97_SINGLE("3D Lower Cut-off Switch", AC97_3D_CONTROL, 4, 1, 1),

AC97_ENUM("Bass Control", wm9711_enum[5]),
AC97_SINGLE("Bass Cut-off Switch", AC97_MASTER_TONE, 12, 1, 1),
AC97_SINGLE("Tone Cut-off Switch", AC97_MASTER_TONE, 4, 1, 1),
AC97_SINGLE("Playback Attenuate (-6dB) Switch", AC97_MASTER_TONE, 6, 1, 0),

AC97_SINGLE("ADC Switch", AC97_REC_GAIN, 15, 1, 1),
AC97_ENUM("Capture Volume Steps", wm9711_enum[6]),
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 15, 1),
AC97_SINGLE("Capture ZC Switch", AC97_REC_GAIN, 7, 1, 0),

AC97_SINGLE("Mic 1 to Phone Switch", AC97_MIC, 14, 1, 1),
AC97_SINGLE("Mic 2 to Phone Switch", AC97_MIC, 13, 1, 1),
AC97_ENUM("Mic Select Source", wm9711_enum[7]),
AC97_SINGLE("Mic 1 Volume", AC97_MIC, 8, 32, 1),
AC97_SINGLE("Mic 20dB Boost Switch", AC97_MIC, 7, 1, 0),

AC97_SINGLE("Master ZC Switch", AC97_MASTER, 7, 1, 0),
AC97_SINGLE("Headphone ZC Switch", AC97_HEADPHONE, 7, 1, 0),
AC97_SINGLE("Mono ZC Switch", AC97_MASTER_MONO, 7, 1, 0),
};

static int patch_wolfson_wm9711_specific(ac97_t * ac97)
{
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm9711_snd_ac97_controls); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm9711_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97,  AC97_CODEC_CLASS_REV, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_PCI_SVID, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_VIDEO, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_AUX, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_PC_BEEP, 0x0808);
	snd_ac97_write_cache(ac97,  AC97_CD, 0x0000);
	return 0;
}

static struct snd_ac97_build_ops patch_wolfson_wm9711_ops = {
	.build_specific = patch_wolfson_wm9711_specific,
};

int patch_wolfson11(ac97_t * ac97)
{
	/* WM9711, WM9712 */
	ac97->build_ops = &patch_wolfson_wm9711_ops;

	ac97->flags |= AC97_HAS_NO_REC_GAIN | AC97_STEREO_MUTES | AC97_HAS_NO_MIC |
		AC97_HAS_NO_PC_BEEP | AC97_HAS_NO_VIDEO | AC97_HAS_NO_CD;
	
	return 0;
}

static const char* wm9713_mic_mixer[] = {"Stereo", "Mic 1", "Mic 2", "Mute"};
static const char* wm9713_rec_mux[] = {"Stereo", "Left", "Right", "Mute"};
static const char* wm9713_rec_src[] = 
	{"Mic 1", "Mic 2", "Line", "Mono In", "Headphone Mix", "Master Mix", 
	"Mono Mix", "Zh"};
static const char* wm9713_rec_gain[] = {"+1.5dB Steps", "+0.75dB Steps"};
static const char* wm9713_alc_select[] = {"None", "Left", "Right", "Stereo"};
static const char* wm9713_mono_pga[] = {"Vmid", "Zh", "Mono Mix", "Inv 1"};
static const char* wm9713_spk_pga[] = 
	{"Vmid", "Zh", "Headphone Mix", "Master Mix", "Inv", "NC", "NC", "NC"};
static const char* wm9713_hp_pga[] = {"Vmid", "Zh", "Headphone Mix", "NC"};
static const char* wm9713_out3_pga[] = {"Vmid", "Zh", "Inv 1", "NC"};
static const char* wm9713_out4_pga[] = {"Vmid", "Zh", "Inv 2", "NC"};
static const char* wm9713_dac_inv[] = 
	{"Off", "Mono Mix", "Master Mix", "Headphone Mix L", "Headphone Mix R", 
	"Headphone Mix Mono", "NC", "Vmid"};
static const char* wm9713_base[] = {"Linear Control", "Adaptive Boost"};
static const char* wm9713_ng_type[] = {"Constant Gain", "Mute"};

static const struct ac97_enum wm9713_enum[] = {
AC97_ENUM_SINGLE(AC97_LINE, 3, 4, wm9713_mic_mixer),
AC97_ENUM_SINGLE(AC97_VIDEO, 14, 4, wm9713_rec_mux),
AC97_ENUM_SINGLE(AC97_VIDEO, 9, 4, wm9713_rec_mux),
AC97_ENUM_DOUBLE(AC97_VIDEO, 3, 0, 8, wm9713_rec_src),
AC97_ENUM_DOUBLE(AC97_CD, 14, 6, 2, wm9713_rec_gain),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 14, 4, wm9713_alc_select),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 14, 4, wm9713_mono_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 11, 8, 8, wm9713_spk_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN, 6, 4, 4, wm9713_hp_pga),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 2, 4, wm9713_out3_pga),
AC97_ENUM_SINGLE(AC97_REC_GAIN, 0, 4, wm9713_out4_pga),
AC97_ENUM_DOUBLE(AC97_REC_GAIN_MIC, 13, 10, 8, wm9713_dac_inv),
AC97_ENUM_SINGLE(AC97_GENERAL_PURPOSE, 15, 2, wm9713_base),
AC97_ENUM_SINGLE(AC97_PCI_SVID, 5, 2, wm9713_ng_type),
};

static const snd_kcontrol_new_t wm13_snd_ac97_controls[] = {
AC97_DOUBLE("Line In Volume", AC97_PC_BEEP, 8, 0, 31, 1),
AC97_SINGLE("Line In to Headphone Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("Line In to Master Switch", AC97_PC_BEEP, 14, 1, 1),
AC97_SINGLE("Line In to Mono Switch", AC97_PC_BEEP, 13, 1, 1),

AC97_DOUBLE("PCM Playback Volume", AC97_PHONE, 8, 0, 31, 1),
AC97_SINGLE("PCM Playback to Headphone Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("PCM Playback to Master Switch", AC97_PHONE, 14, 1, 1),
AC97_SINGLE("PCM Playback to Mono Switch", AC97_PHONE, 13, 1, 1),

AC97_SINGLE("Mic 1 Volume", AC97_MIC, 8, 31, 1),
AC97_SINGLE("Mic 2 Volume", AC97_MIC, 0, 31, 1),
AC97_SINGLE("Mic 1 to Mono Switch", AC97_LINE, 7, 1, 1),
AC97_SINGLE("Mic 2 to Mono Switch", AC97_LINE, 6, 1, 1),
AC97_SINGLE("Mic Boost (+20dB) Switch", AC97_LINE, 5, 1, 0),
AC97_ENUM("Mic to Headphone Mux", wm9713_enum[0]),
AC97_SINGLE("Mic Headphone Mixer Volume", AC97_LINE, 0, 7, 1),

AC97_SINGLE("Capture Switch", AC97_CD, 15, 1, 1),
AC97_ENUM("Capture Volume Steps", wm9713_enum[4]),
AC97_DOUBLE("Capture Volume", AC97_CD, 8, 0, 15, 0),
AC97_SINGLE("Capture ZC Switch", AC97_CD, 7, 1, 0),

AC97_ENUM("Capture to Headphone Mux", wm9713_enum[1]),
AC97_SINGLE("Capture to Headphone Volume", AC97_VIDEO, 11, 7, 1),
AC97_ENUM("Capture to Mono Mux", wm9713_enum[2]),
AC97_SINGLE("Capture to Mono Boost (+20dB) Switch", AC97_VIDEO, 8, 1, 0),
AC97_SINGLE("Capture ADC Boost (+20dB) Switch", AC97_VIDEO, 6, 1, 0),
AC97_ENUM("Capture Select", wm9713_enum[3]),

AC97_SINGLE("ALC Target Volume", AC97_CODEC_CLASS_REV, 12, 15, 0),
AC97_SINGLE("ALC Hold Time", AC97_CODEC_CLASS_REV, 8, 15, 0),
AC97_SINGLE("ALC Decay Time ", AC97_CODEC_CLASS_REV, 4, 15, 0),
AC97_SINGLE("ALC Attack Time", AC97_CODEC_CLASS_REV, 0, 15, 0),
AC97_ENUM("ALC Function", wm9713_enum[5]),
AC97_SINGLE("ALC Max Volume", AC97_PCI_SVID, 11, 7, 0),
AC97_SINGLE("ALC ZC Timeout", AC97_PCI_SVID, 9, 3, 0),
AC97_SINGLE("ALC ZC Switch", AC97_PCI_SVID, 8, 1, 0),
AC97_SINGLE("ALC NG Switch", AC97_PCI_SVID, 7, 1, 0),
AC97_ENUM("ALC NG Type", wm9713_enum[13]),
AC97_SINGLE("ALC NG Threshold", AC97_PCI_SVID, 0, 31, 0),

AC97_DOUBLE("Master ZC Switch", AC97_MASTER, 14, 6, 1, 0),
AC97_DOUBLE("Headphone ZC Switch", AC97_HEADPHONE, 14, 6, 1, 0),
AC97_DOUBLE("Out3/4 ZC Switch", AC97_MASTER_MONO, 14, 6, 1, 0),
AC97_SINGLE("Master Right Switch", AC97_MASTER, 7, 1, 1),
AC97_SINGLE("Headphone Right Switch", AC97_HEADPHONE, 7, 1, 1),
AC97_SINGLE("Out3/4 Right Switch", AC97_MASTER_MONO, 7, 1, 1),

AC97_SINGLE("Mono In to Headphone Switch", AC97_MASTER_TONE, 15, 1, 1),
AC97_SINGLE("Mono In to Master Switch", AC97_MASTER_TONE, 14, 1, 1),
AC97_SINGLE("Mono In Volume", AC97_MASTER_TONE, 8, 31, 1),
AC97_SINGLE("Mono Switch", AC97_MASTER_TONE, 7, 1, 1),
AC97_SINGLE("Mono ZC Switch", AC97_MASTER_TONE, 6, 1, 0),
AC97_SINGLE("Mono Volume", AC97_MASTER_TONE, 0, 31, 1),

AC97_SINGLE("PC Beep to Headphone Switch", AC97_AUX, 15, 1, 1),
AC97_SINGLE("PC Beep to Headphone Volume", AC97_AUX, 12, 7, 1),
AC97_SINGLE("PC Beep to Master Switch", AC97_AUX, 11, 1, 1),
AC97_SINGLE("PC Beep to Master Volume", AC97_AUX, 8, 7, 1),
AC97_SINGLE("PC Beep to Mono Switch", AC97_AUX, 7, 1, 1),
AC97_SINGLE("PC Beep to Mono Volume", AC97_AUX, 4, 7, 1),

AC97_SINGLE("Voice to Headphone Switch", AC97_PCM, 15, 1, 1),
AC97_SINGLE("Voice to Headphone Volume", AC97_PCM, 12, 7, 1),
AC97_SINGLE("Voice to Master Switch", AC97_PCM, 11, 1, 1),
AC97_SINGLE("Voice to Master Volume", AC97_PCM, 8, 7, 1),
AC97_SINGLE("Voice to Mono Switch", AC97_PCM, 7, 1, 1),
AC97_SINGLE("Voice to Mono Volume", AC97_PCM, 4, 7, 1),

AC97_SINGLE("Aux to Headphone Switch", AC97_REC_SEL, 15, 1, 1),
AC97_SINGLE("Aux to Headphone Volume", AC97_REC_SEL, 12, 7, 1),
AC97_SINGLE("Aux to Master Switch", AC97_REC_SEL, 11, 1, 1),
AC97_SINGLE("Aux to Master Volume", AC97_REC_SEL, 8, 7, 1),
AC97_SINGLE("Aux to Mono Switch", AC97_REC_SEL, 7, 1, 1),
AC97_SINGLE("Aux to Mono Volume", AC97_REC_SEL, 4, 7, 1),

AC97_ENUM("Mono Input Mux", wm9713_enum[6]),
AC97_ENUM("Master Input Mux", wm9713_enum[7]),
AC97_ENUM("Headphone Input Mux", wm9713_enum[8]),
AC97_ENUM("Out 3 Input Mux", wm9713_enum[9]),
AC97_ENUM("Out 4 Input Mux", wm9713_enum[10]),

AC97_ENUM("Bass Control", wm9713_enum[12]),
AC97_SINGLE("Bass Cut-off Switch", AC97_GENERAL_PURPOSE, 12, 1, 1),
AC97_SINGLE("Tone Cut-off Switch", AC97_GENERAL_PURPOSE, 4, 1, 1),
AC97_SINGLE("Playback Attenuate (-6dB) Switch", AC97_GENERAL_PURPOSE, 6, 1, 0),
AC97_SINGLE("Bass Volume", AC97_GENERAL_PURPOSE, 8, 15, 1),
AC97_SINGLE("Tone Volume", AC97_GENERAL_PURPOSE, 0, 15, 1),
};

static const snd_kcontrol_new_t wm13_snd_ac97_controls_3d[] = {
AC97_ENUM("Inv Input Mux", wm9713_enum[11]),
AC97_SINGLE("3D Upper Cut-off Switch", AC97_REC_GAIN_MIC, 5, 1, 0),
AC97_SINGLE("3D Lower Cut-off Switch", AC97_REC_GAIN_MIC, 4, 1, 0),
AC97_SINGLE("3D Depth", AC97_REC_GAIN_MIC, 0, 15, 1),
};

static int patch_wolfson_wm9713_3d (ac97_t * ac97)
{
	int err, i;
    
	for (i = 0; i < ARRAY_SIZE(wm13_snd_ac97_controls_3d); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm13_snd_ac97_controls_3d[i], ac97))) < 0)
			return err;
	}
	return 0;
}

static int patch_wolfson_wm9713_specific(ac97_t * ac97)
{
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(wm13_snd_ac97_controls); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm13_snd_ac97_controls[i], ac97))) < 0)
			return err;
	}
	snd_ac97_write_cache(ac97, AC97_PC_BEEP, 0x0808);
	snd_ac97_write_cache(ac97, AC97_PHONE, 0x0808);
	snd_ac97_write_cache(ac97, AC97_MIC, 0x0808);
	snd_ac97_write_cache(ac97, AC97_LINE, 0x00da);
	snd_ac97_write_cache(ac97, AC97_CD, 0x0808);
	snd_ac97_write_cache(ac97, AC97_VIDEO, 0xd612);
	snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x1ba0);
	return 0;
}

#ifdef CONFIG_PM
static void patch_wolfson_wm9713_suspend (ac97_t * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xfeff);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0xffff);
}

static void patch_wolfson_wm9713_resume (ac97_t * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xda00);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0x3810);
	snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0x0);
}
#endif

static struct snd_ac97_build_ops patch_wolfson_wm9713_ops = {
	.build_specific = patch_wolfson_wm9713_specific,
	.build_3d = patch_wolfson_wm9713_3d,
#ifdef CONFIG_PM	
	.suspend = patch_wolfson_wm9713_suspend,
	.resume = patch_wolfson_wm9713_resume
#endif
};

int patch_wolfson13(ac97_t * ac97)
{
	/* WM9713, WM9714 */
	ac97->build_ops = &patch_wolfson_wm9713_ops;

	ac97->flags |= AC97_HAS_NO_REC_GAIN | AC97_STEREO_MUTES | AC97_HAS_NO_PHONE |
		AC97_HAS_NO_PC_BEEP | AC97_HAS_NO_VIDEO | AC97_HAS_NO_CD | AC97_HAS_NO_TONE |
		AC97_HAS_NO_STD_PCM;
    	ac97->scaps &= ~AC97_SCAP_MODEM;

	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xda00);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0x3810);
	snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0x0);

	return 0;
}

/*
 * Tritech codec
 */
int patch_tritech_tr28028(ac97_t * ac97)
{
	snd_ac97_write_cache(ac97, 0x26, 0x0300);
	snd_ac97_write_cache(ac97, 0x26, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SPDIF, 0x0000);
	return 0;
}

/*
 * Sigmatel STAC97xx codecs
 */
static int patch_sigmatel_stac9700_3d(ac97_t * ac97)
{
	snd_kcontrol_t *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 2, 3, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9708_3d(ac97_t * ac97)
{
	snd_kcontrol_t *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 0, 3, 0);
	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Rear Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 2, 3, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	return 0;
}

static const snd_kcontrol_new_t snd_ac97_sigmatel_4speaker =
AC97_SINGLE("Sigmatel 4-Speaker Stereo Playback Switch", AC97_SIGMATEL_DAC2INVERT, 2, 1, 0);

static const snd_kcontrol_new_t snd_ac97_sigmatel_phaseinvert =
AC97_SINGLE("Sigmatel Surround Phase Inversion Playback Switch", AC97_SIGMATEL_DAC2INVERT, 3, 1, 0);

static const snd_kcontrol_new_t snd_ac97_sigmatel_controls[] = {
AC97_SINGLE("Sigmatel DAC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 1, 1, 0),
AC97_SINGLE("Sigmatel ADC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 0, 1, 0)
};

static int patch_sigmatel_stac97xx_specific(ac97_t * ac97)
{
	int err;

	snd_ac97_write_cache(ac97, AC97_SIGMATEL_ANALOG, snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) & ~0x0003);
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 1))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_controls[0], 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 0))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_controls[1], 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_DAC2INVERT, 2))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_4speaker, 1)) < 0)
			return err;
	if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_DAC2INVERT, 3))
		if ((err = patch_build_controls(ac97, &snd_ac97_sigmatel_phaseinvert, 1)) < 0)
			return err;
	return 0;
}

static struct snd_ac97_build_ops patch_sigmatel_stac9700_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac97xx_specific
};

int patch_sigmatel_stac9700(ac97_t * ac97)
{
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	return 0;
}

static int snd_ac97_stac9708_put_bias(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int err;

	down(&ac97->page_mutex);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	err = snd_ac97_update_bits(ac97, AC97_SIGMATEL_BIAS2, 0x0010,
				   (ucontrol->value.integer.value[0] & 1) << 4);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0);
	up(&ac97->page_mutex);
	return err;
}

static const snd_kcontrol_new_t snd_ac97_stac9708_bias_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sigmatel Output Bias Switch",
	.info = snd_ac97_info_volsw,
	.get = snd_ac97_get_volsw,
	.put = snd_ac97_stac9708_put_bias,
	.private_value = AC97_SINGLE_VALUE(AC97_SIGMATEL_BIAS2, 4, 1, 0),
};

static int patch_sigmatel_stac9708_specific(ac97_t *ac97)
{
	int err;

	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Sigmatel Surround Playback");
	if ((err = patch_build_controls(ac97, &snd_ac97_stac9708_bias_control, 1)) < 0)
		return err;
	return patch_sigmatel_stac97xx_specific(ac97);
}

static struct snd_ac97_build_ops patch_sigmatel_stac9708_ops = {
	.build_3d	= patch_sigmatel_stac9708_3d,
	.build_specific	= patch_sigmatel_stac9708_specific
};

int patch_sigmatel_stac9708(ac97_t * ac97)
{
	unsigned int codec72, codec6c;

	ac97->build_ops = &patch_sigmatel_stac9708_ops;
	ac97->caps |= 0x10;	/* HP (sigmatel surround) support */

	codec72 = snd_ac97_read(ac97, AC97_SIGMATEL_BIAS2) & 0x8000;
	codec6c = snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG);

	if ((codec72==0) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0007);
	} else if ((codec72==0x8000) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1001);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_DAC2INVERT, 0x0008);
	} else if ((codec72==0x8000) && (codec6c==0x0080)) {
		/* nothing */
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9721(ac97_t * ac97)
{
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	if (snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) == 0) {
		// patch for SigmaTel
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x4000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9744(ac97_t * ac97)
{
	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

int patch_sigmatel_stac9756(ac97_t * ac97)
{
	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int snd_ac97_stac9758_output_jack_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[5] = { "Input/Disabled", "Front Output",
		"Rear Output", "Center/LFE Output", "Mixer Output" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item > 4)
		uinfo->value.enumerated.item = 4;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_output_jack_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t* ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_OUTSEL] >> shift;
	if (!(val & 4))
		ucontrol->value.enumerated.item[0] = 0;
	else
		ucontrol->value.enumerated.item[0] = 1 + (val & 3);
	return 0;
}

static int snd_ac97_stac9758_output_jack_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 4)
		return -EINVAL;
	if (ucontrol->value.enumerated.item[0] == 0)
		val = 0;
	else
		val = 4 | (ucontrol->value.enumerated.item[0] - 1);
	return ac97_update_bits_page(ac97, AC97_SIGMATEL_OUTSEL,
				     7 << shift, val << shift, 0);
}

static int snd_ac97_stac9758_input_jack_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[7] = { "Mic2 Jack", "Mic1 Jack", "Line In Jack",
		"Front Jack", "Rear Jack", "Center/LFE Jack", "Mute" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 7;
	if (uinfo->value.enumerated.item > 6)
		uinfo->value.enumerated.item = 6;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_input_jack_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t* ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_INSEL];
	ucontrol->value.enumerated.item[0] = (val >> shift) & 7;
	return 0;
}

static int snd_ac97_stac9758_input_jack_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;

	return ac97_update_bits_page(ac97, AC97_SIGMATEL_INSEL, 7 << shift,
				     ucontrol->value.enumerated.item[0] << shift, 0);
}

static int snd_ac97_stac9758_phonesel_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[3] = { "None", "Front Jack", "Rear Jack" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_stac9758_phonesel_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t* ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->regs[AC97_SIGMATEL_IOMISC] & 3;
	return 0;
}

static int snd_ac97_stac9758_phonesel_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	return ac97_update_bits_page(ac97, AC97_SIGMATEL_IOMISC, 3,
				     ucontrol->value.enumerated.item[0], 0);
}

#define STAC9758_OUTPUT_JACK(xname, shift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_ac97_stac9758_output_jack_info, \
	.get = snd_ac97_stac9758_output_jack_get, \
	.put = snd_ac97_stac9758_output_jack_put, \
	.private_value = shift }
#define STAC9758_INPUT_JACK(xname, shift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_ac97_stac9758_input_jack_info, \
	.get = snd_ac97_stac9758_input_jack_get, \
	.put = snd_ac97_stac9758_input_jack_put, \
	.private_value = shift }
static const snd_kcontrol_new_t snd_ac97_sigmatel_stac9758_controls[] = {
	STAC9758_OUTPUT_JACK("Mic1 Jack", 1),
	STAC9758_OUTPUT_JACK("LineIn Jack", 4),
	STAC9758_OUTPUT_JACK("Front Jack", 7),
	STAC9758_OUTPUT_JACK("Rear Jack", 10),
	STAC9758_OUTPUT_JACK("Center/LFE Jack", 13),
	STAC9758_INPUT_JACK("Mic Input Source", 0),
	STAC9758_INPUT_JACK("Line Input Source", 8),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Amp",
		.info = snd_ac97_stac9758_phonesel_info,
		.get = snd_ac97_stac9758_phonesel_get,
		.put = snd_ac97_stac9758_phonesel_put
	},
	AC97_SINGLE("Exchange Center/LFE", AC97_SIGMATEL_IOMISC, 4, 1, 0),
	AC97_SINGLE("Headphone +3dB Boost", AC97_SIGMATEL_IOMISC, 8, 1, 0)
};

static int patch_sigmatel_stac9758_specific(ac97_t *ac97)
{
	int err;

	err = patch_sigmatel_stac97xx_specific(ac97);
	if (err < 0)
		return err;
	err = patch_build_controls(ac97, snd_ac97_sigmatel_stac9758_controls,
				   ARRAY_SIZE(snd_ac97_sigmatel_stac9758_controls));
	if (err < 0)
		return err;
	/* DAC-A direct */
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Front Playback");
	/* DAC-A to Mix = PCM */
	/* DAC-B direct = Surround */
	/* DAC-B to Mix */
	snd_ac97_rename_vol_ctl(ac97, "Video Playback", "Surround Mix Playback");
	/* DAC-C direct = Center/LFE */

	return 0;
}

static struct snd_ac97_build_ops patch_sigmatel_stac9758_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac9758_specific
};

int patch_sigmatel_stac9758(ac97_t * ac97)
{
	static unsigned short regs[4] = {
		AC97_SIGMATEL_OUTSEL,
		AC97_SIGMATEL_IOMISC,
		AC97_SIGMATEL_INSEL,
		AC97_SIGMATEL_VARIOUS
	};
	static unsigned short def_regs[4] = {
		/* OUTSEL */ 0xd794, /* CL:CL, SR:SR, LO:MX, LI:DS, MI:DS */
		/* IOMISC */ 0x2001,
		/* INSEL */ 0x0201, /* LI:LI, MI:M1 */
		/* VARIOUS */ 0x0040
	};
	static unsigned short m675_regs[4] = {
		/* OUTSEL */ 0xfc70, /* CL:MX, SR:MX, LO:DS, LI:MX, MI:DS */
		/* IOMISC */ 0x2102, /* HP amp on */
		/* INSEL */ 0x0203, /* LI:LI, MI:FR */
		/* VARIOUS */ 0x0041 /* stereo mic */
	};
	unsigned short *pregs = def_regs;
	int i;

	/* Gateway M675 notebook */
	if (ac97->pci && 
	    ac97->subsystem_vendor == 0x107b &&
	    ac97->subsystem_device == 0x0601)
	    	pregs = m675_regs;

	// patch for SigmaTel
	ac97->build_ops = &patch_sigmatel_stac9758_ops;
	/* FIXME: assume only page 0 for writing cache */
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, AC97_PAGE_VENDOR);
	for (i = 0; i < 4; i++)
		snd_ac97_write_cache(ac97, regs[i], pregs[i]);

	ac97->flags |= AC97_STEREO_MUTES;
	return 0;
}

/*
 * Cirrus Logic CS42xx codecs
 */
static const snd_kcontrol_new_t snd_ac97_cirrus_controls_spdif[2] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CSR_SPDIF, 15, 1, 0),
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA", AC97_CSR_ACMODE, 0, 3, 0)
};

static int patch_cirrus_build_spdif(ac97_t * ac97)
{
	int err;

	/* con mask, pro mask, default */
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3)) < 0)
		return err;
	/* switch, spsa */
	if ((err = patch_build_controls(ac97, &snd_ac97_cirrus_controls_spdif[0], 1)) < 0)
		return err;
	switch (ac97->id & AC97_ID_CS_MASK) {
	case AC97_ID_CS4205:
		if ((err = patch_build_controls(ac97, &snd_ac97_cirrus_controls_spdif[1], 1)) < 0)
			return err;
		break;
	}
	/* set default PCM S/PDIF params */
	/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
	snd_ac97_write_cache(ac97, AC97_CSR_SPDIF, 0x0a20);
	return 0;
}

static struct snd_ac97_build_ops patch_cirrus_ops = {
	.build_spdif = patch_cirrus_build_spdif
};

int patch_cirrus_spdif(ac97_t * ac97)
{
	/* Basically, the cs4201/cs4205/cs4297a has non-standard sp/dif registers.
	   WHY CAN'T ANYONE FOLLOW THE BLOODY SPEC?  *sigh*
	   - sp/dif EA ID is not set, but sp/dif is always present.
	   - enable/disable is spdif register bit 15.
	   - sp/dif control register is 0x68.  differs from AC97:
	   - valid is bit 14 (vs 15)
	   - no DRS
	   - only 44.1/48k [00 = 48, 01=44,1] (AC97 is 00=44.1, 10=48)
	   - sp/dif ssource select is in 0x5e bits 0,1.
	*/

	ac97->build_ops = &patch_cirrus_ops;
	ac97->flags |= AC97_CS_SPDIF; 
	ac97->rates[AC97_RATES_SPDIF] &= ~SNDRV_PCM_RATE_32000;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	snd_ac97_write_cache(ac97, AC97_CSR_ACMODE, 0x0080);
	return 0;
}

int patch_cirrus_cs4299(ac97_t * ac97)
{
	/* force the detection of PC Beep */
	ac97->flags |= AC97_HAS_PC_BEEP;
	
	return patch_cirrus_spdif(ac97);
}

/*
 * Conexant codecs
 */
static const snd_kcontrol_new_t snd_ac97_conexant_controls_spdif[1] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CXR_AUDIO_MISC, 3, 1, 0),
};

static int patch_conexant_build_spdif(ac97_t * ac97)
{
	int err;

	/* con mask, pro mask, default */
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3)) < 0)
		return err;
	/* switch */
	if ((err = patch_build_controls(ac97, &snd_ac97_conexant_controls_spdif[0], 1)) < 0)
		return err;
	/* set default PCM S/PDIF params */
	/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
	snd_ac97_write_cache(ac97, AC97_CXR_AUDIO_MISC,
			     snd_ac97_read(ac97, AC97_CXR_AUDIO_MISC) & ~(AC97_CXR_SPDIFEN|AC97_CXR_COPYRGT|AC97_CXR_SPDIF_MASK));
	return 0;
}

static struct snd_ac97_build_ops patch_conexant_ops = {
	.build_spdif = patch_conexant_build_spdif
};

int patch_conexant(ac97_t * ac97)
{
	ac97->build_ops = &patch_conexant_ops;
	ac97->flags |= AC97_CX_SPDIF;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */
	return 0;
}

/*
 * Analog Device AD18xx, AD19xx codecs
 */
#ifdef CONFIG_PM
static void ad18xx_resume(ac97_t *ac97)
{
	static unsigned short setup_regs[] = {
		AC97_AD_MISC, AC97_AD_SERIAL_CFG, AC97_AD_JACK_SPDIF,
	};
	int i, codec;

	for (i = 0; i < (int)ARRAY_SIZE(setup_regs); i++) {
		unsigned short reg = setup_regs[i];
		if (test_bit(reg, ac97->reg_accessed)) {
			snd_ac97_write(ac97, reg, ac97->regs[reg]);
			snd_ac97_read(ac97, reg);
		}
	}

	if (! (ac97->flags & AC97_AD_MULTI))
		/* normal restore */
		snd_ac97_restore_status(ac97);
	else {
		/* restore the AD18xx codec configurations */
		for (codec = 0; codec < 3; codec++) {
			if (! ac97->spec.ad18xx.id[codec])
				continue;
			/* select single codec */
			snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
					     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
			ac97->bus->ops->write(ac97, AC97_AD_CODEC_CFG, ac97->spec.ad18xx.codec_cfg[codec]);
		}
		/* select all codecs */
		snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);

		/* restore status */
		for (i = 2; i < 0x7c ; i += 2) {
			if (i == AC97_POWERDOWN || i == AC97_EXTENDED_ID)
				continue;
			if (test_bit(i, ac97->reg_accessed)) {
				/* handle multi codecs for AD18xx */
				if (i == AC97_PCM) {
					for (codec = 0; codec < 3; codec++) {
						if (! ac97->spec.ad18xx.id[codec])
							continue;
						/* select single codec */
						snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
								     ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
						/* update PCM bits */
						ac97->bus->ops->write(ac97, AC97_PCM, ac97->spec.ad18xx.pcmreg[codec]);
					}
					/* select all codecs */
					snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
					continue;
				} else if (i == AC97_AD_TEST ||
					   i == AC97_AD_CODEC_CFG ||
					   i == AC97_AD_SERIAL_CFG)
					continue; /* ignore */
			}
			snd_ac97_write(ac97, i, ac97->regs[i]);
			snd_ac97_read(ac97, i);
		}
	}

	snd_ac97_restore_iec958(ac97);
}
#endif

int patch_ad1819(ac97_t * ac97)
{
	unsigned short scfg;

	// patch for Analog Devices
	scfg = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, scfg | 0x7000); /* select all codecs */
	return 0;
}

static unsigned short patch_ad1881_unchained(ac97_t * ac97, int idx, unsigned short mask)
{
	unsigned short val;

	// test for unchained codec
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, mask);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);	/* ID0C, ID1C, SDIE = off */
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	ac97->spec.ad18xx.unchained[idx] = mask;
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = 0x0000;
	return mask;
}

static int patch_ad1881_chained1(ac97_t * ac97, int idx, unsigned short codec_bits)
{
	static int cfg_bits[3] = { 1<<12, 1<<14, 1<<13 };
	unsigned short val;
	
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, cfg_bits[idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0004);	// SDIE
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	if (codec_bits)
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, codec_bits);
	ac97->spec.ad18xx.chained[idx] = cfg_bits[idx];
	ac97->spec.ad18xx.id[idx] = val;
	ac97->spec.ad18xx.codec_cfg[idx] = codec_bits ? codec_bits : 0x0004;
	return 1;
}

static void patch_ad1881_chained(ac97_t * ac97, int unchained_idx, int cidx1, int cidx2)
{
	// already detected?
	if (ac97->spec.ad18xx.unchained[cidx1] || ac97->spec.ad18xx.chained[cidx1])
		cidx1 = -1;
	if (ac97->spec.ad18xx.unchained[cidx2] || ac97->spec.ad18xx.chained[cidx2])
		cidx2 = -1;
	if (cidx1 < 0 && cidx2 < 0)
		return;
	// test for chained codecs
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
			     ac97->spec.ad18xx.unchained[unchained_idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0002);		// ID1C
	ac97->spec.ad18xx.codec_cfg[unchained_idx] = 0x0002;
	if (cidx1 >= 0) {
		if (patch_ad1881_chained1(ac97, cidx1, 0x0006))		// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx2, 0);
		else if (patch_ad1881_chained1(ac97, cidx2, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx1, 0);
	} else if (cidx2 >= 0) {
		patch_ad1881_chained1(ac97, cidx2, 0);
	}
}

static struct snd_ac97_build_ops patch_ad1881_build_ops = {
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

int patch_ad1881(ac97_t * ac97)
{
	static const char cfg_idxs[3][2] = {
		{2, 1},
		{0, 2},
		{0, 1}
	};
	
	// patch for Analog Devices
	unsigned short codecs[3];
	unsigned short val;
	int idx, num;

	val = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, val);
	codecs[0] = patch_ad1881_unchained(ac97, 0, (1<<12));
	codecs[1] = patch_ad1881_unchained(ac97, 1, (1<<14));
	codecs[2] = patch_ad1881_unchained(ac97, 2, (1<<13));

	snd_runtime_check(codecs[0] | codecs[1] | codecs[2], goto __end);

	for (idx = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.unchained[idx])
			patch_ad1881_chained(ac97, idx, cfg_idxs[idx][0], cfg_idxs[idx][1]);

	if (ac97->spec.ad18xx.id[1]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->spec.ad18xx.id[2]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}

      __end:
	/* select all codecs */
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
	/* check if only one codec is present */
	for (idx = num = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.id[idx])
			num++;
	if (num == 1) {
		/* ok, deselect all ID bits */
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);
		ac97->spec.ad18xx.codec_cfg[0] = 
			ac97->spec.ad18xx.codec_cfg[1] = 
			ac97->spec.ad18xx.codec_cfg[2] = 0x0000;
	}
	/* required for AD1886/AD1885 combination */
	ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ac97->spec.ad18xx.id[0]) {
		ac97->id &= 0xffff0000;
		ac97->id |= ac97->spec.ad18xx.id[0];
	}
	ac97->build_ops = &patch_ad1881_build_ops;
	return 0;
}

static const snd_kcontrol_new_t snd_ac97_controls_ad1885[] = {
	AC97_SINGLE("Digital Mono Direct", AC97_AD_MISC, 11, 1, 0),
	/* AC97_SINGLE("Digital Audio Mode", AC97_AD_MISC, 12, 1, 0), */ /* seems problematic */
	AC97_SINGLE("Low Power Mixer", AC97_AD_MISC, 14, 1, 0),
	AC97_SINGLE("Zero Fill DAC", AC97_AD_MISC, 15, 1, 0),
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 9, 1, 1), /* inverted */
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 8, 1, 1), /* inverted */
};

static int patch_ad1885_specific(ac97_t * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_ad1885, ARRAY_SIZE(snd_ac97_controls_ad1885))) < 0)
		return err;
	return 0;
}

static struct snd_ac97_build_ops patch_ad1885_build_ops = {
	.build_specific = &patch_ad1885_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

int patch_ad1885(ac97_t * ac97)
{
	patch_ad1881(ac97);
	/* This is required to deal with the Intel D815EEAL2 */
	/* i.e. Line out is actually headphone out from codec */

	/* set default */
	snd_ac97_write_cache(ac97, AC97_AD_MISC, 0x0404);

	ac97->build_ops = &patch_ad1885_build_ops;
	return 0;
}

int patch_ad1886(ac97_t * ac97)
{
	patch_ad1881(ac97);
	/* Presario700 workaround */
	/* for Jack Sense/SPDIF Register misetting causing */
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, 0x0010);
	return 0;
}

/* MISC bits */
#define AC97_AD198X_MBC		0x0003	/* mic boost */
#define AC97_AD198X_MBC_20	0x0000	/* +20dB */
#define AC97_AD198X_MBC_10	0x0001	/* +10dB */
#define AC97_AD198X_MBC_30	0x0002	/* +30dB */
#define AC97_AD198X_VREFD	0x0004	/* VREF high-Z */
#define AC97_AD198X_VREFH	0x0008	/* 2.25V, 3.7V */
#define AC97_AD198X_VREF_0	0x000c	/* 0V */
#define AC97_AD198X_SRU		0x0010	/* sample rate unlock */
#define AC97_AD198X_LOSEL	0x0020	/* LINE_OUT amplifiers input select */
#define AC97_AD198X_2MIC	0x0040	/* 2-channel mic select */
#define AC97_AD198X_SPRD	0x0080	/* SPREAD enable */
#define AC97_AD198X_DMIX0	0x0100	/* downmix mode: 0 = 6-to-4, 1 = 6-to-2 downmix */
#define AC97_AD198X_DMIX1	0x0200	/* downmix mode: 1 = enabled */
#define AC97_AD198X_HPSEL	0x0400	/* headphone amplifier input select */
#define AC97_AD198X_CLDIS	0x0800	/* center/lfe disable */
#define AC97_AD198X_LODIS	0x1000	/* LINE_OUT disable */
#define AC97_AD198X_MSPLT	0x2000	/* mute split */
#define AC97_AD198X_AC97NC	0x4000	/* AC97 no compatible mode */
#define AC97_AD198X_DACZ	0x8000	/* DAC zero-fill mode */


static int snd_ac97_ad198x_spdif_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[2] = { "AC-Link", "A/D Converter" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ad198x_spdif_source_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_SERIAL_CFG];
	ucontrol->value.enumerated.item[0] = (val >> 2) & 1;
	return 0;
}

static int snd_ac97_ad198x_spdif_source_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 2;
	return snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x0004, val);
}

static const snd_kcontrol_new_t snd_ac97_ad198x_spdif_source = {
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
	.info	= snd_ac97_ad198x_spdif_source_info,
	.get	= snd_ac97_ad198x_spdif_source_get,
	.put	= snd_ac97_ad198x_spdif_source_put,
};

static int patch_ad198x_post_spdif(ac97_t * ac97)
{
 	return patch_build_controls(ac97, &snd_ac97_ad198x_spdif_source, 1);
}

static const snd_kcontrol_new_t snd_ac97_ad1981x_jack_sense[] = {
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 11, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0),
};

static int patch_ad1981a_specific(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_ad1981x_jack_sense,
				    ARRAY_SIZE(snd_ac97_ad1981x_jack_sense));
}

static struct snd_ac97_build_ops patch_ad1981a_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1981a_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static void check_ad1981_hp_jack_sense(ac97_t *ac97)
{
	u32 subid = ((u32)ac97->subsystem_vendor << 16) | ac97->subsystem_device;
	switch (subid) {
	case 0x103c0890: /* HP nc6000 */
	case 0x103c099c: /* HP nx6110 */
	case 0x103c006d: /* HP nx9105 */
	case 0x17340088: /* FSC Scenic-W */
		/* enable headphone jack sense */
		snd_ac97_update_bits(ac97, AC97_AD_JACK_SPDIF, 1<<11, 1<<11);
		break;
	}
}

int patch_ad1981a(ac97_t *ac97)
{
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1981a_build_ops;
	snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD198X_MSPLT, AC97_AD198X_MSPLT);
	ac97->flags |= AC97_STEREO_MUTES;
	check_ad1981_hp_jack_sense(ac97);
	return 0;
}

static const snd_kcontrol_new_t snd_ac97_ad198x_2cmic =
AC97_SINGLE("Stereo Mic", AC97_AD_MISC, 6, 1, 0);

static int patch_ad1981b_specific(ac97_t *ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1)) < 0)
		return err;
	return patch_build_controls(ac97, snd_ac97_ad1981x_jack_sense,
				    ARRAY_SIZE(snd_ac97_ad1981x_jack_sense));
}

static struct snd_ac97_build_ops patch_ad1981b_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1981b_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

int patch_ad1981b(ac97_t *ac97)
{
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1981b_build_ops;
	snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD198X_MSPLT, AC97_AD198X_MSPLT);
	ac97->flags |= AC97_STEREO_MUTES;
	check_ad1981_hp_jack_sense(ac97);
	return 0;
}

static int snd_ac97_ad1888_lohpsel_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ac97_ad1888_lohpsel_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t* ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC];
	ucontrol->value.integer.value[0] = !(val & AC97_AD198X_LOSEL);
	return 0;
}

static int snd_ac97_ad1888_lohpsel_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = !ucontrol->value.integer.value[0]
		? (AC97_AD198X_LOSEL | AC97_AD198X_HPSEL) : 0;
	return snd_ac97_update_bits(ac97, AC97_AD_MISC,
				    AC97_AD198X_LOSEL | AC97_AD198X_HPSEL, val);
}

static int snd_ac97_ad1888_downmix_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[3] = {"Off", "6 -> 4", "6 -> 2"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ad1888_downmix_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t* ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC];
	if (!(val & AC97_AD198X_DMIX1))
		ucontrol->value.enumerated.item[0] = 0;
	else
		ucontrol->value.enumerated.item[0] = 1 + ((val >> 8) & 1);
	return 0;
}

static int snd_ac97_ad1888_downmix_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	if (ucontrol->value.enumerated.item[0] == 0)
		val = 0;
	else
		val = AC97_AD198X_DMIX1 |
			((ucontrol->value.enumerated.item[0] - 1) << 8);
	return snd_ac97_update_bits(ac97, AC97_AD_MISC,
				    AC97_AD198X_DMIX0 | AC97_AD198X_DMIX1, val);
}

static void ad1888_update_jacks(ac97_t *ac97)
{
	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_AD_MISC, 1 << 12,
			     is_shared_linein(ac97) ? 0 : 1 << 12);
	/* shared Mic */
	snd_ac97_update_bits(ac97, AC97_AD_MISC, 1 << 11,
			     is_shared_micin(ac97) ? 0 : 1 << 11);
}

static const snd_kcontrol_new_t snd_ac97_ad1888_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Exchange Front/Surround",
		.info = snd_ac97_ad1888_lohpsel_info,
		.get = snd_ac97_ad1888_lohpsel_get,
		.put = snd_ac97_ad1888_lohpsel_put
	},
	AC97_SINGLE("Spread Front to Surround and Center/LFE", AC97_AD_MISC, 7, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Downmix",
		.info = snd_ac97_ad1888_downmix_info,
		.get = snd_ac97_ad1888_downmix_get,
		.put = snd_ac97_ad1888_downmix_put
	},
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,

	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 10, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0),
};

static int patch_ad1888_specific(ac97_t *ac97)
{
	/* rename 0x04 as "Master" and 0x02 as "Master Surround" */
	snd_ac97_rename_vol_ctl(ac97, "Master Playback", "Master Surround Playback");
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Master Playback");
	return patch_build_controls(ac97, snd_ac97_ad1888_controls, ARRAY_SIZE(snd_ac97_ad1888_controls));
}

static struct snd_ac97_build_ops patch_ad1888_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1888_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1888_update_jacks,
};

int patch_ad1888(ac97_t * ac97)
{
	unsigned short misc;
	
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1888_build_ops;
	/* Switch FRONT/SURROUND LINE-OUT/HP-OUT default connection */
	/* it seems that most vendors connect line-out connector to headphone out of AC'97 */
	/* AD-compatible mode */
	/* Stereo mutes enabled */
	misc = snd_ac97_read(ac97, AC97_AD_MISC);
	snd_ac97_write_cache(ac97, AC97_AD_MISC, misc |
			     AC97_AD198X_LOSEL |
			     AC97_AD198X_HPSEL |
			     AC97_AD198X_MSPLT |
			     AC97_AD198X_AC97NC);
	ac97->flags |= AC97_STEREO_MUTES;
	return 0;
}

static int patch_ad1980_specific(ac97_t *ac97)
{
	int err;

	if ((err = patch_ad1888_specific(ac97)) < 0)
		return err;
	return patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1);
}

static struct snd_ac97_build_ops patch_ad1980_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1980_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1888_update_jacks,
};

int patch_ad1980(ac97_t * ac97)
{
	patch_ad1888(ac97);
	ac97->build_ops = &patch_ad1980_build_ops;
	return 0;
}

static const snd_kcontrol_new_t snd_ac97_ad1985_controls[] = {
	AC97_SINGLE("Exchange Center/LFE", AC97_AD_SERIAL_CFG, 3, 1, 0)
};

static void ad1985_update_jacks(ac97_t *ac97)
{
	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_AD_MISC, 1 << 12,
			     is_shared_linein(ac97) ? 0 : 1 << 12);
	/* shared Mic */
	snd_ac97_update_bits(ac97, AC97_AD_MISC, 1 << 11,
			     is_shared_micin(ac97) ? 0 : 1 << 11);
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 1 << 9,
			     is_shared_micin(ac97) ? 0 : 1 << 9);
}

static int patch_ad1985_specific(ac97_t *ac97)
{
	int err;

	if ((err = patch_ad1980_specific(ac97)) < 0)
		return err;
	return patch_build_controls(ac97, snd_ac97_ad1985_controls, ARRAY_SIZE(snd_ac97_ad1985_controls));
}

static struct snd_ac97_build_ops patch_ad1985_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1985_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1985_update_jacks,
};

int patch_ad1985(ac97_t * ac97)
{
	unsigned short misc;
	
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1985_build_ops;
	misc = snd_ac97_read(ac97, AC97_AD_MISC);
	/* switch front/surround line-out/hp-out */
	/* center/LFE, mic in 3.75V mode */
	/* AD-compatible mode */
	/* Stereo mutes enabled */
	/* in accordance with ADI driver: misc | 0x5c28 */
	snd_ac97_write_cache(ac97, AC97_AD_MISC, misc |
			     AC97_AD198X_VREFH |
			     AC97_AD198X_LOSEL |
			     AC97_AD198X_HPSEL |
			     AC97_AD198X_CLDIS |
			     AC97_AD198X_LODIS |
			     AC97_AD198X_MSPLT |
			     AC97_AD198X_AC97NC);
	ac97->flags |= AC97_STEREO_MUTES;
	/* on AD1985 rev. 3, AC'97 revision bits are zero */
	ac97->ext_id = (ac97->ext_id & ~AC97_EI_REV_MASK) | AC97_EI_REV_23;
	return 0;
}

/*
 * realtek ALC65x/850 codecs
 */
static void alc650_update_jacks(ac97_t *ac97)
{
	int shared;
	
	/* shared Line-In */
	shared = is_shared_linein(ac97);
	snd_ac97_update_bits(ac97, AC97_ALC650_MULTICH, 1 << 9,
			     shared ? (1 << 9) : 0);
	/* update shared Mic */
	shared = is_shared_micin(ac97);
	/* disable/enable vref */
	snd_ac97_update_bits(ac97, AC97_ALC650_CLOCK, 1 << 12,
			     shared ? (1 << 12) : 0);
	/* turn on/off center-on-mic */
	snd_ac97_update_bits(ac97, AC97_ALC650_MULTICH, 1 << 10,
			     shared ? (1 << 10) : 0);
	/* GPIO0 high for mic */
	snd_ac97_update_bits(ac97, AC97_ALC650_GPIO_STATUS, 0x100,
			     shared ? 0 : 0x100);
}

static const snd_kcontrol_new_t snd_ac97_controls_alc650[] = {
	AC97_SINGLE("Duplicate Front", AC97_ALC650_MULTICH, 0, 1, 0),
	AC97_SINGLE("Surround Down Mix", AC97_ALC650_MULTICH, 1, 1, 0),
	AC97_SINGLE("Center/LFE Down Mix", AC97_ALC650_MULTICH, 2, 1, 0),
	AC97_SINGLE("Exchange Center/LFE", AC97_ALC650_MULTICH, 3, 1, 0),
	/* 4: Analog Input To Surround */
	/* 5: Analog Input To Center/LFE */
	/* 6: Independent Master Volume Right */
	/* 7: Independent Master Volume Left */
	/* 8: reserved */
	/* 9: Line-In/Surround share */
	/* 10: Mic/CLFE share */
	/* 11-13: in IEC958 controls */
	AC97_SINGLE("Swap Surround Slot", AC97_ALC650_MULTICH, 14, 1, 0),
#if 0 /* always set in patch_alc650 */
	AC97_SINGLE("IEC958 Input Clock Enable", AC97_ALC650_CLOCK, 0, 1, 0),
	AC97_SINGLE("IEC958 Input Pin Enable", AC97_ALC650_CLOCK, 1, 1, 0),
	AC97_SINGLE("Surround DAC Switch", AC97_ALC650_SURR_DAC_VOL, 15, 1, 1),
	AC97_DOUBLE("Surround DAC Volume", AC97_ALC650_SURR_DAC_VOL, 8, 0, 31, 1),
	AC97_SINGLE("Center/LFE DAC Switch", AC97_ALC650_LFE_DAC_VOL, 15, 1, 1),
	AC97_DOUBLE("Center/LFE DAC Volume", AC97_ALC650_LFE_DAC_VOL, 8, 0, 31, 1),
#endif
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static const snd_kcontrol_new_t snd_ac97_spdif_controls_alc650[] = {
        AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_ALC650_MULTICH, 11, 1, 0),
        AC97_SINGLE("Analog to IEC958 Output", AC97_ALC650_MULTICH, 12, 1, 0),
	/* disable this controls since it doesn't work as expected */
	/* AC97_SINGLE("IEC958 Input Monitor", AC97_ALC650_MULTICH, 13, 1, 0), */
};

static int patch_alc650_specific(ac97_t * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_alc650, ARRAY_SIZE(snd_ac97_controls_alc650))) < 0)
		return err;
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_alc650, ARRAY_SIZE(snd_ac97_spdif_controls_alc650))) < 0)
			return err;
	}
	return 0;
}

static struct snd_ac97_build_ops patch_alc650_ops = {
	.build_specific	= patch_alc650_specific,
	.update_jacks = alc650_update_jacks
};

int patch_alc650(ac97_t * ac97)
{
	unsigned short val;

	ac97->build_ops = &patch_alc650_ops;

	/* determine the revision */
	val = snd_ac97_read(ac97, AC97_ALC650_REVISION) & 0x3f;
	if (val < 3)
		ac97->id = 0x414c4720;          /* Old version */
	else if (val < 0x10)
		ac97->id = 0x414c4721;          /* D version */
	else if (val < 0x20)
		ac97->id = 0x414c4722;          /* E version */
	else if (val < 0x30)
		ac97->id = 0x414c4723;          /* F version */

	/* revision E or F */
	/* FIXME: what about revision D ? */
	ac97->spec.dev_flags = (ac97->id == 0x414c4722 ||
				ac97->id == 0x414c4723);

	/* enable AC97_ALC650_GPIO_SETUP, AC97_ALC650_CLOCK for R/W */
	snd_ac97_write_cache(ac97, AC97_ALC650_GPIO_STATUS, 
		snd_ac97_read(ac97, AC97_ALC650_GPIO_STATUS) | 0x8000);

	/* Enable SPDIF-IN only on Rev.E and above */
	val = snd_ac97_read(ac97, AC97_ALC650_CLOCK);
	/* SPDIF IN with pin 47 */
	if (ac97->spec.dev_flags)
		val |= 0x03; /* enable */
	else
		val &= ~0x03; /* disable */
	snd_ac97_write_cache(ac97, AC97_ALC650_CLOCK, val);

	/* set default: slot 3,4,7,8,6,9
	   spdif-in monitor off, analog-spdif off, spdif-in off
	   center on mic off, surround on line-in off
	   downmix off, duplicate front off
	*/
	snd_ac97_write_cache(ac97, AC97_ALC650_MULTICH, 0);

	/* set GPIO0 for mic bias */
	/* GPIO0 pin output, no interrupt, high */
	snd_ac97_write_cache(ac97, AC97_ALC650_GPIO_SETUP,
			     snd_ac97_read(ac97, AC97_ALC650_GPIO_SETUP) | 0x01);
	snd_ac97_write_cache(ac97, AC97_ALC650_GPIO_STATUS,
			     (snd_ac97_read(ac97, AC97_ALC650_GPIO_STATUS) | 0x100) & ~0x10);

	/* full DAC volume */
	snd_ac97_write_cache(ac97, AC97_ALC650_SURR_DAC_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_ALC650_LFE_DAC_VOL, 0x0808);
	return 0;
}

static void alc655_update_jacks(ac97_t *ac97)
{
	int shared;
	
	/* shared Line-In */
	shared = is_shared_linein(ac97);
	ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 1 << 9,
			      shared ? (1 << 9) : 0, 0);
	/* update shared mic */
	shared = is_shared_micin(ac97);
	/* misc control; vrefout disable */
	snd_ac97_update_bits(ac97, AC97_ALC650_CLOCK, 1 << 12,
			     shared ? (1 << 12) : 0);
	ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 1 << 10,
			      shared ? (1 << 10) : 0, 0);
}

static const snd_kcontrol_new_t snd_ac97_controls_alc655[] = {
	AC97_PAGE_SINGLE("Duplicate Front", AC97_ALC650_MULTICH, 0, 1, 0, 0),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int alc655_iec958_route_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts_655[3] = { "PCM", "Analog In", "IEC958 In" };
	static char *texts_658[4] = { "PCM", "Analog1 In", "Analog2 In", "IEC958 In" };
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ac97->spec.dev_flags ? 4 : 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       ac97->spec.dev_flags ?
	       texts_658[uinfo->value.enumerated.item] :
	       texts_655[uinfo->value.enumerated.item]);
	return 0;
}

static int alc655_iec958_route_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_ALC650_MULTICH];
	val = (val >> 12) & 3;
	if (ac97->spec.dev_flags && val == 3)
		val = 0;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int alc655_iec958_route_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	return ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 3 << 12,
				     (unsigned short)ucontrol->value.enumerated.item[0] << 12,
				     0);
}

static const snd_kcontrol_new_t snd_ac97_spdif_controls_alc655[] = {
        AC97_PAGE_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_ALC650_MULTICH, 11, 1, 0, 0),
	/* disable this controls since it doesn't work as expected */
        /* AC97_PAGE_SINGLE("IEC958 Input Monitor", AC97_ALC650_MULTICH, 14, 1, 0, 0), */
	{
		.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name   = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Route",
		.info   = alc655_iec958_route_info,
		.get    = alc655_iec958_route_get,
		.put    = alc655_iec958_route_put,
	},
};

static int patch_alc655_specific(ac97_t * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_alc655, ARRAY_SIZE(snd_ac97_controls_alc655))) < 0)
		return err;
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_alc655, ARRAY_SIZE(snd_ac97_spdif_controls_alc655))) < 0)
			return err;
	}
	return 0;
}

static struct snd_ac97_build_ops patch_alc655_ops = {
	.build_specific	= patch_alc655_specific,
	.update_jacks = alc655_update_jacks
};

int patch_alc655(ac97_t * ac97)
{
	unsigned int val;

	if (ac97->id == AC97_ID_ALC658) {
		ac97->spec.dev_flags = 1; /* ALC658 */
		if ((snd_ac97_read(ac97, AC97_ALC650_REVISION) & 0x3f) == 2) {
			ac97->id = AC97_ID_ALC658D;
			ac97->spec.dev_flags = 2;
		}
	}

	ac97->build_ops = &patch_alc655_ops;

	/* assume only page 0 for writing cache */
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, AC97_PAGE_VENDOR);

	/* adjust default values */
	val = snd_ac97_read(ac97, 0x7a); /* misc control */
	if (ac97->spec.dev_flags) /* ALC658 */
		val &= ~(1 << 1); /* Pin 47 is spdif input pin */
	else { /* ALC655 */
		if (ac97->subsystem_vendor == 0x1462 &&
		    ac97->subsystem_device == 0x0131) /* MSI S270 laptop */
			val &= ~(1 << 1); /* Pin 47 is EAPD (for internal speaker) */
		else
			val |= (1 << 1); /* Pin 47 is spdif input pin */
	}
	val &= ~(1 << 12); /* vref enable */
	snd_ac97_write_cache(ac97, 0x7a, val);
	/* set default: spdif-in enabled,
	   spdif-in monitor off, spdif-in PCM off
	   center on mic off, surround on line-in off
	   duplicate front off
	*/
	snd_ac97_write_cache(ac97, AC97_ALC650_MULTICH, 1<<15);

	/* full DAC volume */
	snd_ac97_write_cache(ac97, AC97_ALC650_SURR_DAC_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_ALC650_LFE_DAC_VOL, 0x0808);

	/* update undocumented bit... */
	if (ac97->id == AC97_ID_ALC658D)
		snd_ac97_update_bits(ac97, 0x74, 0x0800, 0x0800);

	return 0;
}


#define AC97_ALC850_JACK_SELECT	0x76
#define AC97_ALC850_MISC1	0x7a

static void alc850_update_jacks(ac97_t *ac97)
{
	int shared;
	
	/* shared Line-In */
	shared = is_shared_linein(ac97);
	/* SURR 1kOhm (bit4), Amp (bit5) */
	snd_ac97_update_bits(ac97, AC97_ALC850_MISC1, (1<<4)|(1<<5),
			     shared ? (1<<5) : (1<<4));
	/* LINE-IN = 0, SURROUND = 2 */
	snd_ac97_update_bits(ac97, AC97_ALC850_JACK_SELECT, 7 << 12,
			     shared ? (2<<12) : (0<<12));
	/* update shared mic */
	shared = is_shared_micin(ac97);
	/* Vref disable (bit12), 1kOhm (bit13) */
	snd_ac97_update_bits(ac97, AC97_ALC850_MISC1, (1<<12)|(1<<13),
			     shared ? (1<<12) : (1<<13));
	/* MIC-IN = 1, CENTER-LFE = 2 */
	snd_ac97_update_bits(ac97, AC97_ALC850_JACK_SELECT, 7 << 4,
			     shared ? (2<<4) : (1<<4));
}

static const snd_kcontrol_new_t snd_ac97_controls_alc850[] = {
	AC97_PAGE_SINGLE("Duplicate Front", AC97_ALC650_MULTICH, 0, 1, 0, 0),
	AC97_SINGLE("Mic Front Input Switch", AC97_ALC850_JACK_SELECT, 15, 1, 1),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int patch_alc850_specific(ac97_t *ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_alc850, ARRAY_SIZE(snd_ac97_controls_alc850))) < 0)
		return err;
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_alc655, ARRAY_SIZE(snd_ac97_spdif_controls_alc655))) < 0)
			return err;
	}
	return 0;
}

static struct snd_ac97_build_ops patch_alc850_ops = {
	.build_specific	= patch_alc850_specific,
	.update_jacks = alc850_update_jacks
};

int patch_alc850(ac97_t *ac97)
{
	ac97->build_ops = &patch_alc850_ops;

	ac97->spec.dev_flags = 0; /* for IEC958 playback route - ALC655 compatible */

	/* assume only page 0 for writing cache */
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, AC97_PAGE_VENDOR);

	/* adjust default values */
	/* set default: spdif-in enabled,
	   spdif-in monitor off, spdif-in PCM off
	   center on mic off, surround on line-in off
	   duplicate front off
	*/
	snd_ac97_write_cache(ac97, AC97_ALC650_MULTICH, 1<<15);
	/* SURR_OUT: on, Surr 1kOhm: on, Surr Amp: off, Front 1kOhm: off
	 * Front Amp: on, Vref: enable, Center 1kOhm: on, Mix: on
	 */
	snd_ac97_write_cache(ac97, 0x7a, (1<<1)|(1<<4)|(0<<5)|(1<<6)|
			     (1<<7)|(0<<12)|(1<<13)|(0<<14));
	/* detection UIO2,3: all path floating, UIO3: MIC, Vref2: disable,
	 * UIO1: FRONT, Vref3: disable, UIO3: LINE, Front-Mic: mute
	 */
	snd_ac97_write_cache(ac97, 0x76, (0<<0)|(0<<2)|(1<<4)|(1<<7)|(2<<8)|
			     (1<<11)|(0<<12)|(1<<15));

	/* full DAC volume */
	snd_ac97_write_cache(ac97, AC97_ALC650_SURR_DAC_VOL, 0x0808);
	snd_ac97_write_cache(ac97, AC97_ALC650_LFE_DAC_VOL, 0x0808);
	return 0;
}


/*
 * C-Media CM97xx codecs
 */
static void cm9738_update_jacks(ac97_t *ac97)
{
	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_CM9738_VENDOR_CTRL, 1 << 10,
			     is_shared_linein(ac97) ? (1 << 10) : 0);
}

static const snd_kcontrol_new_t snd_ac97_cm9738_controls[] = {
	AC97_SINGLE("Duplicate Front", AC97_CM9738_VENDOR_CTRL, 13, 1, 0),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_4CH_CTL,
};

static int patch_cm9738_specific(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9738_controls, ARRAY_SIZE(snd_ac97_cm9738_controls));
}

static struct snd_ac97_build_ops patch_cm9738_ops = {
	.build_specific	= patch_cm9738_specific,
	.update_jacks = cm9738_update_jacks
};

int patch_cm9738(ac97_t * ac97)
{
	ac97->build_ops = &patch_cm9738_ops;
	/* FIXME: can anyone confirm below? */
	/* CM9738 has no PCM volume although the register reacts */
	ac97->flags |= AC97_HAS_NO_PCM_VOL;
	snd_ac97_write_cache(ac97, AC97_PCM, 0x8000);

	return 0;
}

static int snd_ac97_cmedia_spdif_playback_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[] = { "Analog", "Digital" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_cmedia_spdif_playback_source_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_CM9739_SPDIF_CTRL];
	ucontrol->value.enumerated.item[0] = (val >> 1) & 0x01;
	return 0;
}

static int snd_ac97_cmedia_spdif_playback_source_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	return snd_ac97_update_bits(ac97, AC97_CM9739_SPDIF_CTRL,
				    0x01 << 1, 
				    (ucontrol->value.enumerated.item[0] & 0x01) << 1);
}

static const snd_kcontrol_new_t snd_ac97_cm9739_controls_spdif[] = {
	/* BIT 0: SPDI_EN - always true */
	{ /* BIT 1: SPDIFS */
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info	= snd_ac97_cmedia_spdif_playback_source_info,
		.get	= snd_ac97_cmedia_spdif_playback_source_get,
		.put	= snd_ac97_cmedia_spdif_playback_source_put,
	},
	/* BIT 2: IG_SPIV */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,NONE) "Valid Switch", AC97_CM9739_SPDIF_CTRL, 2, 1, 0),
	/* BIT 3: SPI2F */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,NONE) "Monitor", AC97_CM9739_SPDIF_CTRL, 3, 1, 0), 
	/* BIT 4: SPI2SDI */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_CM9739_SPDIF_CTRL, 4, 1, 0),
	/* BIT 8: SPD32 - 32bit SPDIF - not supported yet */
};

static void cm9739_update_jacks(ac97_t *ac97)
{
	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_CM9739_MULTI_CHAN, 1 << 10,
			     is_shared_linein(ac97) ? (1 << 10) : 0);
	/* shared Mic */
	snd_ac97_update_bits(ac97, AC97_CM9739_MULTI_CHAN, 0x3000,
			     is_shared_micin(ac97) ? 0x1000 : 0x2000);
}

static const snd_kcontrol_new_t snd_ac97_cm9739_controls[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int patch_cm9739_specific(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9739_controls, ARRAY_SIZE(snd_ac97_cm9739_controls));
}

static int patch_cm9739_post_spdif(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9739_controls_spdif, ARRAY_SIZE(snd_ac97_cm9739_controls_spdif));
}

static struct snd_ac97_build_ops patch_cm9739_ops = {
	.build_specific	= patch_cm9739_specific,
	.build_post_spdif = patch_cm9739_post_spdif,
	.update_jacks = cm9739_update_jacks
};

int patch_cm9739(ac97_t * ac97)
{
	unsigned short val;

	ac97->build_ops = &patch_cm9739_ops;

	/* CM9739/A has no Master and PCM volume although the register reacts */
	ac97->flags |= AC97_HAS_NO_MASTER_VOL | AC97_HAS_NO_PCM_VOL;
	snd_ac97_write_cache(ac97, AC97_MASTER, 0x8000);
	snd_ac97_write_cache(ac97, AC97_PCM, 0x8000);

	/* check spdif */
	val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
	if (val & AC97_EA_SPCV) {
		/* enable spdif in */
		snd_ac97_write_cache(ac97, AC97_CM9739_SPDIF_CTRL,
				     snd_ac97_read(ac97, AC97_CM9739_SPDIF_CTRL) | 0x01);
		ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */
	} else {
		ac97->ext_id &= ~AC97_EI_SPDIF; /* disable extended-id */
		ac97->rates[AC97_RATES_SPDIF] = 0;
	}

	/* set-up multi channel */
	/* bit 14: 0 = SPDIF, 1 = EAPD */
	/* bit 13: enable internal vref output for mic */
	/* bit 12: disable center/lfe (swithable) */
	/* bit 10: disable surround/line (switchable) */
	/* bit 9: mix 2 surround off */
	/* bit 4: undocumented; 0 mutes the CM9739A, which defaults to 1 */
	/* bit 3: undocumented; surround? */
	/* bit 0: dB */
	val = snd_ac97_read(ac97, AC97_CM9739_MULTI_CHAN) & (1 << 4);
	val |= (1 << 3);
	val |= (1 << 13);
	if (! (ac97->ext_id & AC97_EI_SPDIF))
		val |= (1 << 14);
	snd_ac97_write_cache(ac97, AC97_CM9739_MULTI_CHAN, val);

	/* FIXME: set up GPIO */
	snd_ac97_write_cache(ac97, 0x70, 0x0100);
	snd_ac97_write_cache(ac97, 0x72, 0x0020);
	/* Special exception for ASUS W1000/CMI9739. It does not have an SPDIF in. */
	if (ac97->pci &&
	     ac97->subsystem_vendor == 0x1043 &&
	     ac97->subsystem_device == 0x1843) {
		snd_ac97_write_cache(ac97, AC97_CM9739_SPDIF_CTRL,
			snd_ac97_read(ac97, AC97_CM9739_SPDIF_CTRL) & ~0x01);
		snd_ac97_write_cache(ac97, AC97_CM9739_MULTI_CHAN,
			snd_ac97_read(ac97, AC97_CM9739_MULTI_CHAN) | (1 << 14));
	}

	return 0;
}

#define AC97_CM9761_MULTI_CHAN	0x64
#define AC97_CM9761_FUNC	0x66
#define AC97_CM9761_SPDIF_CTRL	0x6c

static void cm9761_update_jacks(ac97_t *ac97)
{
	unsigned short surr_vals[2][2] = {
		{ 0x0008, 0x0400 }, /* off, on */
		{ 0x0000, 0x0408 }, /* off, on (9761-82 rev.B) */
	};
	unsigned short clfe_vals[2][2] = {
		{ 0x2000, 0x1880 }, /* off, on */
		{ 0x1000, 0x2880 }, /* off, on (9761-82 rev.B) */
	};

	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_CM9761_MULTI_CHAN, 0x0408,
			     surr_vals[ac97->spec.dev_flags][is_shared_linein(ac97)]);
	/* shared Mic */
	snd_ac97_update_bits(ac97, AC97_CM9761_MULTI_CHAN, 0x3880,
			     clfe_vals[ac97->spec.dev_flags][is_shared_micin(ac97)]);
}

static const snd_kcontrol_new_t snd_ac97_cm9761_controls[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int cm9761_spdif_out_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[] = { "AC-Link", "ADC", "SPDIF-In" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int cm9761_spdif_out_source_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	if (ac97->regs[AC97_CM9761_FUNC] & 0x1)
		ucontrol->value.enumerated.item[0] = 2; /* SPDIF-loopback */
	else if (ac97->regs[AC97_CM9761_SPDIF_CTRL] & 0x2)
		ucontrol->value.enumerated.item[0] = 1; /* ADC loopback */
	else
		ucontrol->value.enumerated.item[0] = 0; /* AC-link */
	return 0;
}

static int cm9761_spdif_out_source_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.enumerated.item[0] == 2)
		return snd_ac97_update_bits(ac97, AC97_CM9761_FUNC, 0x1, 0x1);
	snd_ac97_update_bits(ac97, AC97_CM9761_FUNC, 0x1, 0);
	return snd_ac97_update_bits(ac97, AC97_CM9761_SPDIF_CTRL, 0x2,
				    ucontrol->value.enumerated.item[0] == 1 ? 0x2 : 0);
}

static const char *cm9761_dac_clock[] = { "AC-Link", "SPDIF-In", "Both" };
static const struct ac97_enum cm9761_dac_clock_enum =
	AC97_ENUM_SINGLE(AC97_CM9761_SPDIF_CTRL, 9, 3, cm9761_dac_clock);

static const snd_kcontrol_new_t snd_ac97_cm9761_controls_spdif[] = {
	{ /* BIT 1: SPDIFS */
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info = cm9761_spdif_out_source_info,
		.get = cm9761_spdif_out_source_get,
		.put = cm9761_spdif_out_source_put,
	},
	/* BIT 2: IG_SPIV */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,NONE) "Valid Switch", AC97_CM9761_SPDIF_CTRL, 2, 1, 0),
	/* BIT 3: SPI2F */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,NONE) "Monitor", AC97_CM9761_SPDIF_CTRL, 3, 1, 0), 
	/* BIT 4: SPI2SDI */
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_CM9761_SPDIF_CTRL, 4, 1, 0),
	/* BIT 9-10: DAC_CTL */
	AC97_ENUM("DAC Clock Source", cm9761_dac_clock_enum),
};

static int patch_cm9761_post_spdif(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9761_controls_spdif, ARRAY_SIZE(snd_ac97_cm9761_controls_spdif));
}

static int patch_cm9761_specific(ac97_t * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9761_controls, ARRAY_SIZE(snd_ac97_cm9761_controls));
}

static struct snd_ac97_build_ops patch_cm9761_ops = {
	.build_specific	= patch_cm9761_specific,
	.build_post_spdif = patch_cm9761_post_spdif,
	.update_jacks = cm9761_update_jacks
};

int patch_cm9761(ac97_t *ac97)
{
	unsigned short val;

	/* CM9761 has no PCM volume although the register reacts */
	/* Master volume seems to have _some_ influence on the analog
	 * input sounds
	 */
	ac97->flags |= /*AC97_HAS_NO_MASTER_VOL |*/ AC97_HAS_NO_PCM_VOL;
	snd_ac97_write_cache(ac97, AC97_MASTER, 0x8808);
	snd_ac97_write_cache(ac97, AC97_PCM, 0x8808);

	ac97->spec.dev_flags = 0; /* 1 = model 82 revision B */
	if (ac97->id == AC97_ID_CM9761_82) {
		unsigned short tmp;
		/* check page 1, reg 0x60 */
		val = snd_ac97_read(ac97, AC97_INT_PAGING);
		snd_ac97_write_cache(ac97, AC97_INT_PAGING, (val & ~0x0f) | 0x01);
		tmp = snd_ac97_read(ac97, 0x60);
		ac97->spec.dev_flags = tmp & 1; /* revision B? */
		snd_ac97_write_cache(ac97, AC97_INT_PAGING, val);
	}

	ac97->build_ops = &patch_cm9761_ops;

	/* enable spdif */
	/* force the SPDIF bit in ext_id - codec doesn't set this bit! */
        ac97->ext_id |= AC97_EI_SPDIF;
	/* to be sure: we overwrite the ext status bits */
	snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, 0x05c0);
	/* Don't set 0x0200 here.  This results in the silent analog output */
	snd_ac97_write_cache(ac97, AC97_CM9761_SPDIF_CTRL, 0x0001); /* enable spdif-in */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */

	/* set-up multi channel */
	/* bit 15: pc master beep off
	 * bit 14: pin47 = EAPD/SPDIF
	 * bit 13: vref ctl [= cm9739]
	 * bit 12: CLFE control (reverted on rev B)
	 * bit 11: Mic/center share (reverted on rev B)
	 * bit 10: suddound/line share
	 * bit  9: Analog-in mix -> surround
	 * bit  8: Analog-in mix -> CLFE
	 * bit  7: Mic/LFE share (mic/center/lfe)
	 * bit  5: vref select (9761A)
	 * bit  4: front control
	 * bit  3: surround control (revereted with rev B)
	 * bit  2: front mic
	 * bit  1: stereo mic
	 * bit  0: mic boost level (0=20dB, 1=30dB)
	 */

#if 0
	if (ac97->spec.dev_flags)
		val = 0x0214;
	else
		val = 0x321c;
#endif
	val = snd_ac97_read(ac97, AC97_CM9761_MULTI_CHAN);
	val |= (1 << 4); /* front on */
	snd_ac97_write_cache(ac97, AC97_CM9761_MULTI_CHAN, val);

	/* FIXME: set up GPIO */
	snd_ac97_write_cache(ac97, 0x70, 0x0100);
	snd_ac97_write_cache(ac97, 0x72, 0x0020);

	return 0;
}
       
#define AC97_CM9780_SIDE	0x60
#define AC97_CM9780_JACK	0x62
#define AC97_CM9780_MIXER	0x64
#define AC97_CM9780_MULTI_CHAN	0x66
#define AC97_CM9780_SPDIF	0x6c

static const char *cm9780_ch_select[] = { "Front", "Side", "Center/LFE", "Rear" };
static const struct ac97_enum cm9780_ch_select_enum =
	AC97_ENUM_SINGLE(AC97_CM9780_MULTI_CHAN, 6, 4, cm9780_ch_select);
static const snd_kcontrol_new_t cm9780_controls[] = {
	AC97_DOUBLE("Side Playback Switch", AC97_CM9780_SIDE, 15, 7, 1, 1),
	AC97_DOUBLE("Side Playback Volume", AC97_CM9780_SIDE, 8, 0, 31, 0),
	AC97_ENUM("Side Playback Route", cm9780_ch_select_enum),
};

static int patch_cm9780_specific(ac97_t *ac97)
{
	return patch_build_controls(ac97, cm9780_controls, ARRAY_SIZE(cm9780_controls));
}

static struct snd_ac97_build_ops patch_cm9780_ops = {
	.build_specific	= patch_cm9780_specific,
	.build_post_spdif = patch_cm9761_post_spdif	/* identical with CM9761 */
};

int patch_cm9780(ac97_t *ac97)
{
	unsigned short val;

	ac97->build_ops = &patch_cm9780_ops;

	/* enable spdif */
	if (ac97->ext_id & AC97_EI_SPDIF) {
		ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */
		val = snd_ac97_read(ac97, AC97_CM9780_SPDIF);
		val |= 0x1; /* SPDI_EN */
		snd_ac97_write_cache(ac97, AC97_CM9780_SPDIF, val);
	}

	return 0;
}

/*
 * VIA VT1616 codec
 */
static const snd_kcontrol_new_t snd_ac97_controls_vt1616[] = {
AC97_SINGLE("DC Offset removal", 0x5a, 10, 1, 0),
AC97_SINGLE("Alternate Level to Surround Out", 0x5a, 15, 1, 0),
AC97_SINGLE("Downmix LFE and Center to Front", 0x5a, 12, 1, 0),
AC97_SINGLE("Downmix Surround to Front", 0x5a, 11, 1, 0),
};

static int patch_vt1616_specific(ac97_t * ac97)
{
	int err;

	if (snd_ac97_try_bit(ac97, 0x5a, 9))
		if ((err = patch_build_controls(ac97, &snd_ac97_controls_vt1616[0], 1)) < 0)
			return err;
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_vt1616[1], ARRAY_SIZE(snd_ac97_controls_vt1616) - 1)) < 0)
		return err;
	return 0;
}

static struct snd_ac97_build_ops patch_vt1616_ops = {
	.build_specific	= patch_vt1616_specific
};

int patch_vt1616(ac97_t * ac97)
{
	ac97->build_ops = &patch_vt1616_ops;
	return 0;
}

/*
 * VT1617A codec
 */
int patch_vt1617a(ac97_t * ac97)
{
	ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000;
	return 0;
}

/*
 */
static void it2646_update_jacks(ac97_t *ac97)
{
	/* shared Line-In */
	snd_ac97_update_bits(ac97, 0x76, 1 << 9,
			     is_shared_linein(ac97) ? (1<<9) : 0);
	/* shared Mic */
	snd_ac97_update_bits(ac97, 0x76, 1 << 10,
			     is_shared_micin(ac97) ? (1<<10) : 0);
}

static const snd_kcontrol_new_t snd_ac97_controls_it2646[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static const snd_kcontrol_new_t snd_ac97_spdif_controls_it2646[] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), 0x76, 11, 1, 0),
	AC97_SINGLE("Analog to IEC958 Output", 0x76, 12, 1, 0),
	AC97_SINGLE("IEC958 Input Monitor", 0x76, 13, 1, 0),
};

static int patch_it2646_specific(ac97_t * ac97)
{
	int err;
	if ((err = patch_build_controls(ac97, snd_ac97_controls_it2646, ARRAY_SIZE(snd_ac97_controls_it2646))) < 0)
		return err;
	if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_it2646, ARRAY_SIZE(snd_ac97_spdif_controls_it2646))) < 0)
		return err;
	return 0;
}

static struct snd_ac97_build_ops patch_it2646_ops = {
	.build_specific	= patch_it2646_specific,
	.update_jacks = it2646_update_jacks
};

int patch_it2646(ac97_t * ac97)
{
	ac97->build_ops = &patch_it2646_ops;
	/* full DAC volume */
	snd_ac97_write_cache(ac97, 0x5E, 0x0808);
	snd_ac97_write_cache(ac97, 0x7A, 0x0808);
	return 0;
}

/*
 * Si3036 codec
 */

#define AC97_SI3036_CHIP_ID     0x5a
#define AC97_SI3036_LINE_CFG    0x5c

static const snd_kcontrol_new_t snd_ac97_controls_si3036[] = {
AC97_DOUBLE("Modem Speaker Volume", 0x5c, 14, 12, 3, 1)
};

static int patch_si3036_specific(ac97_t * ac97)
{
	int idx, err;
	for (idx = 0; idx < ARRAY_SIZE(snd_ac97_controls_si3036); idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ctl_new1(&snd_ac97_controls_si3036[idx], ac97))) < 0)
			return err;
	return 0;
}

static struct snd_ac97_build_ops patch_si3036_ops = {
	.build_specific	= patch_si3036_specific,
};

int mpatch_si3036(ac97_t * ac97)
{
	ac97->build_ops = &patch_si3036_ops;
	snd_ac97_write_cache(ac97, 0x5c, 0xf210 );
	snd_ac97_write_cache(ac97, 0x68, 0);
	return 0;
}
