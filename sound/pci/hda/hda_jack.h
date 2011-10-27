/*
 * Jack-detection handling for HD-audio
 *
 * Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __SOUND_HDA_JACK_H
#define __SOUND_HDA_JACK_H

struct hda_jack_tbl {
	hda_nid_t nid;
	unsigned int pin_sense;		/* cached pin-sense value */
	unsigned int jack_cachable:1;	/* can be updated via unsol events */
	unsigned int jack_dirty:1;	/* needs to update? */
	unsigned int need_notify:1;	/* to be notified? */
	struct snd_kcontrol *kctl;	/* assigned kctl for jack-detection */
};

struct hda_jack_tbl *
snd_hda_jack_tbl_get(struct hda_codec *codec, hda_nid_t nid);

struct hda_jack_tbl *
snd_hda_jack_tbl_new(struct hda_codec *codec, hda_nid_t nid);
void snd_hda_jack_tbl_clear(struct hda_codec *codec);

/**
 * snd_hda_jack_set_dirty - set the dirty flag for the given jack-entry
 *
 * Call this function when a pin-state may change, e.g. when the hardware
 * notifies via an unsolicited event.
 */
static inline void snd_hda_jack_set_dirty(struct hda_codec *codec,
					  hda_nid_t nid)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_get(codec, nid);
	if (jack)
		jack->jack_dirty = 1;
}

void snd_hda_jack_set_dirty_all(struct hda_codec *codec);

int snd_hda_jack_detect_enable(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int tag);

u32 snd_hda_pin_sense(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_jack_detect(struct hda_codec *codec, hda_nid_t nid);

static inline bool is_jack_detectable(struct hda_codec *codec, hda_nid_t nid)
{
	if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_PRES_DETECT))
		return false;
	if (!codec->ignore_misc_bit &&
	    (get_defcfg_misc(snd_hda_codec_get_pincfg(codec, nid)) &
	     AC_DEFCFG_MISC_NO_PRESENCE))
		return false;
	if (!(get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP))
		return false;
	return true;
}

int snd_hda_jack_add_kctl(struct hda_codec *codec, hda_nid_t nid,
			  const char *name, int idx);
int snd_hda_jack_add_kctls(struct hda_codec *codec,
			   const struct auto_pin_cfg *cfg);

void snd_hda_jack_report(struct hda_codec *codec, hda_nid_t nid);
void snd_hda_jack_report_sync(struct hda_codec *codec);


#endif /* __SOUND_HDA_JACK_H */
