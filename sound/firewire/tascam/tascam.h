/*
 * tascam.h - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_TASCAM_H_INCLUDED
#define SOUND_TASCAM_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>

#include "../lib.h"

struct snd_tscm_spec {
	const char *const name;
	bool has_adat;
	bool has_spdif;
	unsigned int pcm_capture_analog_channels;
	unsigned int pcm_playback_analog_channels;
	unsigned int midi_capture_ports;
	unsigned int midi_playback_ports;
	bool is_controller;
};

struct snd_tscm {
	struct snd_card *card;
	struct fw_unit *unit;

	struct mutex mutex;

	const struct snd_tscm_spec *spec;
};

#define TSCM_ADDR_BASE			0xffff00000000ull

#define TSCM_OFFSET_FIRMWARE_REGISTER	0x0000
#define TSCM_OFFSET_FIRMWARE_FPGA	0x0004
#define TSCM_OFFSET_FIRMWARE_ARM	0x0008
#define TSCM_OFFSET_FIRMWARE_HW		0x000c

void snd_tscm_proc_init(struct snd_tscm *tscm);

#endif
