/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Digital Beep Input Interface for HD-audio codec
 *
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 * Copyright (c) 2008 Embedded Alley Solutions Inc
 */

#ifndef __SOUND_HDA_BEEP_H
#define __SOUND_HDA_BEEP_H

#include <sound/hda_codec.h>

#define HDA_BEEP_MODE_OFF	0
#define HDA_BEEP_MODE_ON	1

/* beep information */
struct hda_beep {
	struct input_dev *dev;
	struct hda_codec *codec;
	char phys[32];
	int tone;
	hda_nid_t nid;
	unsigned int registered:1;
	unsigned int enabled:1;
	unsigned int linear_tone:1;	/* linear tone for IDT/STAC codec */
	unsigned int playing:1;
	struct work_struct beep_work; /* scheduled task for beep event */
	struct mutex mutex;
	void (*power_hook)(struct hda_beep *beep, bool on);
};

#ifdef CONFIG_SND_HDA_INPUT_BEEP
int snd_hda_enable_beep_device(struct hda_codec *codec, int enable);
int snd_hda_attach_beep_device(struct hda_codec *codec, int nid);
void snd_hda_detach_beep_device(struct hda_codec *codec);
#else
static inline int snd_hda_attach_beep_device(struct hda_codec *codec, int nid)
{
	return 0;
}
static inline void snd_hda_detach_beep_device(struct hda_codec *codec)
{
}
#endif
#endif
