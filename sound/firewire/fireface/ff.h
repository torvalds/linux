/*
 * ff.h - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREFACE_H_INCLUDED
#define SOUND_FIREFACE_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>

#include <sound/core.h>

#include "../lib.h"

#define SND_FF_STREAM_MODES		3

struct snd_ff_spec {
	const char *const name;

	const unsigned int pcm_capture_channels[SND_FF_STREAM_MODES];
	const unsigned int pcm_playback_channels[SND_FF_STREAM_MODES];

	unsigned int midi_in_ports;
	unsigned int midi_out_ports;
};

struct snd_ff {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	const struct snd_ff_spec *spec;
};
#endif
