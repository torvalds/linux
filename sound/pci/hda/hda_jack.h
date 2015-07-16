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

#include <linux/err.h>

struct auto_pin_cfg;
struct hda_jack_tbl;
struct hda_jack_callback;

typedef void (*hda_jack_callback_fn) (struct hda_codec *, struct hda_jack_callback *);

struct hda_jack_callback {
	struct hda_jack_tbl *tbl;
	hda_jack_callback_fn func;
	unsigned int private_data;	/* arbitrary data */
	struct hda_jack_callback *next;
};

struct hda_jack_tbl {
	hda_nid_t nid;
	unsigned char tag;		/* unsol event tag */
	struct hda_jack_callback *callback;
	/* jack-detection stuff */
	unsigned int pin_sense;		/* cached pin-sense value */
	unsigned int jack_detect:1;	/* capable of jack-detection? */
	unsigned int jack_dirty:1;	/* needs to update? */
	unsigned int phantom_jack:1;    /* a fixed, always present port? */
	unsigned int block_report:1;    /* in a transitional state - do not report to userspace */
	hda_nid_t gating_jack;		/* valid when gating jack plugged */
	hda_nid_t gated_jack;		/* gated is dependent on this jack */
	int type;
	struct snd_jack *jack;
};

struct hda_jack_tbl *
snd_hda_jack_tbl_get(struct hda_codec *codec, hda_nid_t nid);
struct hda_jack_tbl *
snd_hda_jack_tbl_get_from_tag(struct hda_codec *codec, unsigned char tag);

void snd_hda_jack_tbl_clear(struct hda_codec *codec);

void snd_hda_jack_set_dirty_all(struct hda_codec *codec);

int snd_hda_jack_detect_enable(struct hda_codec *codec, hda_nid_t nid);
struct hda_jack_callback *
snd_hda_jack_detect_enable_callback(struct hda_codec *codec, hda_nid_t nid,
				    hda_jack_callback_fn cb);

int snd_hda_jack_set_gating_jack(struct hda_codec *codec, hda_nid_t gated_nid,
				 hda_nid_t gating_nid);

u32 snd_hda_pin_sense(struct hda_codec *codec, hda_nid_t nid);

/* the jack state returned from snd_hda_jack_detect_state() */
enum {
	HDA_JACK_NOT_PRESENT, HDA_JACK_PRESENT, HDA_JACK_PHANTOM,
};

int snd_hda_jack_detect_state(struct hda_codec *codec, hda_nid_t nid);

/**
 * snd_hda_jack_detect - Detect the jack
 * @codec: the HDA codec
 * @nid: pin NID to check jack detection
 */
static inline bool snd_hda_jack_detect(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_jack_detect_state(codec, nid) != HDA_JACK_NOT_PRESENT;
}

bool is_jack_detectable(struct hda_codec *codec, hda_nid_t nid);

int snd_hda_jack_add_kctl(struct hda_codec *codec, hda_nid_t nid,
			  const char *name);
int snd_hda_jack_add_kctls(struct hda_codec *codec,
			   const struct auto_pin_cfg *cfg);

void snd_hda_jack_report_sync(struct hda_codec *codec);

void snd_hda_jack_unsol_event(struct hda_codec *codec, unsigned int res);

void snd_hda_jack_poll_all(struct hda_codec *codec);

#endif /* __SOUND_HDA_JACK_H */
