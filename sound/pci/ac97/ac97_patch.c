/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

#include "ac97_local.h"
#include "ac97_patch.h"

/*
 *  Chip specific initialization
 */

static int patch_build_controls(struct snd_ac97 * ac97, const struct snd_kcontrol_new *controls, int count)
{
	int idx, err;

	for (idx = 0; idx < count; idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&controls[idx], ac97))) < 0)
			return err;
	return 0;
}

/* replace with a new TLV */
static void reset_tlv(struct snd_ac97 *ac97, const char *name,
		      const unsigned int *tlv)
{
	struct snd_ctl_elem_id sid;
	struct snd_kcontrol *kctl;
	memset(&sid, 0, sizeof(sid));
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kctl = snd_ctl_find_id(ac97->bus->card, &sid);
	if (kctl && kctl->tlv.p)
		kctl->tlv.p = tlv;
}

/* set to the page, update bits and restore the page */
static int ac97_update_bits_page(struct snd_ac97 *ac97, unsigned short reg, unsigned short mask, unsigned short value, unsigned short page)
{
	unsigned short page_save;
	int ret;

	mutex_lock(&ac97->page_mutex);
	page_save = snd_ac97_read(ac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page);
	ret = snd_ac97_update_bits(ac97, reg, mask, value);
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, page_save);
	mutex_unlock(&ac97->page_mutex); /* unlock paging */
	return ret;
}

/*
 * shared line-in/mic controls
 */
static int ac97_enum_text_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo,
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

static int ac97_surround_jack_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[] = { "Shared", "Independent" };
	return ac97_enum_text_info(kcontrol, uinfo, texts, 2);
}

static int ac97_surround_jack_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->indep_surround;
	return 0;
}

static int ac97_surround_jack_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char indep = !!ucontrol->value.enumerated.item[0];

	if (indep != ac97->indep_surround) {
		ac97->indep_surround = indep;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

static int ac97_channel_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[] = { "2ch", "4ch", "6ch", "8ch" };
	return ac97_enum_text_info(kcontrol, uinfo, texts,
		kcontrol->private_value);
}

static int ac97_channel_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->channel_mode;
	return 0;
}

static int ac97_channel_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char mode = ucontrol->value.enumerated.item[0];

	if (mode >= kcontrol->private_value)
		return -EINVAL;

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
/* 6ch */
#define AC97_CHANNEL_MODE_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 3, \
	}
/* 4ch */
#define AC97_CHANNEL_MODE_4CH_CTL \
	{ \
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name	= "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 2, \
	}
/* 8ch */
#define AC97_CHANNEL_MODE_8CH_CTL \
	{ \
		.iface  = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name   = "Channel Mode", \
		.info = ac97_channel_mode_info, \
		.get = ac97_channel_mode_get, \
		.put = ac97_channel_mode_put, \
		.private_value = 4, \
	}

static inline int is_surround_on(struct snd_ac97 *ac97)
{
	return ac97->channel_mode >= 1;
}

static inline int is_clfe_on(struct snd_ac97 *ac97)
{
	return ac97->channel_mode >= 2;
}

/* system has shared jacks with surround out enabled */
static inline int is_shared_surrout(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && is_surround_on(ac97);
}

/* system has shared jacks with center/lfe out enabled */
static inline int is_shared_clfeout(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && is_clfe_on(ac97);
}

/* system has shared jacks with line in enabled */
static inline int is_shared_linein(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && !is_surround_on(ac97);
}

/* system has shared jacks with mic in enabled */
static inline int is_shared_micin(struct snd_ac97 *ac97)
{
	return !ac97->indep_surround && !is_clfe_on(ac97);
}

static inline int alc850_is_aux_back_surround(struct snd_ac97 *ac97)
{
	return is_surround_on(ac97);
}

/* The following snd_ac97_ymf753_... items added by David Shust (dshust@shustring.com) */
/* Modified for YMF743 by Keita Maehara <maehara@debian.org> */

/* It is possible to indicate to the Yamaha YMF7x3 the type of
   speakers being used. */

static int snd_ac97_ymf7x3_info_speaker(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_ymf7x3_get_speaker(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_3D_MODE_SEL];
	val = (val >> 10) & 3;
	if (val > 0)    /* 0 = invalid */
		val--;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_ymf7x3_put_speaker(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] + 1) << 10;
	return snd_ac97_update(ac97, AC97_YMF7X3_3D_MODE_SEL, val);
}

static const struct snd_kcontrol_new snd_ac97_ymf7x3_controls_speaker =
{
	.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name   = "3D Control - Speaker",
	.info   = snd_ac97_ymf7x3_info_speaker,
	.get    = snd_ac97_ymf7x3_get_speaker,
	.put    = snd_ac97_ymf7x3_put_speaker,
};

/* It is possible to indicate to the Yamaha YMF7x3 the source to
   direct to the S/PDIF output. */
static int snd_ac97_ymf7x3_spdif_source_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_ymf7x3_spdif_source_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_DIT_CTRL];
	ucontrol->value.enumerated.item[0] = (val >> 1) & 1;
	return 0;
}

static int snd_ac97_ymf7x3_spdif_source_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 1;
	return snd_ac97_update_bits(ac97, AC97_YMF7X3_DIT_CTRL, 0x0002, val);
}

static int patch_yamaha_ymf7x3_3d(struct snd_ac97 *ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97);
	err = snd_ctl_add(ac97->bus->card, kctl);
	if (err < 0)
		return err;
	strcpy(kctl->id.name, "3D Control - Wide");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 9, 7, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	err = snd_ctl_add(ac97->bus->card,
			  snd_ac97_cnew(&snd_ac97_ymf7x3_controls_speaker,
					ac97));
	if (err < 0)
		return err;
	snd_ac97_write_cache(ac97, AC97_YMF7X3_3D_MODE_SEL, 0x0c00);
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_yamaha_ymf743_controls_spdif[3] =
{
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		    AC97_YMF7X3_DIT_CTRL, 0, 1, 0),
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("", PLAYBACK, NONE) "Source",
		.info	= snd_ac97_ymf7x3_spdif_source_info,
		.get	= snd_ac97_ymf7x3_spdif_source_get,
		.put	= snd_ac97_ymf7x3_spdif_source_put,
	},
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", NONE, NONE) "Mute",
		    AC97_YMF7X3_DIT_CTRL, 2, 1, 1)
};

static int patch_yamaha_ymf743_build_spdif(struct snd_ac97 *ac97)
{
	int err;

	err = patch_build_controls(ac97, &snd_ac97_controls_spdif[0], 3);
	if (err < 0)
		return err;
	err = patch_build_controls(ac97,
				   snd_ac97_yamaha_ymf743_controls_spdif, 3);
	if (err < 0)
		return err;
	/* set default PCM S/PDIF params */
	/* PCM audio,no copyright,no preemphasis,PCM coder,original */
	snd_ac97_write_cache(ac97, AC97_YMF7X3_DIT_CTRL, 0xa201);
	return 0;
}

static const struct snd_ac97_build_ops patch_yamaha_ymf743_ops = {
	.build_spdif	= patch_yamaha_ymf743_build_spdif,
	.build_3d	= patch_yamaha_ymf7x3_3d,
};

static int patch_yamaha_ymf743(struct snd_ac97 *ac97)
{
	ac97->build_ops = &patch_yamaha_ymf743_ops;
	ac97->caps |= AC97_BC_BASS_TREBLE;
	ac97->caps |= 0x04 << 10; /* Yamaha 3D enhancement */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */
	ac97->ext_id |= AC97_EI_SPDIF; /* force the detection of spdif */
	return 0;
}

/* The AC'97 spec states that the S/PDIF signal is to be output at pin 48.
   The YMF753 will output the S/PDIF signal to pin 43, 47 (EAPD), or 48.
   By default, no output pin is selected, and the S/PDIF signal is not output.
   There is also a bit to mute S/PDIF output in a vendor-specific register. */
static int snd_ac97_ymf753_spdif_output_pin_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_ymf753_spdif_output_pin_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_YMF7X3_DIT_CTRL];
	ucontrol->value.enumerated.item[0] = (val & 0x0008) ? 2 : (val & 0x0020) ? 1 : 0;
	return 0;
}

static int snd_ac97_ymf753_spdif_output_pin_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] == 2) ? 0x0008 :
	      (ucontrol->value.enumerated.item[0] == 1) ? 0x0020 : 0;
	return snd_ac97_update_bits(ac97, AC97_YMF7X3_DIT_CTRL, 0x0028, val);
	/* The following can be used to direct S/PDIF output to pin 47 (EAPD).
	   snd_ac97_write_cache(ac97, 0x62, snd_ac97_read(ac97, 0x62) | 0x0008); */
}

static const struct snd_kcontrol_new snd_ac97_ymf753_controls_spdif[3] = {
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info	= snd_ac97_ymf7x3_spdif_source_info,
		.get	= snd_ac97_ymf7x3_spdif_source_get,
		.put	= snd_ac97_ymf7x3_spdif_source_put,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Output Pin",
		.info	= snd_ac97_ymf753_spdif_output_pin_info,
		.get	= snd_ac97_ymf753_spdif_output_pin_get,
		.put	= snd_ac97_ymf753_spdif_output_pin_put,
	},
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("", NONE, NONE) "Mute",
		    AC97_YMF7X3_DIT_CTRL, 2, 1, 1)
};

static int patch_yamaha_ymf753_post_spdif(struct snd_ac97 * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_ymf753_controls_spdif, ARRAY_SIZE(snd_ac97_ymf753_controls_spdif))) < 0)
		return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_yamaha_ymf753_ops = {
	.build_3d	= patch_yamaha_ymf7x3_3d,
	.build_post_spdif = patch_yamaha_ymf753_post_spdif
};

static int patch_yamaha_ymf753(struct snd_ac97 * ac97)
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
 * May 2, 2003 Liam Girdwood <lrg@slimlogic.co.uk>
 *  removed broken wolfson00 patch.
 *  added support for WM9705,WM9708,WM9709,WM9710,WM9711,WM9712 and WM9717.
 */

static const struct snd_kcontrol_new wm97xx_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
};

