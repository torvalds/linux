/*
 * digi00x.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_DIGI00X_H_INCLUDED
#define SOUND_DIGI00X_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../lib.h"
#include "../iso-resources.h"
#include "../amdtp-stream.h"

struct snd_dg00x {
	struct snd_card *card;
	struct fw_unit *unit;

	struct mutex mutex;
};

int amdtp_dot_init(struct amdtp_stream *s, struct fw_unit *unit,
		   enum amdtp_stream_direction dir);
int amdtp_dot_set_parameters(struct amdtp_stream *s, unsigned int rate,
			     unsigned int pcm_channels,
			     unsigned int midi_ports);
void amdtp_dot_reset(struct amdtp_stream *s);
int amdtp_dot_add_pcm_hw_constraints(struct amdtp_stream *s,
				     struct snd_pcm_runtime *runtime);
void amdtp_dot_set_pcm_format(struct amdtp_stream *s, snd_pcm_format_t format);

#endif
