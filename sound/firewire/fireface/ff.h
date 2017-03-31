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
#include <sound/rawmidi.h>

#include "../lib.h"

#define SND_FF_STREAM_MODES		3

#define SND_FF_MAXIMIM_MIDI_QUADS	9
#define SND_FF_IN_MIDI_PORTS		2
#define SND_FF_OUT_MIDI_PORTS		2

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

	/* To handle MIDI tx. */
	struct snd_rawmidi_substream *tx_midi_substreams[SND_FF_IN_MIDI_PORTS];
	struct fw_address_handler async_handler;

	/* TO handle MIDI rx. */
	struct snd_rawmidi_substream *rx_midi_substreams[SND_FF_OUT_MIDI_PORTS];
	u8 running_status[SND_FF_OUT_MIDI_PORTS];
	__le32 msg_buf[SND_FF_OUT_MIDI_PORTS][SND_FF_MAXIMIM_MIDI_QUADS];
	struct work_struct rx_midi_work[SND_FF_OUT_MIDI_PORTS];
	struct fw_transaction transactions[SND_FF_OUT_MIDI_PORTS];
	ktime_t next_ktime[SND_FF_OUT_MIDI_PORTS];
	bool rx_midi_error[SND_FF_OUT_MIDI_PORTS];
	unsigned int rx_bytes[SND_FF_OUT_MIDI_PORTS];
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

int snd_ff_transaction_register(struct snd_ff *ff);
int snd_ff_transaction_reregister(struct snd_ff *ff);
void snd_ff_transaction_unregister(struct snd_ff *ff);

#endif
