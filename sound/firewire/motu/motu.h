/*
 * motu.h - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_MOTU_H_INCLUDED
#define SOUND_FIREWIRE_MOTU_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>

#include "../lib.h"

struct snd_motu {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	/* Model dependent information. */
	const struct snd_motu_spec *spec;
};

enum snd_motu_spec_flags {
	SND_MOTU_SPEC_SUPPORT_CLOCK_X2	= 0x0001,
	SND_MOTU_SPEC_SUPPORT_CLOCK_X4	= 0x0002,
	SND_MOTU_SPEC_TX_MICINST_CHUNK	= 0x0004,
	SND_MOTU_SPEC_TX_RETURN_CHUNK	= 0x0008,
	SND_MOTU_SPEC_TX_REVERB_CHUNK	= 0x0010,
	SND_MOTU_SPEC_TX_AESEBU_CHUNK	= 0x0020,
	SND_MOTU_SPEC_HAS_OPT_IFACE_A	= 0x0040,
	SND_MOTU_SPEC_HAS_OPT_IFACE_B	= 0x0080,
	SND_MOTU_SPEC_HAS_MIDI		= 0x0100,
};

struct snd_motu_spec {
	const char *const name;
	enum snd_motu_spec_flags flags;

	unsigned char analog_in_ports;
	unsigned char analog_out_ports;
};

#endif
