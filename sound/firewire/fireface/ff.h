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
#include <linux/sched/signal.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/hwdep.h>
#include <sound/firewire.h>

#include "../lib.h"
#include "../amdtp-stream.h"
#include "../iso-resources.h"

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

	const struct snd_ff_protocol *protocol;
};

struct snd_ff {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;
	spinlock_t lock;

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

	unsigned int substreams_counter;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	struct fw_iso_resources tx_resources;
	struct fw_iso_resources rx_resources;

	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;
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

extern const struct snd_ff_protocol snd_ff_protocol_ff400;

int snd_ff_transaction_register(struct snd_ff *ff);
int snd_ff_transaction_reregister(struct snd_ff *ff);
void snd_ff_transaction_unregister(struct snd_ff *ff);

int amdtp_ff_set_parameters(struct amdtp_stream *s, unsigned int rate,
			    unsigned int pcm_channels);
int amdtp_ff_add_pcm_hw_constraints(struct amdtp_stream *s,
				    struct snd_pcm_runtime *runtime);
int amdtp_ff_init(struct amdtp_stream *s, struct fw_unit *unit,
		  enum amdtp_stream_direction dir);

int snd_ff_stream_init_duplex(struct snd_ff *ff);
void snd_ff_stream_destroy_duplex(struct snd_ff *ff);
int snd_ff_stream_start_duplex(struct snd_ff *ff, unsigned int rate);
void snd_ff_stream_stop_duplex(struct snd_ff *ff);
void snd_ff_stream_update_duplex(struct snd_ff *ff);

void snd_ff_stream_lock_changed(struct snd_ff *ff);
int snd_ff_stream_lock_try(struct snd_ff *ff);
void snd_ff_stream_lock_release(struct snd_ff *ff);

void snd_ff_proc_init(struct snd_ff *ff);

int snd_ff_create_midi_devices(struct snd_ff *ff);

int snd_ff_create_pcm_devices(struct snd_ff *ff);

int snd_ff_create_hwdep_devices(struct snd_ff *ff);

#endif