static int patch_wolfson_wm9703_specific(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_wolfson_wm9703_ops = {
	.build_specific = patch_wolfson_wm9703_specific,
};

static int patch_wolfson03(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_wolfson_wm9703_ops;
	return 0;
}

static const struct snd_kcontrol_new wm9704_snd_ac97_controls[] = {
AC97_DOUBLE("Front Playback Volume", AC97_WM97XX_FMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Front Playback Switch", AC97_WM97XX_FMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear Playback Volume", AC97_WM9704_RMIXER_VOL, 8, 0, 31, 1),
AC97_SINGLE("Rear Playback Switch", AC97_WM9704_RMIXER_VOL, 15, 1, 1),
AC97_DOUBLE("Rear DAC Volume", AC97_WM9704_RPCM_VOL, 8, 0, 31, 1),
AC97_DOUBLE("Surround Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
};

static int patch_wolfson_wm9704_specific(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_wolfson_wm9704_ops = {
	.build_specific = patch_wolfson_wm9704_specific,
};

static int patch_wolfson04(struct snd_ac97 * ac97)
{
	/* WM9704M/9704Q */
	ac97->build_ops = &patch_wolfson_wm9704_ops;
	return 0;
}

static int patch_wolfson05(struct snd_ac97 * ac97)
{
	/* WM9705, WM9710 */
	ac97->build_ops = &patch_wolfson_wm9703_ops;
#ifdef CONFIG_TOUCHSCREEN_WM9705
	/* WM9705 touchscreen uses AUX and VIDEO for touch */
	ac97->flags |= AC97_HAS_NO_VIDEO | AC97_HAS_NO_AUX;
#endif
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

static const struct snd_kcontrol_new wm9711_snd_ac97_controls[] = {
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
AC97_SINGLE("Out3 ZC Switch", AC97_AUX, 7, 1, 0),
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
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 63, 1),
AC97_SINGLE("Capture ZC Switch", AC97_REC_GAIN, 7, 1, 0),

AC97_SINGLE("Mic 1 to Phone Switch", AC97_MIC, 14, 1, 1),
AC97_SINGLE("Mic 2 to Phone Switch", AC97_MIC, 13, 1, 1),
AC97_ENUM("Mic Select Source", wm9711_enum[7]),
AC97_SINGLE("Mic 1 Volume", AC97_MIC, 8, 31, 1),
AC97_SINGLE("Mic 2 Volume", AC97_MIC, 0, 31, 1),
AC97_SINGLE("Mic 20dB Boost Switch", AC97_MIC, 7, 1, 0),

AC97_SINGLE("Master Left Inv Switch", AC97_MASTER, 6, 1, 0),
AC97_SINGLE("Master ZC Switch", AC97_MASTER, 7, 1, 0),
AC97_SINGLE("Headphone ZC Switch", AC97_HEADPHONE, 7, 1, 0),
AC97_SINGLE("Mono ZC Switch", AC97_MASTER_MONO, 7, 1, 0),
};

static int patch_wolfson_wm9711_specific(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_wolfson_wm9711_ops = {
	.build_specific = patch_wolfson_wm9711_specific,
};

static int patch_wolfson11(struct snd_ac97 * ac97)
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

static const struct snd_kcontrol_new wm13_snd_ac97_controls[] = {
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

AC97_SINGLE("Beep to Headphone Switch", AC97_AUX, 15, 1, 1),
AC97_SINGLE("Beep to Headphone Volume", AC97_AUX, 12, 7, 1),
AC97_SINGLE("Beep to Master Switch", AC97_AUX, 11, 1, 1),
AC97_SINGLE("Beep to Master Volume", AC97_AUX, 8, 7, 1),
AC97_SINGLE("Beep to Mono Switch", AC97_AUX, 7, 1, 1),
AC97_SINGLE("Beep to Mono Volume", AC97_AUX, 4, 7, 1),

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

static const struct snd_kcontrol_new wm13_snd_ac97_controls_3d[] = {
AC97_ENUM("Inv Input Mux", wm9713_enum[11]),
AC97_SINGLE("3D Upper Cut-off Switch", AC97_REC_GAIN_MIC, 5, 1, 0),
AC97_SINGLE("3D Lower Cut-off Switch", AC97_REC_GAIN_MIC, 4, 1, 0),
AC97_SINGLE("3D Depth", AC97_REC_GAIN_MIC, 0, 15, 1),
};

static int patch_wolfson_wm9713_3d (struct snd_ac97 * ac97)
{
	int err, i;
    
	for (i = 0; i < ARRAY_SIZE(wm13_snd_ac97_controls_3d); i++) {
		if ((err = snd_ctl_add(ac97->bus->card, snd_ac97_cnew(&wm13_snd_ac97_controls_3d[i], ac97))) < 0)
			return err;
	}
	return 0;
}

static int patch_wolfson_wm9713_specific(struct snd_ac97 * ac97)
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
static void patch_wolfson_wm9713_suspend (struct snd_ac97 * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xfeff);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0xffff);
}

static void patch_wolfson_wm9713_resume (struct snd_ac97 * ac97)
{
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MID, 0xda00);
	snd_ac97_write_cache(ac97, AC97_EXTENDED_MSTATUS, 0x3810);
	snd_ac97_write_cache(ac97, AC97_POWERDOWN, 0x0);
}
#endif

static const struct snd_ac97_build_ops patch_wolfson_wm9713_ops = {
	.build_specific = patch_wolfson_wm9713_specific,
	.build_3d = patch_wolfson_wm9713_3d,
#ifdef CONFIG_PM	
	.suspend = patch_wolfson_wm9713_suspend,
	.resume = patch_wolfson_wm9713_resume
#endif
};

static int patch_wolfson13(struct snd_ac97 * ac97)
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
static int patch_tritech_tr28028(struct snd_ac97 * ac97)
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
static int patch_sigmatel_stac9700_3d(struct snd_ac97 * ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	if ((err = snd_ctl_add(ac97->bus->card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
		return err;
	strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
	kctl->private_value = AC97_SINGLE_VALUE(AC97_3D_CONTROL, 2, 3, 0);
	snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9708_3d(struct snd_ac97 * ac97)
{
	struct snd_kcontrol *kctl;
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

static const struct snd_kcontrol_new snd_ac97_sigmatel_4speaker =
AC97_SINGLE("Sigmatel 4-Speaker Stereo Playback Switch",
		AC97_SIGMATEL_DAC2INVERT, 2, 1, 0);

/* "Sigmatel " removed due to excessive name length: */
static const struct snd_kcontrol_new snd_ac97_sigmatel_phaseinvert =
AC97_SINGLE("Surround Phase Inversion Playback Switch",
		AC97_SIGMATEL_DAC2INVERT, 3, 1, 0);

static const struct snd_kcontrol_new snd_ac97_sigmatel_controls[] = {
AC97_SINGLE("Sigmatel DAC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 1, 1, 0),
AC97_SINGLE("Sigmatel ADC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 0, 1, 0)
};

static int patch_sigmatel_stac97xx_specific(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_sigmatel_stac9700_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac97xx_specific
};

static int patch_sigmatel_stac9700(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_sigmatel_stac9700_ops;
	return 0;
}

static int snd_ac97_stac9708_put_bias(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int err;

	mutex_lock(&ac97->page_mutex);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	err = snd_ac97_update_bits(ac97, AC97_SIGMATEL_BIAS2, 0x0010,
				   (ucontrol->value.integer.value[0] & 1) << 4);
	snd_ac97_write(ac97, AC97_SIGMATEL_BIAS1, 0);
	mutex_unlock(&ac97->page_mutex);
	return err;
}

static const struct snd_kcontrol_new snd_ac97_stac9708_bias_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sigmatel Output Bias Switch",
	.info = snd_ac97_info_volsw,
	.get = snd_ac97_get_volsw,
	.put = snd_ac97_stac9708_put_bias,
	.private_value = AC97_SINGLE_VALUE(AC97_SIGMATEL_BIAS2, 4, 1, 0),
};

static int patch_sigmatel_stac9708_specific(struct snd_ac97 *ac97)
{
	int err;

	/* the register bit is writable, but the function is not implemented: */
	snd_ac97_remove_ctl(ac97, "PCM Out Path & Mute", NULL);

	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Sigmatel Surround Playback");
	if ((err = patch_build_controls(ac97, &snd_ac97_stac9708_bias_control, 1)) < 0)
		return err;
	return patch_sigmatel_stac97xx_specific(ac97);
}

static const struct snd_ac97_build_ops patch_sigmatel_stac9708_ops = {
	.build_3d	= patch_sigmatel_stac9708_3d,
	.build_specific	= patch_sigmatel_stac9708_specific
};

static int patch_sigmatel_stac9708(struct snd_ac97 * ac97)
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

static int patch_sigmatel_stac9721(struct snd_ac97 * ac97)
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

static int patch_sigmatel_stac9744(struct snd_ac97 * ac97)
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

static int patch_sigmatel_stac9756(struct snd_ac97 * ac97)
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

static int snd_ac97_stac9758_output_jack_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_stac9758_output_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_OUTSEL] >> shift;
	if (!(val & 4))
		ucontrol->value.enumerated.item[0] = 0;
	else
		ucontrol->value.enumerated.item[0] = 1 + (val & 3);
	return 0;
}

static int snd_ac97_stac9758_output_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
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

static int snd_ac97_stac9758_input_jack_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_stac9758_input_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;
	unsigned short val;

	val = ac97->regs[AC97_SIGMATEL_INSEL];
	ucontrol->value.enumerated.item[0] = (val >> shift) & 7;
	return 0;
}

static int snd_ac97_stac9758_input_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value;

	return ac97_update_bits_page(ac97, AC97_SIGMATEL_INSEL, 7 << shift,
				     ucontrol->value.enumerated.item[0] << shift, 0);
}

static int snd_ac97_stac9758_phonesel_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_stac9758_phonesel_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ac97->regs[AC97_SIGMATEL_IOMISC] & 3;
	return 0;
}

static int snd_ac97_stac9758_phonesel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

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
static const struct snd_kcontrol_new snd_ac97_sigmatel_stac9758_controls[] = {
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

static int patch_sigmatel_stac9758_specific(struct snd_ac97 *ac97)
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

static const struct snd_ac97_build_ops patch_sigmatel_stac9758_ops = {
	.build_3d	= patch_sigmatel_stac9700_3d,
	.build_specific	= patch_sigmatel_stac9758_specific
};

static int patch_sigmatel_stac9758(struct snd_ac97 * ac97)
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
static const struct snd_kcontrol_new snd_ac97_cirrus_controls_spdif[2] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CSR_SPDIF, 15, 1, 0),
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA", AC97_CSR_ACMODE, 0, 3, 0)
};

static int patch_cirrus_build_spdif(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_cirrus_ops = {
	.build_spdif = patch_cirrus_build_spdif
};

static int patch_cirrus_spdif(struct snd_ac97 * ac97)
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

static int patch_cirrus_cs4299(struct snd_ac97 * ac97)
{
	/* force the detection of PC Beep */
	ac97->flags |= AC97_HAS_PC_BEEP;
	
	return patch_cirrus_spdif(ac97);
}

/*
 * Conexant codecs
 */
static const struct snd_kcontrol_new snd_ac97_conexant_controls_spdif[1] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CXR_AUDIO_MISC, 3, 1, 0),
};

static int patch_conexant_build_spdif(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_conexant_ops = {
	.build_spdif = patch_conexant_build_spdif
};

static int patch_conexant(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_conexant_ops;
	ac97->flags |= AC97_CX_SPDIF;
        ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_48000; /* 48k only */
	return 0;
}

static int patch_cx20551(struct snd_ac97 *ac97)
{
	snd_ac97_update_bits(ac97, 0x5c, 0x01, 0x01);
	return 0;
}

/*
 * Analog Device AD18xx, AD19xx codecs
 */
#ifdef CONFIG_PM
static void ad18xx_resume(struct snd_ac97 *ac97)
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

static void ad1888_resume(struct snd_ac97 *ac97)
{
	ad18xx_resume(ac97);
	snd_ac97_write_cache(ac97, AC97_CODEC_CLASS_REV, 0x8080);
}

#endif

static const struct snd_ac97_res_table ad1819_restbl[] = {
	{ AC97_PHONE, 0x9f1f },
	{ AC97_MIC, 0x9f1f },
	{ AC97_LINE, 0x9f1f },
	{ AC97_CD, 0x9f1f },
	{ AC97_VIDEO, 0x9f1f },
	{ AC97_AUX, 0x9f1f },
	{ AC97_PCM, 0x9f1f },
	{ } /* terminator */
};

static int patch_ad1819(struct snd_ac97 * ac97)
{
	unsigned short scfg;

	// patch for Analog Devices
	scfg = snd_ac97_read(ac97, AC97_AD_SERIAL_CFG);
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, scfg | 0x7000); /* select all codecs */
	ac97->res_table = ad1819_restbl;
	return 0;
}

static unsigned short patch_ad1881_unchained(struct snd_ac97 * ac97, int idx, unsigned short mask)
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

static int patch_ad1881_chained1(struct snd_ac97 * ac97, int idx, unsigned short codec_bits)
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

static void patch_ad1881_chained(struct snd_ac97 * ac97, int unchained_idx, int cidx1, int cidx2)
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
		if (cidx2 < 0)
			patch_ad1881_chained1(ac97, cidx1, 0);
		else if (patch_ad1881_chained1(ac97, cidx1, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx2, 0);
		else if (patch_ad1881_chained1(ac97, cidx2, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx1, 0);
	} else if (cidx2 >= 0) {
		patch_ad1881_chained1(ac97, cidx2, 0);
	}
}

static const struct snd_ac97_build_ops patch_ad1881_build_ops = {
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1881(struct snd_ac97 * ac97)
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

	if (! (codecs[0] || codecs[1] || codecs[2]))
		goto __end;

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

static const struct snd_kcontrol_new snd_ac97_controls_ad1885[] = {
	AC97_SINGLE("Digital Mono Direct", AC97_AD_MISC, 11, 1, 0),
	/* AC97_SINGLE("Digital Audio Mode", AC97_AD_MISC, 12, 1, 0), */ /* seems problematic */
	AC97_SINGLE("Low Power Mixer", AC97_AD_MISC, 14, 1, 0),
	AC97_SINGLE("Zero Fill DAC", AC97_AD_MISC, 15, 1, 0),
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 9, 1, 1), /* inverted */
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 8, 1, 1), /* inverted */
};

