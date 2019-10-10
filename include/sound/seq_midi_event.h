/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_SEQ_MIDI_EVENT_H
#define __SOUND_SEQ_MIDI_EVENT_H

/*
 *  MIDI byte <-> sequencer event coder
 *
 *  Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>,
 *                        Jaroslav Kysela <perex@perex.cz>
 */

#include <sound/asequencer.h>

#define MAX_MIDI_EVENT_BUF	256

/* midi status */
struct snd_midi_event {
	int qlen;		/* queue length */
	int read;		/* chars read */
	int type;		/* current event type */
	unsigned char lastcmd;	/* last command (for MIDI state handling) */
	unsigned char nostat;	/* no state flag */
	int bufsize;		/* allocated buffer size */
	unsigned char *buf;	/* input buffer */
	spinlock_t lock;
};

int snd_midi_event_new(int bufsize, struct snd_midi_event **rdev);
void snd_midi_event_free(struct snd_midi_event *dev);
void snd_midi_event_reset_encode(struct snd_midi_event *dev);
void snd_midi_event_reset_decode(struct snd_midi_event *dev);
void snd_midi_event_no_status(struct snd_midi_event *dev, int on);
bool snd_midi_event_encode_byte(struct snd_midi_event *dev, unsigned char c,
				struct snd_seq_event *ev);
/* decode from event to bytes - return number of written bytes if success */
long snd_midi_event_decode(struct snd_midi_event *dev, unsigned char *buf, long count,
			   struct snd_seq_event *ev);

#endif /* __SOUND_SEQ_MIDI_EVENT_H */
