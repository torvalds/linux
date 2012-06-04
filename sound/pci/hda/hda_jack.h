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

struct auto_pin_cfg;

struct hda_jack_tbl {
	hda_nid_t nid;
	unsigned char action;		/* event action (0 = none) */
	unsigned char tag;		/* unsol event tag */
	unsigned int private_data;	/* arbitrary data */
	/* jack-detection stuff */
	unsigned int pin_sense;		/* cached pin-sense value */
	unsigned int jack_detect:1;	/* capable of jack-detection? */
	unsigned int jack_dirty:1;	/* needs to update? */
	unsigned int phantom_jack:1;    /* a fixed, always present port? */
	struct snd_kcontrol *kctl;	/* assigned kctl for jack-detection */
#ifdef CONFIG_SND_HDA_INPUT_JACK
	int type;
	struct snd_jack *jack;
#endif
};

struct hda_jack_tbl *
snd_hda_jack_tbl_get(struct hda_codec *codec, hda_nid_t nid);
struct hda_jack_tbl *
snd_hda_jack_tbl_get_from_tag(struct hda_codec *codec, unsigned char tag);

struct hda_jack_tbl *
snd_hda_jack_tbl_new(struct hda_codec *codec, hda_nid_t nid);
void snd_hda_jack_tbl_clear(struct hda_codec *codec);

/**
 * snd_hda_jack_get_action - get jack-tbl entry for the tag
 *
 * Call this from the unsol event handler to get the assigned action for the
 * event.  This will mark the dirty flag for the later reporting, too.
 */
static inline unsigned char
snd_hda_jack_get_action(struct hda_codec *codec, unsigned int tag)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_get_from_tag(codec, tag);
	if (jack) {
		jack->jack_dirty = 1;
		return jack->action;
	}
	return 0;
}

void snd_hda_jack_set_dirty_all(struct hda_codec *codec);

int snd_hda_jack_detect_enable(struct hda_codec *codec, hda_nid_t nid,
			       unsigned char action);

u32 snd_hda_pin_sense(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_jack_detect(struct hda_codec *codec, hda_nid_t nid);

bool is_jack_detectable(struct hda_codec *codec, hda_nid_t nid);

int snd_hda_jack_add_kctl(struct hda_codec *codec, hda_nid_t nid,
			  const char *name, int idx);
int snd_hda_jack_add_kctls(struct hda_codec *codec,
			   const struct auto_pin_cfg *cfg);

void snd_hda_jack_report_sync(struct hda_codec *codec);


#endif /* __SOUND_HDA_JACK_H */