static const DECLARE_TLV_DB_SCALE(db_scale_6bit_6db_max, -8850, 150, 0);

static int patch_ad1885_specific(struct snd_ac97 * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_ad1885, ARRAY_SIZE(snd_ac97_controls_ad1885))) < 0)
		return err;
	reset_tlv(ac97, "Headphone Playback Volume",
		  db_scale_6bit_6db_max);
	return 0;
}

static const struct snd_ac97_build_ops patch_ad1885_build_ops = {
	.build_specific = &patch_ad1885_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1885(struct snd_ac97 * ac97)
{
	patch_ad1881(ac97);
	/* This is required to deal with the Intel D815EEAL2 */
	/* i.e. Line out is actually headphone out from codec */

	/* set default */
	snd_ac97_write_cache(ac97, AC97_AD_MISC, 0x0404);

	ac97->build_ops = &patch_ad1885_build_ops;
	return 0;
}

static int patch_ad1886_specific(struct snd_ac97 * ac97)
{
	reset_tlv(ac97, "Headphone Playback Volume",
		  db_scale_6bit_6db_max);
	return 0;
}

static const struct snd_ac97_build_ops patch_ad1886_build_ops = {
	.build_specific = &patch_ad1886_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1886(struct snd_ac97 * ac97)
{
	patch_ad1881(ac97);
	/* Presario700 workaround */
	/* for Jack Sense/SPDIF Register misetting causing */
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, 0x0010);
	ac97->build_ops = &patch_ad1886_build_ops;
	return 0;
}

/* MISC bits (AD1888/AD1980/AD1985 register 0x76) */
#define AC97_AD198X_MBC		0x0003	/* mic boost */
#define AC97_AD198X_MBC_20	0x0000	/* +20dB */
#define AC97_AD198X_MBC_10	0x0001	/* +10dB */
#define AC97_AD198X_MBC_30	0x0002	/* +30dB */
#define AC97_AD198X_VREFD	0x0004	/* VREF high-Z */
#define AC97_AD198X_VREFH	0x0008	/* 0=2.25V, 1=3.7V */
#define AC97_AD198X_VREF_0	0x000c	/* 0V (AD1985 only) */
#define AC97_AD198X_VREF_MASK	(AC97_AD198X_VREFH | AC97_AD198X_VREFD)
#define AC97_AD198X_VREF_SHIFT	2
#define AC97_AD198X_SRU		0x0010	/* sample rate unlock */
#define AC97_AD198X_LOSEL	0x0020	/* LINE_OUT amplifiers input select */
#define AC97_AD198X_2MIC	0x0040	/* 2-channel mic select */
#define AC97_AD198X_SPRD	0x0080	/* SPREAD enable */
#define AC97_AD198X_DMIX0	0x0100	/* downmix mode: */
					/*  0 = 6-to-4, 1 = 6-to-2 downmix */
#define AC97_AD198X_DMIX1	0x0200	/* downmix mode: 1 = enabled */
#define AC97_AD198X_HPSEL	0x0400	/* headphone amplifier input select */
#define AC97_AD198X_CLDIS	0x0800	/* center/lfe disable */
#define AC97_AD198X_LODIS	0x1000	/* LINE_OUT disable */
#define AC97_AD198X_MSPLT	0x2000	/* mute split */
#define AC97_AD198X_AC97NC	0x4000	/* AC97 no compatible mode */
#define AC97_AD198X_DACZ	0x8000	/* DAC zero-fill mode */

/* MISC 1 bits (AD1986 register 0x76) */
#define AC97_AD1986_MBC		0x0003	/* mic boost */
#define AC97_AD1986_MBC_20	0x0000	/* +20dB */
#define AC97_AD1986_MBC_10	0x0001	/* +10dB */
#define AC97_AD1986_MBC_30	0x0002	/* +30dB */
#define AC97_AD1986_LISEL0	0x0004	/* LINE_IN select bit 0 */
#define AC97_AD1986_LISEL1	0x0008	/* LINE_IN select bit 1 */
#define AC97_AD1986_LISEL_MASK	(AC97_AD1986_LISEL1 | AC97_AD1986_LISEL0)
#define AC97_AD1986_LISEL_LI	0x0000  /* LINE_IN pins as LINE_IN source */
#define AC97_AD1986_LISEL_SURR	0x0004  /* SURROUND pins as LINE_IN source */
#define AC97_AD1986_LISEL_MIC	0x0008  /* MIC_1/2 pins as LINE_IN source */
#define AC97_AD1986_SRU		0x0010	/* sample rate unlock */
#define AC97_AD1986_SOSEL	0x0020	/* SURROUND_OUT amplifiers input sel */
#define AC97_AD1986_2MIC	0x0040	/* 2-channel mic select */
#define AC97_AD1986_SPRD	0x0080	/* SPREAD enable */
#define AC97_AD1986_DMIX0	0x0100	/* downmix mode: */
					/*  0 = 6-to-4, 1 = 6-to-2 downmix */
#define AC97_AD1986_DMIX1	0x0200	/* downmix mode: 1 = enabled */
#define AC97_AD1986_CLDIS	0x0800	/* center/lfe disable */
#define AC97_AD1986_SODIS	0x1000	/* SURROUND_OUT disable */
#define AC97_AD1986_MSPLT	0x2000	/* mute split (read only 1) */
#define AC97_AD1986_AC97NC	0x4000	/* AC97 no compatible mode (r/o 1) */
#define AC97_AD1986_DACZ	0x8000	/* DAC zero-fill mode */

/* MISC 2 bits (AD1986 register 0x70) */
#define AC97_AD_MISC2		0x70	/* Misc Control Bits 2 (AD1986) */

#define AC97_AD1986_CVREF0	0x0004	/* C/LFE VREF_OUT 2.25V */
#define AC97_AD1986_CVREF1	0x0008	/* C/LFE VREF_OUT 0V */
#define AC97_AD1986_CVREF2	0x0010	/* C/LFE VREF_OUT 3.7V */
#define AC97_AD1986_CVREF_MASK \
	(AC97_AD1986_CVREF2 | AC97_AD1986_CVREF1 | AC97_AD1986_CVREF0)
#define AC97_AD1986_JSMAP	0x0020	/* Jack Sense Mapping 1 = alternate */
#define AC97_AD1986_MMDIS	0x0080	/* Mono Mute Disable */
#define AC97_AD1986_MVREF0	0x0400	/* MIC VREF_OUT 2.25V */
#define AC97_AD1986_MVREF1	0x0800	/* MIC VREF_OUT 0V */
#define AC97_AD1986_MVREF2	0x1000	/* MIC VREF_OUT 3.7V */
#define AC97_AD1986_MVREF_MASK \
	(AC97_AD1986_MVREF2 | AC97_AD1986_MVREF1 | AC97_AD1986_MVREF0)

/* MISC 3 bits (AD1986 register 0x7a) */
#define AC97_AD_MISC3		0x7a	/* Misc Control Bits 3 (AD1986) */

#define AC97_AD1986_MMIX	0x0004	/* Mic Mix, left/right */
#define AC97_AD1986_GPO		0x0008	/* General Purpose Out */
#define AC97_AD1986_LOHPEN	0x0010	/* LINE_OUT headphone drive */
#define AC97_AD1986_LVREF0	0x0100	/* LINE_OUT VREF_OUT 2.25V */
#define AC97_AD1986_LVREF1	0x0200	/* LINE_OUT VREF_OUT 0V */
#define AC97_AD1986_LVREF2	0x0400	/* LINE_OUT VREF_OUT 3.7V */
#define AC97_AD1986_LVREF_MASK \
	(AC97_AD1986_LVREF2 | AC97_AD1986_LVREF1 | AC97_AD1986_LVREF0)
#define AC97_AD1986_JSINVA	0x0800	/* Jack Sense Invert SENSE_A */
#define AC97_AD1986_LOSEL	0x1000	/* LINE_OUT amplifiers input select */
#define AC97_AD1986_HPSEL0	0x2000	/* Headphone amplifiers */
					/*   input select Surround DACs */
#define AC97_AD1986_HPSEL1	0x4000	/* Headphone amplifiers input */
					/*   select C/LFE DACs */
#define AC97_AD1986_JSINVB	0x8000	/* Jack Sense Invert SENSE_B */

/* Serial Config bits (AD1986 register 0x74) (incomplete) */
#define AC97_AD1986_OMS0	0x0100	/* Optional Mic Selector bit 0 */
#define AC97_AD1986_OMS1	0x0200	/* Optional Mic Selector bit 1 */
#define AC97_AD1986_OMS2	0x0400	/* Optional Mic Selector bit 2 */
#define AC97_AD1986_OMS_MASK \
	(AC97_AD1986_OMS2 | AC97_AD1986_OMS1 | AC97_AD1986_OMS0)
#define AC97_AD1986_OMS_M	0x0000  /* MIC_1/2 pins are MIC sources */
#define AC97_AD1986_OMS_L	0x0100  /* LINE_IN pins are MIC sources */
#define AC97_AD1986_OMS_C	0x0200  /* Center/LFE pins are MCI sources */
#define AC97_AD1986_OMS_MC	0x0400  /* Mix of MIC and C/LFE pins */
					/*   are MIC sources */
#define AC97_AD1986_OMS_ML	0x0500  /* MIX of MIC and LINE_IN pins */
					/*   are MIC sources */
#define AC97_AD1986_OMS_LC	0x0600  /* MIX of LINE_IN and C/LFE pins */
					/*   are MIC sources */
#define AC97_AD1986_OMS_MLC	0x0700  /* MIX of MIC, LINE_IN, C/LFE pins */
					/*   are MIC sources */


static int snd_ac97_ad198x_spdif_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_ad198x_spdif_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_SERIAL_CFG];
	ucontrol->value.enumerated.item[0] = (val >> 2) & 1;
	return 0;
}

