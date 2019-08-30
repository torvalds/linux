/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 */

#ifndef MIDI_H
#define MIDI_H

#include <sound/rawmidi.h>

#include "midibuf.h"

#define MIDI_BUFFER_SIZE 1024

struct snd_line6_midi {
	/* Pointer back to the Line 6 driver data structure */
	struct usb_line6 *line6;

	/* MIDI substream for receiving (or NULL if not active) */
	struct snd_rawmidi_substream *substream_receive;

	/* MIDI substream for transmitting (or NULL if not active) */
	struct snd_rawmidi_substream *substream_transmit;

	/* Number of currently active MIDI send URBs */
	int num_active_send_urbs;

	/* Spin lock to protect MIDI buffer handling */
	spinlock_t lock;

	/* Wait queue for MIDI transmission */
	wait_queue_head_t send_wait;

	/* Buffer for incoming MIDI stream */
	struct midi_buffer midibuf_in;

	/* Buffer for outgoing MIDI stream */
	struct midi_buffer midibuf_out;
};

extern int line6_init_midi(struct usb_line6 *line6);
extern void line6_midi_receive(struct usb_line6 *line6, unsigned char *data,
			       int length);

#endif
