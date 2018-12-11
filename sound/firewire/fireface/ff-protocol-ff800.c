/*
 * ff-protocol-ff800.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

static void ff800_handle_midi_msg(struct snd_ff *ff, __le32 *buf, size_t length)
{
	int i;

	for (i = 0; i < length / 4; i++) {
		u8 byte = le32_to_cpu(buf[i]) & 0xff;
		struct snd_rawmidi_substream *substream;

		substream = READ_ONCE(ff->tx_midi_substreams[0]);
		if (substream)
			snd_rawmidi_receive(substream, &byte, 1);
	}
}

const struct snd_ff_protocol snd_ff_protocol_ff800 = {
	.handle_midi_msg	= ff800_handle_midi_msg,
};