static int snd_ac97_ad198x_spdif_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << 2;
	return snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x0004, val);
}

static const struct snd_kcontrol_new snd_ac97_ad198x_spdif_source = {
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
	.info	= snd_ac97_ad198x_spdif_source_info,
	.get	= snd_ac97_ad198x_spdif_source_get,
	.put	= snd_ac97_ad198x_spdif_source_put,
};

static int patch_ad198x_post_spdif(struct snd_ac97 * ac97)
{
 	return patch_build_controls(ac97, &snd_ac97_ad198x_spdif_source, 1);
}

static const struct snd_kcontrol_new snd_ac97_ad1981x_jack_sense[] = {
	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 11, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0),
};

/* black list to avoid HP/Line jack-sense controls
 * (SS vendor << 16 | device)
 */
static unsigned int ad1981_jacks_blacklist[] = {
	0x10140523, /* Thinkpad R40 */
	0x10140534, /* Thinkpad X31 */
	0x10140537, /* Thinkpad T41p */
	0x1014053e, /* Thinkpad R40e */
	0x10140554, /* Thinkpad T42p/R50p */
	0x10140567, /* Thinkpad T43p 2668-G7U */
	0x10140581, /* Thinkpad X41-2527 */
	0x10280160, /* Dell Dimension 2400 */
	0x104380b0, /* Asus A7V8X-MX */
	0x11790241, /* Toshiba Satellite A-15 S127 */
	0x1179ff10, /* Toshiba P500 */
	0x144dc01a, /* Samsung NP-X20C004/SEG */
	0 /* end */
};

static int check_list(struct snd_ac97 *ac97, const unsigned int *list)
{
	u32 subid = ((u32)ac97->subsystem_vendor << 16) | ac97->subsystem_device;
	for (; *list; list++)
		if (*list == subid)
			return 1;
	return 0;
}

static int patch_ad1981a_specific(struct snd_ac97 * ac97)
{
	if (check_list(ac97, ad1981_jacks_blacklist))
		return 0;
	return patch_build_controls(ac97, snd_ac97_ad1981x_jack_sense,
				    ARRAY_SIZE(snd_ac97_ad1981x_jack_sense));
}

static const struct snd_ac97_build_ops patch_ad1981a_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1981a_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

/* white list to enable HP jack-sense bits
 * (SS vendor << 16 | device)
 */
static unsigned int ad1981_jacks_whitelist[] = {
	0x0e11005a, /* HP nc4000/4010 */
	0x103c0890, /* HP nc6000 */
	0x103c0938, /* HP nc4220 */
	0x103c099c, /* HP nx6110 */
	0x103c0944, /* HP nc6220 */
	0x103c0934, /* HP nc8220 */
	0x103c006d, /* HP nx9105 */
	0x17340088, /* FSC Scenic-W */
	0 /* end */
};

static void check_ad1981_hp_jack_sense(struct snd_ac97 *ac97)
{
	if (check_list(ac97, ad1981_jacks_whitelist))
		/* enable headphone jack sense */
		snd_ac97_update_bits(ac97, AC97_AD_JACK_SPDIF, 1<<11, 1<<11);
}

static int patch_ad1981a(struct snd_ac97 *ac97)
{
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1981a_build_ops;
	snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD198X_MSPLT, AC97_AD198X_MSPLT);
	ac97->flags |= AC97_STEREO_MUTES;
	check_ad1981_hp_jack_sense(ac97);
	return 0;
}

static const struct snd_kcontrol_new snd_ac97_ad198x_2cmic =
AC97_SINGLE("Stereo Mic", AC97_AD_MISC, 6, 1, 0);

static int patch_ad1981b_specific(struct snd_ac97 *ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1)) < 0)
		return err;
	if (check_list(ac97, ad1981_jacks_blacklist))
		return 0;
	return patch_build_controls(ac97, snd_ac97_ad1981x_jack_sense,
				    ARRAY_SIZE(snd_ac97_ad1981x_jack_sense));
}

static const struct snd_ac97_build_ops patch_ad1981b_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1981b_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume
#endif
};

static int patch_ad1981b(struct snd_ac97 *ac97)
{
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1981b_build_ops;
	snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD198X_MSPLT, AC97_AD198X_MSPLT);
	ac97->flags |= AC97_STEREO_MUTES;
	check_ad1981_hp_jack_sense(ac97);
	return 0;
}

#define snd_ac97_ad1888_lohpsel_info	snd_ctl_boolean_mono_info

static int snd_ac97_ad1888_lohpsel_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC];
	ucontrol->value.integer.value[0] = !(val & AC97_AD198X_LOSEL);
	if (ac97->spec.ad18xx.lo_as_master)
		ucontrol->value.integer.value[0] =
			!ucontrol->value.integer.value[0];
	return 0;
}

static int snd_ac97_ad1888_lohpsel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = !ucontrol->value.integer.value[0];
	if (ac97->spec.ad18xx.lo_as_master)
		val = !val;
	val = val ? (AC97_AD198X_LOSEL | AC97_AD198X_HPSEL) : 0;
	return snd_ac97_update_bits(ac97, AC97_AD_MISC,
				    AC97_AD198X_LOSEL | AC97_AD198X_HPSEL, val);
}

static int snd_ac97_ad1888_downmix_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_ad1888_downmix_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC];
	if (!(val & AC97_AD198X_DMIX1))
		ucontrol->value.enumerated.item[0] = 0;
	else
		ucontrol->value.enumerated.item[0] = 1 + ((val >> 8) & 1);
	return 0;
}

static int snd_ac97_ad1888_downmix_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
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

static void ad1888_update_jacks(struct snd_ac97 *ac97)
{
	unsigned short val = 0;
	/* clear LODIS if shared jack is to be used for Surround out */
	if (!ac97->spec.ad18xx.lo_as_master && is_shared_linein(ac97))
		val |= (1 << 12);
	/* clear CLDIS if shared jack is to be used for C/LFE out */
	if (is_shared_micin(ac97))
		val |= (1 << 11);
	/* shared Line-In */
	snd_ac97_update_bits(ac97, AC97_AD_MISC, (1 << 11) | (1 << 12), val);
}

static const struct snd_kcontrol_new snd_ac97_ad1888_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Exchange Front/Surround",
		.info = snd_ac97_ad1888_lohpsel_info,
		.get = snd_ac97_ad1888_lohpsel_get,
		.put = snd_ac97_ad1888_lohpsel_put
	},
	AC97_SINGLE("V_REFOUT Enable", AC97_AD_MISC, AC97_AD_VREFD_SHIFT, 1, 1),
	AC97_SINGLE("High Pass Filter Enable", AC97_AD_TEST2,
			AC97_AD_HPFD_SHIFT, 1, 1),
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

static int patch_ad1888_specific(struct snd_ac97 *ac97)
{
	if (!ac97->spec.ad18xx.lo_as_master) {
		/* rename 0x04 as "Master" and 0x02 as "Master Surround" */
		snd_ac97_rename_vol_ctl(ac97, "Master Playback",
					"Master Surround Playback");
		snd_ac97_rename_vol_ctl(ac97, "Headphone Playback",
					"Master Playback");
	}
	return patch_build_controls(ac97, snd_ac97_ad1888_controls, ARRAY_SIZE(snd_ac97_ad1888_controls));
}

static const struct snd_ac97_build_ops patch_ad1888_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1888_specific,
#ifdef CONFIG_PM
	.resume = ad1888_resume,
#endif
	.update_jacks = ad1888_update_jacks,
};

static int patch_ad1888(struct snd_ac97 * ac97)
{
	unsigned short misc;
	
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1888_build_ops;

	/*
	 * LO can be used as a real line-out on some devices,
	 * and we need to revert the front/surround mixer switches
	 */
	if (ac97->subsystem_vendor == 0x1043 &&
	    ac97->subsystem_device == 0x1193) /* ASUS A9T laptop */
		ac97->spec.ad18xx.lo_as_master = 1;

	misc = snd_ac97_read(ac97, AC97_AD_MISC);
	/* AD-compatible mode */
	/* Stereo mutes enabled */
	misc |= AC97_AD198X_MSPLT | AC97_AD198X_AC97NC;
	if (!ac97->spec.ad18xx.lo_as_master)
		/* Switch FRONT/SURROUND LINE-OUT/HP-OUT default connection */
		/* it seems that most vendors connect line-out connector to
		 * headphone out of AC'97
		 */
		misc |= AC97_AD198X_LOSEL | AC97_AD198X_HPSEL;

	snd_ac97_write_cache(ac97, AC97_AD_MISC, misc);
	ac97->flags |= AC97_STEREO_MUTES;
	return 0;
}

static int patch_ad1980_specific(struct snd_ac97 *ac97)
{
	int err;

	if ((err = patch_ad1888_specific(ac97)) < 0)
		return err;
	return patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1);
}

static const struct snd_ac97_build_ops patch_ad1980_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1980_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1888_update_jacks,
};

static int patch_ad1980(struct snd_ac97 * ac97)
{
	patch_ad1888(ac97);
	ac97->build_ops = &patch_ad1980_build_ops;
	return 0;
}

static int snd_ac97_ad1985_vrefout_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {"High-Z", "3.7 V", "2.25 V", "0 V"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_ad1985_vrefout_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	static const int reg2ctrl[4] = {2, 0, 1, 3};
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	val = (ac97->regs[AC97_AD_MISC] & AC97_AD198X_VREF_MASK)
	      >> AC97_AD198X_VREF_SHIFT;
	ucontrol->value.enumerated.item[0] = reg2ctrl[val];
	return 0;
}

static int snd_ac97_ad1985_vrefout_put(struct snd_kcontrol *kcontrol, 
				       struct snd_ctl_elem_value *ucontrol)
{
	static const int ctrl2reg[4] = {1, 2, 0, 3};
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	if (ucontrol->value.enumerated.item[0] > 3)
		return -EINVAL;
	val = ctrl2reg[ucontrol->value.enumerated.item[0]]
	      << AC97_AD198X_VREF_SHIFT;
	return snd_ac97_update_bits(ac97, AC97_AD_MISC,
				    AC97_AD198X_VREF_MASK, val);
}

static const struct snd_kcontrol_new snd_ac97_ad1985_controls[] = {
	AC97_SINGLE("Exchange Center/LFE", AC97_AD_SERIAL_CFG, 3, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Exchange Front/Surround",
		.info = snd_ac97_ad1888_lohpsel_info,
		.get = snd_ac97_ad1888_lohpsel_get,
		.put = snd_ac97_ad1888_lohpsel_put
	},
	AC97_SINGLE("High Pass Filter Enable", AC97_AD_TEST2, 12, 1, 1),
	AC97_SINGLE("Spread Front to Surround and Center/LFE",
		    AC97_AD_MISC, 7, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Downmix",
		.info = snd_ac97_ad1888_downmix_info,
		.get = snd_ac97_ad1888_downmix_get,
		.put = snd_ac97_ad1888_downmix_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "V_REFOUT",
		.info = snd_ac97_ad1985_vrefout_info,
		.get = snd_ac97_ad1985_vrefout_get,
		.put = snd_ac97_ad1985_vrefout_put
	},
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,

	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 10, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0),
};

static void ad1985_update_jacks(struct snd_ac97 *ac97)
{
	ad1888_update_jacks(ac97);
	/* clear OMS if shared jack is to be used for C/LFE out */
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 1 << 9,
			     is_shared_micin(ac97) ? 1 << 9 : 0);
}

