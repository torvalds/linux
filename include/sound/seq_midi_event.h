#ifndef __SOUND_SEQ_MIDI_EVENT_H
#define __SOUND_SEQ_MIDI_EVENT_H

/*
 *  MIDI byte <-> sequencer event coder
 *
 *  Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>,
 *                        Jaroslav Kysela <perex@suse.cz>
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
 */

#include "asequencer.h"

#define MAX_MIDI_EVENT_BUF	256

typedef struct snd_midi_event_t snd_midi_event_t;

/* midi status */
struct snd_midi_event_t {
	int qlen;		/* queue length */
	int read;		/* chars read */
	int type;		/* current event type */
	unsigned char lastcmd;	/* last command (for MIDI state handling) */
	unsigned char nostat;	/* no state flag */
	int bufsize;		/* allocated buffer size */
	unsigned char *buf;	/* input buffer */
	spinlock_t lock;
};

int snd_midi_event_new(int bufsize, snd_midi_event_t **rdev);
int snd_midi_event_resize_buffer(snd_midi_event_t *dev, int bufsize);
void snd_midi_event_free(snd_midi_event_t *dev);
void snd_midi_event_init(snd_midi_event_t *dev);
void snd_midi_event_reset_encode(snd_midi_event_t *dev);
void snd_midi_event_reset_decode(snd_midi_event_t *dev);
void snd_midi_event_no_status(snd_midi_event_t *dev, int on);
/* encode from byte stream - return number of written bytes if success */
long snd_midi_event_encode(snd_midi_event_t *dev, unsigned char *buf, long count, snd_seq_event_t *ev);
int snd_midi_event_encode_byte(snd_midi_event_t *dev, int c, snd_seq_event_t *ev);
/* decode from event to bytes - return number of written bytes if success */
long snd_midi_event_decode(snd_midi_event_t *dev, unsigned char *buf, long count, snd_seq_event_t *ev);

#endif /* __SOUND_SEQ_MIDI_EVENT_H */
