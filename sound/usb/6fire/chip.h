/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Copyright:	(C) Torsten Schenk
 */
#ifndef USB6FIRE_CHIP_H
#define USB6FIRE_CHIP_H

#include "common.h"

struct sfire_chip {
	struct usb_device *dev;
	struct snd_card *card;
	int intf_count; /* number of registered interfaces */
	int regidx; /* index in module parameter arrays */
	bool shutdown;

	struct midi_runtime *midi;
	struct pcm_runtime *pcm;
	struct control_runtime *control;
	struct comm_runtime *comm;
};
#endif /* USB6FIRE_CHIP_H */