static int patch_ad1985_specific(struct snd_ac97 *ac97)
{
	int err;

	/* rename 0x04 as "Master" and 0x02 as "Master Surround" */
	snd_ac97_rename_vol_ctl(ac97, "Master Playback",
				"Master Surround Playback");
	snd_ac97_rename_vol_ctl(ac97, "Headphone Playback", "Master Playback");

	if ((err = patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1)) < 0)
		return err;

	return patch_build_controls(ac97, snd_ac97_ad1985_controls,
				    ARRAY_SIZE(snd_ac97_ad1985_controls));
}

static const struct snd_ac97_build_ops patch_ad1985_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1985_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1985_update_jacks,
};

static int patch_ad1985(struct snd_ac97 * ac97)
{
	unsigned short misc;
	
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1985_build_ops;
	misc = snd_ac97_read(ac97, AC97_AD_MISC);
	/* switch front/surround line-out/hp-out */
	/* AD-compatible mode */
	/* Stereo mutes enabled */
	snd_ac97_write_cache(ac97, AC97_AD_MISC, misc |
			     AC97_AD198X_LOSEL |
			     AC97_AD198X_HPSEL |
			     AC97_AD198X_MSPLT |
			     AC97_AD198X_AC97NC);
	ac97->flags |= AC97_STEREO_MUTES;

	/* update current jack configuration */
	ad1985_update_jacks(ac97);

	/* on AD1985 rev. 3, AC'97 revision bits are zero */
	ac97->ext_id = (ac97->ext_id & ~AC97_EI_REV_MASK) | AC97_EI_REV_23;
	return 0;
}

#define snd_ac97_ad1986_bool_info	snd_ctl_boolean_mono_info

static int snd_ac97_ad1986_lososel_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC3];
	ucontrol->value.integer.value[0] = (val & AC97_AD1986_LOSEL) != 0;
	return 0;
}

static int snd_ac97_ad1986_lososel_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int ret0;
	int ret1;
	int sprd = (ac97->regs[AC97_AD_MISC] & AC97_AD1986_SPRD) != 0;

	ret0 = snd_ac97_update_bits(ac97, AC97_AD_MISC3, AC97_AD1986_LOSEL,
					ucontrol->value.integer.value[0] != 0
				    ? AC97_AD1986_LOSEL : 0);
	if (ret0 < 0)
		return ret0;

	/* SOSEL is set to values of "Spread" or "Exchange F/S" controls */
	ret1 = snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD1986_SOSEL,
				    (ucontrol->value.integer.value[0] != 0
				     || sprd)
				    ? AC97_AD1986_SOSEL : 0);
	if (ret1 < 0)
		return ret1;

	return (ret0 > 0 || ret1 > 0) ? 1 : 0;
}

static int snd_ac97_ad1986_spread_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_AD_MISC];
	ucontrol->value.integer.value[0] = (val & AC97_AD1986_SPRD) != 0;
	return 0;
}

static int snd_ac97_ad1986_spread_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	int ret0;
	int ret1;
	int sprd = (ac97->regs[AC97_AD_MISC3] & AC97_AD1986_LOSEL) != 0;

	ret0 = snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD1986_SPRD,
					ucontrol->value.integer.value[0] != 0
				    ? AC97_AD1986_SPRD : 0);
	if (ret0 < 0)
		return ret0;

	/* SOSEL is set to values of "Spread" or "Exchange F/S" controls */
	ret1 = snd_ac97_update_bits(ac97, AC97_AD_MISC, AC97_AD1986_SOSEL,
				    (ucontrol->value.integer.value[0] != 0
				     || sprd)
				    ? AC97_AD1986_SOSEL : 0);
	if (ret1 < 0)
		return ret1;

	return (ret0 > 0 || ret1 > 0) ? 1 : 0;
}

static int snd_ac97_ad1986_miclisel_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = ac97->spec.ad18xx.swap_mic_linein;
	return 0;
}

static int snd_ac97_ad1986_miclisel_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned char swap = ucontrol->value.integer.value[0] != 0;

	if (swap != ac97->spec.ad18xx.swap_mic_linein) {
		ac97->spec.ad18xx.swap_mic_linein = swap;
		if (ac97->build_ops->update_jacks)
			ac97->build_ops->update_jacks(ac97);
		return 1;
	}
	return 0;
}

static int snd_ac97_ad1986_vrefout_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	/* Use MIC_1/2 V_REFOUT as the "get" value */
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	unsigned short reg = ac97->regs[AC97_AD_MISC2];
	if ((reg & AC97_AD1986_MVREF0) != 0)
		val = 2;
	else if ((reg & AC97_AD1986_MVREF1) != 0)
		val = 3;
	else if ((reg & AC97_AD1986_MVREF2) != 0)
		val = 1;
	else
		val = 0;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_ad1986_vrefout_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short cval;
	unsigned short lval;
	unsigned short mval;
	int cret;
	int lret;
	int mret;

	switch (ucontrol->value.enumerated.item[0])
	{
	case 0: /* High-Z */
		cval = 0;
		lval = 0;
		mval = 0;
		break;
	case 1: /* 3.7 V */
		cval = AC97_AD1986_CVREF2;
		lval = AC97_AD1986_LVREF2;
		mval = AC97_AD1986_MVREF2;
		break;
	case 2: /* 2.25 V */
		cval = AC97_AD1986_CVREF0;
		lval = AC97_AD1986_LVREF0;
		mval = AC97_AD1986_MVREF0;
		break;
	case 3: /* 0 V */
		cval = AC97_AD1986_CVREF1;
		lval = AC97_AD1986_LVREF1;
		mval = AC97_AD1986_MVREF1;
		break;
	default:
		return -EINVAL;
	}

	cret = snd_ac97_update_bits(ac97, AC97_AD_MISC2,
				    AC97_AD1986_CVREF_MASK, cval);
	if (cret < 0)
		return cret;
	lret = snd_ac97_update_bits(ac97, AC97_AD_MISC3,
				    AC97_AD1986_LVREF_MASK, lval);
	if (lret < 0)
		return lret;
	mret = snd_ac97_update_bits(ac97, AC97_AD_MISC2,
				    AC97_AD1986_MVREF_MASK, mval);
	if (mret < 0)
		return mret;

	return (cret > 0 || lret > 0 || mret > 0) ? 1 : 0;
}

static const struct snd_kcontrol_new snd_ac97_ad1986_controls[] = {
	AC97_SINGLE("Exchange Center/LFE", AC97_AD_SERIAL_CFG, 3, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Exchange Front/Surround",
		.info = snd_ac97_ad1986_bool_info,
		.get = snd_ac97_ad1986_lososel_get,
		.put = snd_ac97_ad1986_lososel_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Exchange Mic/Line In",
		.info = snd_ac97_ad1986_bool_info,
		.get = snd_ac97_ad1986_miclisel_get,
		.put = snd_ac97_ad1986_miclisel_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Spread Front to Surround and Center/LFE",
		.info = snd_ac97_ad1986_bool_info,
		.get = snd_ac97_ad1986_spread_get,
		.put = snd_ac97_ad1986_spread_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Downmix",
		.info = snd_ac97_ad1888_downmix_info,
		.get = snd_ac97_ad1888_downmix_get,
		.put = snd_ac97_ad1888_downmix_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "V_REFOUT",
		.info = snd_ac97_ad1985_vrefout_info,
		.get = snd_ac97_ad1986_vrefout_get,
		.put = snd_ac97_ad1986_vrefout_put
	},
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,

	AC97_SINGLE("Headphone Jack Sense", AC97_AD_JACK_SPDIF, 10, 1, 0),
	AC97_SINGLE("Line Jack Sense", AC97_AD_JACK_SPDIF, 12, 1, 0)
};

static void ad1986_update_jacks(struct snd_ac97 *ac97)
{
	unsigned short misc_val = 0;
	unsigned short ser_val;

	/* disable SURROUND and CENTER/LFE if not surround mode */
	if (!is_surround_on(ac97))
		misc_val |= AC97_AD1986_SODIS;
	if (!is_clfe_on(ac97))
		misc_val |= AC97_AD1986_CLDIS;

	/* select line input (default=LINE_IN, SURROUND or MIC_1/2) */
	if (is_shared_linein(ac97))
		misc_val |= AC97_AD1986_LISEL_SURR;
	else if (ac97->spec.ad18xx.swap_mic_linein != 0)
		misc_val |= AC97_AD1986_LISEL_MIC;
	snd_ac97_update_bits(ac97, AC97_AD_MISC,
			     AC97_AD1986_SODIS | AC97_AD1986_CLDIS |
			     AC97_AD1986_LISEL_MASK,
			     misc_val);

	/* select microphone input (MIC_1/2, Center/LFE or LINE_IN) */
	if (is_shared_micin(ac97))
		ser_val = AC97_AD1986_OMS_C;
	else if (ac97->spec.ad18xx.swap_mic_linein != 0)
		ser_val = AC97_AD1986_OMS_L;
	else
		ser_val = AC97_AD1986_OMS_M;
	snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG,
			     AC97_AD1986_OMS_MASK,
			     ser_val);
}

static int patch_ad1986_specific(struct snd_ac97 *ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, &snd_ac97_ad198x_2cmic, 1)) < 0)
		return err;

	return patch_build_controls(ac97, snd_ac97_ad1986_controls,
				    ARRAY_SIZE(snd_ac97_ad1985_controls));
}

static const struct snd_ac97_build_ops patch_ad1986_build_ops = {
	.build_post_spdif = patch_ad198x_post_spdif,
	.build_specific = patch_ad1986_specific,
#ifdef CONFIG_PM
	.resume = ad18xx_resume,
#endif
	.update_jacks = ad1986_update_jacks,
};

static int patch_ad1986(struct snd_ac97 * ac97)
{
	patch_ad1881(ac97);
	ac97->build_ops = &patch_ad1986_build_ops;
	ac97->flags |= AC97_STEREO_MUTES;

	/* update current jack configuration */
	ad1986_update_jacks(ac97);

	return 0;
}

/*
 * realtek ALC203: use mono-out for pin 37
 */
static int patch_alc203(struct snd_ac97 *ac97)
{
	snd_ac97_update_bits(ac97, 0x7a, 0x400, 0x400);
	return 0;
}

/*
 * realtek ALC65x/850 codecs
 */
static void alc650_update_jacks(struct snd_ac97 *ac97)
{
	int shared;
	
	/* shared Line-In / Surround Out */
	shared = is_shared_surrout(ac97);
	snd_ac97_update_bits(ac97, AC97_ALC650_MULTICH, 1 << 9,
			     shared ? (1 << 9) : 0);
	/* update shared Mic In / Center/LFE Out */
	shared = is_shared_clfeout(ac97);
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

static const struct snd_kcontrol_new snd_ac97_controls_alc650[] = {
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

static const struct snd_kcontrol_new snd_ac97_spdif_controls_alc650[] = {
        AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_ALC650_MULTICH, 11, 1, 0),
        AC97_SINGLE("Analog to IEC958 Output", AC97_ALC650_MULTICH, 12, 1, 0),
	/* disable this controls since it doesn't work as expected */
	/* AC97_SINGLE("IEC958 Input Monitor", AC97_ALC650_MULTICH, 13, 1, 0), */
};

static const DECLARE_TLV_DB_SCALE(db_scale_5bit_3db_max, -4350, 150, 0);

static int patch_alc650_specific(struct snd_ac97 * ac97)
{
	int err;

	if ((err = patch_build_controls(ac97, snd_ac97_controls_alc650, ARRAY_SIZE(snd_ac97_controls_alc650))) < 0)
		return err;
	if (ac97->ext_id & AC97_EI_SPDIF) {
		if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_alc650, ARRAY_SIZE(snd_ac97_spdif_controls_alc650))) < 0)
			return err;
	}
	if (ac97->id != AC97_ID_ALC650F)
		reset_tlv(ac97, "Master Playback Volume",
			  db_scale_5bit_3db_max);
	return 0;
}

