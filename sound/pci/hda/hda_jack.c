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

#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_jack.h"

/* execute pin sense measurement */
static u32 read_pin_sense(struct hda_codec *codec, hda_nid_t nid)
{
	u32 pincap;

	if (!codec->no_trigger_sense) {
		pincap = snd_hda_query_pin_caps(codec, nid);
		if (pincap & AC_PINCAP_TRIG_REQ) /* need trigger? */
			snd_hda_codec_read(codec, nid, 0,
					AC_VERB_SET_PIN_SENSE, 0);
	}
	return snd_hda_codec_read(codec, nid, 0,
				  AC_VERB_GET_PIN_SENSE, 0);
}

/**
 * snd_hda_jack_tbl_get - query the jack-table entry for the given NID
 */
struct hda_jack_tbl *
snd_hda_jack_tbl_get(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	if (!nid || !jack)
		return NULL;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid == nid)
			return jack;
	return NULL;
}
EXPORT_SYMBOL_HDA(snd_hda_jack_tbl_get);

/**
 * snd_hda_jack_tbl_new - create a jack-table entry for the given NID
 */
struct hda_jack_tbl *
snd_hda_jack_tbl_new(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_get(codec, nid);
	if (jack)
		return jack;
	snd_array_init(&codec->jacktbl, sizeof(*jack), 16);
	jack = snd_array_new(&codec->jacktbl);
	if (!jack)
		return NULL;
	jack->nid = nid;
	jack->jack_dirty = 1;
	return jack;
}

void snd_hda_jack_tbl_clear(struct hda_codec *codec)
{
	snd_array_free(&codec->jacktbl);
}

/* update the cached value and notification flag if needed */
static void jack_detect_update(struct hda_codec *codec,
			       struct hda_jack_tbl *jack)
{
	if (jack->jack_dirty) {
		jack->pin_sense = read_pin_sense(codec, jack->nid);
		jack->jack_dirty = 0;
	}
}

/**
 * snd_hda_set_dirty_all - Mark all the cached as dirty
 *
 * This function sets the dirty flag to all entries of jack table.
 * It's called from the resume path in hda_codec.c.
 */
void snd_hda_jack_set_dirty_all(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid)
			jack->jack_dirty = 1;
}
EXPORT_SYMBOL_HDA(snd_hda_jack_set_dirty_all);

/**
 * snd_hda_pin_sense - execute pin sense measurement
 * @codec: the CODEC to sense
 * @nid: the pin NID to sense
 *
 * Execute necessary pin sense measurement and return its Presence Detect,
 * Impedance, ELD Valid etc. status bits.
 */
u32 snd_hda_pin_sense(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_get(codec, nid);
	if (jack) {
		jack_detect_update(codec, jack);
		return jack->pin_sense;
	}
	return read_pin_sense(codec, nid);
}
EXPORT_SYMBOL_HDA(snd_hda_pin_sense);

/**
 * snd_hda_jack_detect - query pin Presence Detect status
 * @codec: the CODEC to sense
 * @nid: the pin NID to sense
 *
 * Query and return the pin's Presence Detect status.
 */
int snd_hda_jack_detect(struct hda_codec *codec, hda_nid_t nid)
{
	u32 sense = snd_hda_pin_sense(codec, nid);
	return !!(sense & AC_PINSENSE_PRESENCE);
}
EXPORT_SYMBOL_HDA(snd_hda_jack_detect);

/**
 * snd_hda_jack_detect_enable - enable the jack-detection
 */
int snd_hda_jack_detect_enable(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int tag)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_new(codec, nid);
	if (!jack)
		return -ENOMEM;
	return snd_hda_codec_write_cache(codec, nid, 0,
					 AC_VERB_SET_UNSOLICITED_ENABLE,
					 AC_USRSP_EN | tag);
}
EXPORT_SYMBOL_HDA(snd_hda_jack_detect_enable);
