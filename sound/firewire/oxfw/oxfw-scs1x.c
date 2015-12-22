/*
 * oxfw-scs1x.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

int snd_oxfw_scs1x_add(struct snd_oxfw *oxfw)
{
	struct snd_rawmidi *rmidi;
	int err;

	/* Use unique name for backward compatibility to scs1x module. */
	err = snd_rawmidi_new(oxfw->card, "SCS.1x", 0, 0, 0, &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", oxfw->card->shortname);

	return err;
}