static const struct snd_ac97_build_ops patch_alc650_ops = {
	.build_specific	= patch_alc650_specific,
	.update_jacks = alc650_update_jacks
};

static int patch_alc650(struct snd_ac97 * ac97)
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
	if (ac97->spec.dev_flags &&
	    /* ASUS A6KM requires EAPD */
	    ! (ac97->subsystem_vendor == 0x1043 &&
	       ac97->subsystem_device == 0x1103))
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

static void alc655_update_jacks(struct snd_ac97 *ac97)
{
	int shared;
	
	/* shared Line-In / Surround Out */
	shared = is_shared_surrout(ac97);
	ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 1 << 9,
			      shared ? (1 << 9) : 0, 0);
	/* update shared Mic In / Center/LFE Out */
	shared = is_shared_clfeout(ac97);
	/* misc control; vrefout disable */
	snd_ac97_update_bits(ac97, AC97_ALC650_CLOCK, 1 << 12,
			     shared ? (1 << 12) : 0);
	ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 1 << 10,
			      shared ? (1 << 10) : 0, 0);
}

static const struct snd_kcontrol_new snd_ac97_controls_alc655[] = {
	AC97_PAGE_SINGLE("Duplicate Front", AC97_ALC650_MULTICH, 0, 1, 0, 0),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int alc655_iec958_route_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts_655[3] = { "PCM", "Analog In", "IEC958 In" };
	static char *texts_658[4] = { "PCM", "Analog1 In", "Analog2 In", "IEC958 In" };
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

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

static int alc655_iec958_route_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_ALC650_MULTICH];
	val = (val >> 12) & 3;
	if (ac97->spec.dev_flags && val == 3)
		val = 0;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int alc655_iec958_route_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	return ac97_update_bits_page(ac97, AC97_ALC650_MULTICH, 3 << 12,
				     (unsigned short)ucontrol->value.enumerated.item[0] << 12,
				     0);
}

static const struct snd_kcontrol_new snd_ac97_spdif_controls_alc655[] = {
        AC97_PAGE_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), AC97_ALC650_MULTICH, 11, 1, 0, 0),
	/* disable this controls since it doesn't work as expected */
        /* AC97_PAGE_SINGLE("IEC958 Input Monitor", AC97_ALC650_MULTICH, 14, 1, 0, 0), */
	{
		.iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name   = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info   = alc655_iec958_route_info,
		.get    = alc655_iec958_route_get,
		.put    = alc655_iec958_route_put,
	},
};

static int patch_alc655_specific(struct snd_ac97 * ac97)
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

static const struct snd_ac97_build_ops patch_alc655_ops = {
	.build_specific	= patch_alc655_specific,
	.update_jacks = alc655_update_jacks
};

static int patch_alc655(struct snd_ac97 * ac97)
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
		    (ac97->subsystem_device == 0x0131 || /* MSI S270 laptop */
		     ac97->subsystem_device == 0x0161 || /* LG K1 Express */
		     ac97->subsystem_device == 0x0351 || /* MSI L725 laptop */
		     ac97->subsystem_device == 0x0471 || /* MSI L720 laptop */
		     ac97->subsystem_device == 0x0061))  /* MSI S250 laptop */
			val &= ~(1 << 1); /* Pin 47 is EAPD (for internal speaker) */
		else
			val |= (1 << 1); /* Pin 47 is spdif input pin */
		/* this seems missing on some hardwares */
		ac97->ext_id |= AC97_EI_SPDIF;
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
#define AC97_ALC850_MULTICH    0x6a

static void alc850_update_jacks(struct snd_ac97 *ac97)
{
	int shared;
	int aux_is_back_surround;
	
	/* shared Line-In / Surround Out */
	shared = is_shared_surrout(ac97);
	/* SURR 1kOhm (bit4), Amp (bit5) */
	snd_ac97_update_bits(ac97, AC97_ALC850_MISC1, (1<<4)|(1<<5),
			     shared ? (1<<5) : (1<<4));
	/* LINE-IN = 0, SURROUND = 2 */
	snd_ac97_update_bits(ac97, AC97_ALC850_JACK_SELECT, 7 << 12,
			     shared ? (2<<12) : (0<<12));
	/* update shared Mic In / Center/LFE Out */
	shared = is_shared_clfeout(ac97);
	/* Vref disable (bit12), 1kOhm (bit13) */
	snd_ac97_update_bits(ac97, AC97_ALC850_MISC1, (1<<12)|(1<<13),
			     shared ? (1<<12) : (1<<13));
	/* MIC-IN = 1, CENTER-LFE = 5 */
	snd_ac97_update_bits(ac97, AC97_ALC850_JACK_SELECT, 7 << 4,
			     shared ? (5<<4) : (1<<4));

	aux_is_back_surround = alc850_is_aux_back_surround(ac97);
	/* Aux is Back Surround */
	snd_ac97_update_bits(ac97, AC97_ALC850_MULTICH, 1 << 10,
				 aux_is_back_surround ? (1<<10) : (0<<10));
}

static const struct snd_kcontrol_new snd_ac97_controls_alc850[] = {
	AC97_PAGE_SINGLE("Duplicate Front", AC97_ALC650_MULTICH, 0, 1, 0, 0),
	AC97_SINGLE("Mic Front Input Switch", AC97_ALC850_JACK_SELECT, 15, 1, 1),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_8CH_CTL,
};

static int patch_alc850_specific(struct snd_ac97 *ac97)
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

static const struct snd_ac97_build_ops patch_alc850_ops = {
	.build_specific	= patch_alc850_specific,
	.update_jacks = alc850_update_jacks
};

static int patch_alc850(struct snd_ac97 *ac97)
{
	ac97->build_ops = &patch_alc850_ops;

	ac97->spec.dev_flags = 0; /* for IEC958 playback route - ALC655 compatible */
	ac97->flags |= AC97_HAS_8CH;

	/* assume only page 0 for writing cache */
	snd_ac97_update_bits(ac97, AC97_INT_PAGING, AC97_PAGE_MASK, AC97_PAGE_VENDOR);

	/* adjust default values */
	/* set default: spdif-in enabled,
	   spdif-in monitor off, spdif-in PCM off
	   center on mic off, surround on line-in off
	   duplicate front off
	   NB default bit 10=0 = Aux is Capture, not Back Surround
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
static void cm9738_update_jacks(struct snd_ac97 *ac97)
{
	/* shared Line-In / Surround Out */
	snd_ac97_update_bits(ac97, AC97_CM9738_VENDOR_CTRL, 1 << 10,
			     is_shared_surrout(ac97) ? (1 << 10) : 0);
}

static const struct snd_kcontrol_new snd_ac97_cm9738_controls[] = {
	AC97_SINGLE("Duplicate Front", AC97_CM9738_VENDOR_CTRL, 13, 1, 0),
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_4CH_CTL,
};

static int patch_cm9738_specific(struct snd_ac97 * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9738_controls, ARRAY_SIZE(snd_ac97_cm9738_controls));
}

static const struct snd_ac97_build_ops patch_cm9738_ops = {
	.build_specific	= patch_cm9738_specific,
	.update_jacks = cm9738_update_jacks
};

static int patch_cm9738(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_cm9738_ops;
	/* FIXME: can anyone confirm below? */
	/* CM9738 has no PCM volume although the register reacts */
	ac97->flags |= AC97_HAS_NO_PCM_VOL;
	snd_ac97_write_cache(ac97, AC97_PCM, 0x8000);

	return 0;
}

static int snd_ac97_cmedia_spdif_playback_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int snd_ac97_cmedia_spdif_playback_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	val = ac97->regs[AC97_CM9739_SPDIF_CTRL];
	ucontrol->value.enumerated.item[0] = (val >> 1) & 0x01;
	return 0;
}

static int snd_ac97_cmedia_spdif_playback_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	return snd_ac97_update_bits(ac97, AC97_CM9739_SPDIF_CTRL,
				    0x01 << 1, 
				    (ucontrol->value.enumerated.item[0] & 0x01) << 1);
}

static const struct snd_kcontrol_new snd_ac97_cm9739_controls_spdif[] = {
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

static void cm9739_update_jacks(struct snd_ac97 *ac97)
{
	/* shared Line-In / Surround Out */
	snd_ac97_update_bits(ac97, AC97_CM9739_MULTI_CHAN, 1 << 10,
			     is_shared_surrout(ac97) ? (1 << 10) : 0);
	/* shared Mic In / Center/LFE Out **/
	snd_ac97_update_bits(ac97, AC97_CM9739_MULTI_CHAN, 0x3000,
			     is_shared_clfeout(ac97) ? 0x1000 : 0x2000);
}

static const struct snd_kcontrol_new snd_ac97_cm9739_controls[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int patch_cm9739_specific(struct snd_ac97 * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9739_controls, ARRAY_SIZE(snd_ac97_cm9739_controls));
}

static int patch_cm9739_post_spdif(struct snd_ac97 * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9739_controls_spdif, ARRAY_SIZE(snd_ac97_cm9739_controls_spdif));
}

static const struct snd_ac97_build_ops patch_cm9739_ops = {
	.build_specific	= patch_cm9739_specific,
	.build_post_spdif = patch_cm9739_post_spdif,
	.update_jacks = cm9739_update_jacks
};

static int patch_cm9739(struct snd_ac97 * ac97)
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

static void cm9761_update_jacks(struct snd_ac97 *ac97)
{
	/* FIXME: check the bits for each model
	 *        model 83 is confirmed to work
	 */
	static unsigned short surr_on[3][2] = {
		{ 0x0008, 0x0000 }, /* 9761-78 & 82 */
		{ 0x0000, 0x0008 }, /* 9761-82 rev.B */
		{ 0x0000, 0x0008 }, /* 9761-83 */
	};
	static unsigned short clfe_on[3][2] = {
		{ 0x0000, 0x1000 }, /* 9761-78 & 82 */
		{ 0x1000, 0x0000 }, /* 9761-82 rev.B */
		{ 0x0000, 0x1000 }, /* 9761-83 */
	};
	static unsigned short surr_shared[3][2] = {
		{ 0x0000, 0x0400 }, /* 9761-78 & 82 */
		{ 0x0000, 0x0400 }, /* 9761-82 rev.B */
		{ 0x0000, 0x0400 }, /* 9761-83 */
	};
	static unsigned short clfe_shared[3][2] = {
		{ 0x2000, 0x0880 }, /* 9761-78 & 82 */
		{ 0x0000, 0x2880 }, /* 9761-82 rev.B */
		{ 0x2000, 0x0800 }, /* 9761-83 */
	};
	unsigned short val = 0;

	val |= surr_on[ac97->spec.dev_flags][is_surround_on(ac97)];
	val |= clfe_on[ac97->spec.dev_flags][is_clfe_on(ac97)];
	val |= surr_shared[ac97->spec.dev_flags][is_shared_surrout(ac97)];
	val |= clfe_shared[ac97->spec.dev_flags][is_shared_clfeout(ac97)];

	snd_ac97_update_bits(ac97, AC97_CM9761_MULTI_CHAN, 0x3c88, val);
}

