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
#include <sound/info.h>

#include "../lib.h"

#define SND_FF_STREAM_MODES		3

struct snd_ff_protocol;
struct snd_ff_spec {
	const char *const name;

	const unsigned int pcm_capture_channels[SND_FF_STREAM_MODES];
	const unsigned int pcm_playback_channels[SND_FF_STREAM_MODES];

	unsigned int midi_in_ports;
	unsigned int midi_out_ports;

	struct snd_ff_protocol *protocol;
};

struct snd_ff {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	const struct snd_ff_spec *spec;
};

enum snd_ff_clock_src {
	SND_FF_CLOCK_SRC_INTERNAL,
	SND_FF_CLOCK_SRC_SPDIF,
	SND_FF_CLOCK_SRC_ADAT,
	SND_FF_CLOCK_SRC_WORD,
	SND_FF_CLOCK_SRC_LTC,
	/* TODO: perhaps ADAT2 and TCO exists. */
};

struct snd_ff_protocol {
	int (*get_clock)(struct snd_ff *ff, unsigned int *rate,
			 enum snd_ff_clock_src *src);
	int (*begin_session)(struct snd_ff *ff, unsigned int rate);
	void (*finish_session)(struct snd_ff *ff);
	int (*switch_fetching_mode)(struct snd_ff *ff, bool enable);

	void (*dump_sync_status)(struct snd_ff *ff,
				 struct snd_info_buffer *buffer);
	void (*dump_clock_config)(struct snd_ff *ff,
				  struct snd_info_buffer *buffer);

	u64 midi_high_addr_reg;
	u64 midi_rx_port_0_reg;
	u64 midi_rx_port_1_reg;
};

#endif