static const struct snd_kcontrol_new snd_ac97_cm9761_controls[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static int cm9761_spdif_out_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
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

static int cm9761_spdif_out_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	if (ac97->regs[AC97_CM9761_FUNC] & 0x1)
		ucontrol->value.enumerated.item[0] = 2; /* SPDIF-loopback */
	else if (ac97->regs[AC97_CM9761_SPDIF_CTRL] & 0x2)
		ucontrol->value.enumerated.item[0] = 1; /* ADC loopback */
	else
		ucontrol->value.enumerated.item[0] = 0; /* AC-link */
	return 0;
}

static int cm9761_spdif_out_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ac97 *ac97 = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.enumerated.item[0] == 2)
		return snd_ac97_update_bits(ac97, AC97_CM9761_FUNC, 0x1, 0x1);
	snd_ac97_update_bits(ac97, AC97_CM9761_FUNC, 0x1, 0);
	return snd_ac97_update_bits(ac97, AC97_CM9761_SPDIF_CTRL, 0x2,
				    ucontrol->value.enumerated.item[0] == 1 ? 0x2 : 0);
}

static const char *cm9761_dac_clock[] = { "AC-Link", "SPDIF-In", "Both" };
static const struct ac97_enum cm9761_dac_clock_enum =
	AC97_ENUM_SINGLE(AC97_CM9761_SPDIF_CTRL, 9, 3, cm9761_dac_clock);

static const struct snd_kcontrol_new snd_ac97_cm9761_controls_spdif[] = {
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

static int patch_cm9761_post_spdif(struct snd_ac97 * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9761_controls_spdif, ARRAY_SIZE(snd_ac97_cm9761_controls_spdif));
}

static int patch_cm9761_specific(struct snd_ac97 * ac97)
{
	return patch_build_controls(ac97, snd_ac97_cm9761_controls, ARRAY_SIZE(snd_ac97_cm9761_controls));
}

static const struct snd_ac97_build_ops patch_cm9761_ops = {
	.build_specific	= patch_cm9761_specific,
	.build_post_spdif = patch_cm9761_post_spdif,
	.update_jacks = cm9761_update_jacks
};

static int patch_cm9761(struct snd_ac97 *ac97)
{
	unsigned short val;

	/* CM9761 has no PCM volume although the register reacts */
	/* Master volume seems to have _some_ influence on the analog
	 * input sounds
	 */
	ac97->flags |= /*AC97_HAS_NO_MASTER_VOL |*/ AC97_HAS_NO_PCM_VOL;
	snd_ac97_write_cache(ac97, AC97_MASTER, 0x8808);
	snd_ac97_write_cache(ac97, AC97_PCM, 0x8808);

	ac97->spec.dev_flags = 0; /* 1 = model 82 revision B, 2 = model 83 */
	if (ac97->id == AC97_ID_CM9761_82) {
		unsigned short tmp;
		/* check page 1, reg 0x60 */
		val = snd_ac97_read(ac97, AC97_INT_PAGING);
		snd_ac97_write_cache(ac97, AC97_INT_PAGING, (val & ~0x0f) | 0x01);
		tmp = snd_ac97_read(ac97, 0x60);
		ac97->spec.dev_flags = tmp & 1; /* revision B? */
		snd_ac97_write_cache(ac97, AC97_INT_PAGING, val);
	} else if (ac97->id == AC97_ID_CM9761_83)
		ac97->spec.dev_flags = 2;

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
static const struct snd_kcontrol_new cm9780_controls[] = {
	AC97_DOUBLE("Side Playback Switch", AC97_CM9780_SIDE, 15, 7, 1, 1),
	AC97_DOUBLE("Side Playback Volume", AC97_CM9780_SIDE, 8, 0, 31, 0),
	AC97_ENUM("Side Playback Route", cm9780_ch_select_enum),
};

static int patch_cm9780_specific(struct snd_ac97 *ac97)
{
	return patch_build_controls(ac97, cm9780_controls, ARRAY_SIZE(cm9780_controls));
}

static const struct snd_ac97_build_ops patch_cm9780_ops = {
	.build_specific	= patch_cm9780_specific,
	.build_post_spdif = patch_cm9761_post_spdif	/* identical with CM9761 */
};

static int patch_cm9780(struct snd_ac97 *ac97)
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
static const struct snd_kcontrol_new snd_ac97_controls_vt1616[] = {
AC97_SINGLE("DC Offset removal", 0x5a, 10, 1, 0),
AC97_SINGLE("Alternate Level to Surround Out", 0x5a, 15, 1, 0),
AC97_SINGLE("Downmix LFE and Center to Front", 0x5a, 12, 1, 0),
AC97_SINGLE("Downmix Surround to Front", 0x5a, 11, 1, 0),
};

static const char *slave_vols_vt1616[] = {
	"Front Playback Volume",
	"Surround Playback Volume",
	"Center Playback Volume",
	"LFE Playback Volume",
	NULL
};

static const char *slave_sws_vt1616[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	NULL
};

/* find a mixer control element with the given name */
static struct snd_kcontrol *snd_ac97_find_mixer_ctl(struct snd_ac97 *ac97,
						    const char *name)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, name);
	return snd_ctl_find_id(ac97->bus->card, &id);
}

/* create a virtual master control and add slaves */
static int snd_ac97_add_vmaster(struct snd_ac97 *ac97, char *name,
				const unsigned int *tlv, const char **slaves)
{
	struct snd_kcontrol *kctl;
	const char **s;
	int err;

	kctl = snd_ctl_make_virtual_master(name, tlv);
	if (!kctl)
		return -ENOMEM;
	err = snd_ctl_add(ac97->bus->card, kctl);
	if (err < 0)
		return err;

	for (s = slaves; *s; s++) {
		struct snd_kcontrol *sctl;

		sctl = snd_ac97_find_mixer_ctl(ac97, *s);
		if (!sctl) {
			snd_printdd("Cannot find slave %s, skipped\n", *s);
			continue;
		}
		err = snd_ctl_add_slave(kctl, sctl);
		if (err < 0)
			return err;
	}
	return 0;
}

static int patch_vt1616_specific(struct snd_ac97 * ac97)
{
	struct snd_kcontrol *kctl;
	int err;

	if (snd_ac97_try_bit(ac97, 0x5a, 9))
		if ((err = patch_build_controls(ac97, &snd_ac97_controls_vt1616[0], 1)) < 0)
			return err;
	if ((err = patch_build_controls(ac97, &snd_ac97_controls_vt1616[1], ARRAY_SIZE(snd_ac97_controls_vt1616) - 1)) < 0)
		return err;

	/* There is already a misnamed master switch.  Rename it.  */
	kctl = snd_ac97_find_mixer_ctl(ac97, "Master Playback Volume");
	if (!kctl)
		return -EINVAL;

	snd_ac97_rename_vol_ctl(ac97, "Master Playback", "Front Playback");

	err = snd_ac97_add_vmaster(ac97, "Master Playback Volume",
				   kctl->tlv.p, slave_vols_vt1616);
	if (err < 0)
		return err;

	err = snd_ac97_add_vmaster(ac97, "Master Playback Switch",
				   NULL, slave_sws_vt1616);
	if (err < 0)
		return err;

	return 0;
}

static const struct snd_ac97_build_ops patch_vt1616_ops = {
	.build_specific	= patch_vt1616_specific
};

static int patch_vt1616(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_vt1616_ops;
	return 0;
}

/*
 * VT1617A codec
 */

/*
 * unfortunately, the vt1617a stashes the twiddlers required for
 * noodling the i/o jacks on 2 different regs. that means that we can't
 * use the easy way provided by AC97_ENUM_DOUBLE() we have to write
 * are own funcs.
 *
 * NB: this is absolutely and utterly different from the vt1618. dunno
 * about the 1616.
 */

/* copied from ac97_surround_jack_mode_info() */
static int snd_ac97_vt1617a_smart51_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	/* ordering in this list reflects vt1617a docs for Reg 20 and
	 * 7a and Table 6 that lays out the matrix NB WRT Table6: SM51
	 * is SM51EN *AND* it's Bit14, not Bit15 so the table is very
	 * counter-intuitive */ 

	static const char* texts[] = { "LineIn Mic1", "LineIn Mic1 Mic3",
				       "Surr LFE/C Mic3", "LineIn LFE/C Mic3",
				       "LineIn Mic2", "LineIn Mic2 Mic1",
				       "Surr LFE Mic1", "Surr LFE Mic1 Mic2"};
	return ac97_enum_text_info(kcontrol, uinfo, texts, 8);
}

static int snd_ac97_vt1617a_smart51_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	ushort usSM51, usMS;  

	struct snd_ac97 *pac97;
	
	pac97 = snd_kcontrol_chip(kcontrol); /* grab codec handle */

	/* grab our desired bits, then mash them together in a manner
	 * consistent with Table 6 on page 17 in the 1617a docs */
 
	usSM51 = snd_ac97_read(pac97, 0x7a) >> 14;
	usMS   = snd_ac97_read(pac97, 0x20) >> 8;
  
	ucontrol->value.enumerated.item[0] = (usSM51 << 1) + usMS;

	return 0;
}

static int snd_ac97_vt1617a_smart51_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	ushort usSM51, usMS, usReg;  

	struct snd_ac97 *pac97;

	pac97 = snd_kcontrol_chip(kcontrol); /* grab codec handle */

	usSM51 = ucontrol->value.enumerated.item[0] >> 1;
	usMS   = ucontrol->value.enumerated.item[0] &  1;

	/* push our values into the register - consider that things will be left
	 * in a funky state if the write fails */

	usReg = snd_ac97_read(pac97, 0x7a);
	snd_ac97_write_cache(pac97, 0x7a, (usReg & 0x3FFF) + (usSM51 << 14));
	usReg = snd_ac97_read(pac97, 0x20);
	snd_ac97_write_cache(pac97, 0x20, (usReg & 0xFEFF) + (usMS   <<  8));

	return 0;
}

static const struct snd_kcontrol_new snd_ac97_controls_vt1617a[] = {

	AC97_SINGLE("Center/LFE Exchange", 0x5a, 8, 1, 0),
	/*
	 * These are used to enable/disable surround sound on motherboards
	 * that have 3 bidirectional analog jacks
	 */
	{
		.iface         = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name          = "Smart 5.1 Select",
		.info          = snd_ac97_vt1617a_smart51_info,
		.get           = snd_ac97_vt1617a_smart51_get,
		.put           = snd_ac97_vt1617a_smart51_put,
	},
};

static int patch_vt1617a(struct snd_ac97 * ac97)
{
	int err = 0;
	int val;

	/* we choose to not fail out at this point, but we tell the
	   caller when we return */

	err = patch_build_controls(ac97, &snd_ac97_controls_vt1617a[0],
				   ARRAY_SIZE(snd_ac97_controls_vt1617a));

	/* bring analog power consumption to normal by turning off the
	 * headphone amplifier, like WinXP driver for EPIA SP
	 */
	/* We need to check the bit before writing it.
	 * On some (many?) hardwares, setting bit actually clears it!
	 */
	val = snd_ac97_read(ac97, 0x5c);
	if (!(val & 0x20))
		snd_ac97_write_cache(ac97, 0x5c, 0x20);

	ac97->ext_id |= AC97_EI_SPDIF;	/* force the detection of spdif */
	ac97->rates[AC97_RATES_SPDIF] = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000;
	ac97->build_ops = &patch_vt1616_ops;

	return err;
}

/* VIA VT1618 8 CHANNEL AC97 CODEC
 *
 * VIA implements 'Smart 5.1' completely differently on the 1618 than
 * it does on the 1617a. awesome! They seem to have sourced this
 * particular revision of the technology from somebody else, it's
 * called Universal Audio Jack and it shows up on some other folk's chips
 * as well.
 *
 * ordering in this list reflects vt1618 docs for Reg 60h and
 * the block diagram, DACs are as follows:
 *
 *        OUT_O -> Front,
 *	  OUT_1 -> Surround,
 *	  OUT_2 -> C/LFE
 *
 * Unlike the 1617a, each OUT has a consistent set of mappings
 * for all bitpatterns other than 00:
 *
 *        01       Unmixed Output
 *        10       Line In
 *        11       Mic  In
 *
 * Special Case of 00:
 *
 *        OUT_0    Mixed Output
 *        OUT_1    Reserved
 *        OUT_2    Reserved
 *
 * I have no idea what the hell Reserved does, but on an MSI
 * CN700T, i have to set it to get 5.1 output - YMMV, bad
 * shit may happen.
 *
 * If other chips use Universal Audio Jack, then this code might be applicable
 * to them.
 */

struct vt1618_uaj_item {
	unsigned short mask;
	unsigned short shift;
	const char *items[4];
};

/* This list reflects the vt1618 docs for Vendor Defined Register 0x60. */

static struct vt1618_uaj_item vt1618_uaj[3] = {
	{
		/* speaker jack */
		.mask  = 0x03,
		.shift = 0,
		.items = {
			"Speaker Out", "DAC Unmixed Out", "Line In", "Mic In"
		}
	},
	{
		/* line jack */
		.mask  = 0x0c,
		.shift = 2,
		.items = {
			"Surround Out", "DAC Unmixed Out", "Line In", "Mic In"
		}
	},
	{
		/* mic jack */
		.mask  = 0x30,
		.shift = 4,
		.items = {
			"Center LFE Out", "DAC Unmixed Out", "Line In", "Mic In"
		},
	},
};

static int snd_ac97_vt1618_UAJ_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	return ac97_enum_text_info(kcontrol, uinfo,
				   vt1618_uaj[kcontrol->private_value].items,
				   4);
}

/* All of the vt1618 Universal Audio Jack twiddlers are on
 * Vendor Defined Register 0x60, page 0. The bits, and thus
 * the mask, are the only thing that changes
 */
static int snd_ac97_vt1618_UAJ_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	unsigned short datpag, uaj;
	struct snd_ac97 *pac97 = snd_kcontrol_chip(kcontrol);

	mutex_lock(&pac97->page_mutex);

	datpag = snd_ac97_read(pac97, AC97_INT_PAGING) & AC97_PAGE_MASK;
	snd_ac97_update_bits(pac97, AC97_INT_PAGING, AC97_PAGE_MASK, 0);

	uaj = snd_ac97_read(pac97, 0x60) &
		vt1618_uaj[kcontrol->private_value].mask;

	snd_ac97_update_bits(pac97, AC97_INT_PAGING, AC97_PAGE_MASK, datpag);
	mutex_unlock(&pac97->page_mutex);

	ucontrol->value.enumerated.item[0] = uaj >>
		vt1618_uaj[kcontrol->private_value].shift;

	return 0;
}

static int snd_ac97_vt1618_UAJ_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	return ac97_update_bits_page(snd_kcontrol_chip(kcontrol), 0x60,
				     vt1618_uaj[kcontrol->private_value].mask,
				     ucontrol->value.enumerated.item[0]<<
				     vt1618_uaj[kcontrol->private_value].shift,
				     0);
}

/* config aux in jack - not found on 3 jack motherboards or soundcards */

static int snd_ac97_vt1618_aux_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	static const char *txt_aux[] = {"Aux In", "Back Surr Out"};

	return ac97_enum_text_info(kcontrol, uinfo, txt_aux, 2);
}

static int snd_ac97_vt1618_aux_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] =
		(snd_ac97_read(snd_kcontrol_chip(kcontrol), 0x5c) & 0x0008)>>3;
	return 0;
}

static int snd_ac97_vt1618_aux_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	/* toggle surround rear dac power */

	snd_ac97_update_bits(snd_kcontrol_chip(kcontrol), 0x5c, 0x0008,
			     ucontrol->value.enumerated.item[0] << 3);

	/* toggle aux in surround rear out jack */

	return snd_ac97_update_bits(snd_kcontrol_chip(kcontrol), 0x76, 0x0008,
				    ucontrol->value.enumerated.item[0] << 3);
}

static const struct snd_kcontrol_new snd_ac97_controls_vt1618[] = {
	AC97_SINGLE("Exchange Center/LFE", 0x5a,  8, 1,     0),
	AC97_SINGLE("DC Offset",           0x5a, 10, 1,     0),
	AC97_SINGLE("Soft Mute",           0x5c,  0, 1,     1),
	AC97_SINGLE("Headphone Amp",       0x5c,  5, 1,     1),
	AC97_DOUBLE("Back Surr Volume",    0x5e,  8, 0, 31, 1),
	AC97_SINGLE("Back Surr Switch",    0x5e, 15, 1,     1),
	{
		.iface         = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name          = "Speaker Jack Mode",
		.info          = snd_ac97_vt1618_UAJ_info,
		.get           = snd_ac97_vt1618_UAJ_get,
		.put           = snd_ac97_vt1618_UAJ_put,
		.private_value = 0
	},
	{
		.iface         = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name          = "Line Jack Mode",
		.info          = snd_ac97_vt1618_UAJ_info,
		.get           = snd_ac97_vt1618_UAJ_get,
		.put           = snd_ac97_vt1618_UAJ_put,
		.private_value = 1
	},
	{
		.iface         = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name          = "Mic Jack Mode",
		.info          = snd_ac97_vt1618_UAJ_info,
		.get           = snd_ac97_vt1618_UAJ_get,
		.put           = snd_ac97_vt1618_UAJ_put,
		.private_value = 2
	},
	{
		.iface         = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name          = "Aux Jack Mode",
		.info          = snd_ac97_vt1618_aux_info,
		.get           = snd_ac97_vt1618_aux_get,
		.put           = snd_ac97_vt1618_aux_put,
	}
};

static int patch_vt1618(struct snd_ac97 *ac97)
{
	return patch_build_controls(ac97, snd_ac97_controls_vt1618,
				    ARRAY_SIZE(snd_ac97_controls_vt1618));
}

/*
 */
static void it2646_update_jacks(struct snd_ac97 *ac97)
{
	/* shared Line-In / Surround Out */
	snd_ac97_update_bits(ac97, 0x76, 1 << 9,
			     is_shared_surrout(ac97) ? (1<<9) : 0);
	/* shared Mic / Center/LFE Out */
	snd_ac97_update_bits(ac97, 0x76, 1 << 10,
			     is_shared_clfeout(ac97) ? (1<<10) : 0);
}

static const struct snd_kcontrol_new snd_ac97_controls_it2646[] = {
	AC97_SURROUND_JACK_MODE_CTL,
	AC97_CHANNEL_MODE_CTL,
};

static const struct snd_kcontrol_new snd_ac97_spdif_controls_it2646[] = {
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), 0x76, 11, 1, 0),
	AC97_SINGLE("Analog to IEC958 Output", 0x76, 12, 1, 0),
	AC97_SINGLE("IEC958 Input Monitor", 0x76, 13, 1, 0),
};

static int patch_it2646_specific(struct snd_ac97 * ac97)
{
	int err;
	if ((err = patch_build_controls(ac97, snd_ac97_controls_it2646, ARRAY_SIZE(snd_ac97_controls_it2646))) < 0)
		return err;
	if ((err = patch_build_controls(ac97, snd_ac97_spdif_controls_it2646, ARRAY_SIZE(snd_ac97_spdif_controls_it2646))) < 0)
		return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_it2646_ops = {
	.build_specific	= patch_it2646_specific,
	.update_jacks = it2646_update_jacks
};

static int patch_it2646(struct snd_ac97 * ac97)
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

static const struct snd_kcontrol_new snd_ac97_controls_si3036[] = {
AC97_DOUBLE("Modem Speaker Volume", 0x5c, 14, 12, 3, 1)
};

static int patch_si3036_specific(struct snd_ac97 * ac97)
{
	int idx, err;
	for (idx = 0; idx < ARRAY_SIZE(snd_ac97_controls_si3036); idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ctl_new1(&snd_ac97_controls_si3036[idx], ac97))) < 0)
			return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_si3036_ops = {
	.build_specific	= patch_si3036_specific,
};

static int mpatch_si3036(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_si3036_ops;
	snd_ac97_write_cache(ac97, 0x5c, 0xf210 );
	snd_ac97_write_cache(ac97, 0x68, 0);
	return 0;
}

/*
 * LM 4550 Codec
 *
 * We use a static resolution table since LM4550 codec cannot be
 * properly autoprobed to determine the resolution via
 * check_volume_resolution().
 */

static struct snd_ac97_res_table lm4550_restbl[] = {
	{ AC97_MASTER, 0x1f1f },
	{ AC97_HEADPHONE, 0x1f1f },
	{ AC97_MASTER_MONO, 0x001f },
	{ AC97_PC_BEEP, 0x001f },	/* LSB is ignored */
	{ AC97_PHONE, 0x001f },
	{ AC97_MIC, 0x001f },
	{ AC97_LINE, 0x1f1f },
	{ AC97_CD, 0x1f1f },
	{ AC97_VIDEO, 0x1f1f },
	{ AC97_AUX, 0x1f1f },
	{ AC97_PCM, 0x1f1f },
	{ AC97_REC_GAIN, 0x0f0f },
	{ } /* terminator */
};

static int patch_lm4550(struct snd_ac97 *ac97)
{
	ac97->res_table = lm4550_restbl;
	return 0;
}

/* 
 *  UCB1400 codec (http://www.semiconductors.philips.com/acrobat_download/datasheets/UCB1400-02.pdf)
 */
static const struct snd_kcontrol_new snd_ac97_controls_ucb1400[] = {
/* enable/disable headphone driver which allows direct connection to
   stereo headphone without the use of external DC blocking
   capacitors */
AC97_SINGLE("Headphone Driver", 0x6a, 6, 1, 0),
/* Filter used to compensate the DC offset is added in the ADC to remove idle
   tones from the audio band. */
AC97_SINGLE("DC Filter", 0x6a, 4, 1, 0),
/* Control smart-low-power mode feature. Allows automatic power down
   of unused blocks in the ADC analog front end and the PLL. */
AC97_SINGLE("Smart Low Power Mode", 0x6c, 4, 3, 0),
};

static int patch_ucb1400_specific(struct snd_ac97 * ac97)
{
	int idx, err;
	for (idx = 0; idx < ARRAY_SIZE(snd_ac97_controls_ucb1400); idx++)
		if ((err = snd_ctl_add(ac97->bus->card, snd_ctl_new1(&snd_ac97_controls_ucb1400[idx], ac97))) < 0)
			return err;
	return 0;
}

static const struct snd_ac97_build_ops patch_ucb1400_ops = {
	.build_specific	= patch_ucb1400_specific,
};

static int patch_ucb1400(struct snd_ac97 * ac97)
{
	ac97->build_ops = &patch_ucb1400_ops;
	/* enable headphone driver and smart low power mode by default */
	snd_ac97_write_cache(ac97, 0x6a, 0x0050);
	snd_ac97_write_cache(ac97, 0x6c, 0x0030);
	return 0;
}
