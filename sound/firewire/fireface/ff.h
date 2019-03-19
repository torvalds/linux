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

#define SND_FF_MAXIMIM_MIDI_QUADS	9
#define SND_FF_IN_MIDI_PORTS		2
#define SND_FF_OUT_MIDI_PORTS		2

enum snd_ff_stream_mode {
	SND_FF_STREAM_MODE_LOW = 0,
	SND_FF_STREAM_MODE_MID,
	SND_FF_STREAM_MODE_HIGH,
	SND_FF_STREAM_MODE_COUNT,
};

struct snd_ff_protocol;
struct snd_ff_spec {
	const char *const name;

	const unsigned int pcm_capture_channels[SND_FF_STREAM_MODE_COUNT];
	const unsigned int pcm_playback_channels[SND_FF_STREAM_MODE_COUNT];

	unsigned int midi_in_ports;
	unsigned int midi_out_ports;

	const struct snd_ff_protocol *protocol;
	u64 midi_high_addr;
	u8 midi_addr_range;
	u64 midi_rx_addrs[SND_FF_OUT_MIDI_PORTS];
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
	bool on_sysex[SND_FF_OUT_MIDI_PORTS];
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
	SND_FF_CLOCK_SRC_ADAT1,
	SND_FF_CLOCK_SRC_ADAT2,
	SND_FF_CLOCK_SRC_WORD,
	SND_FF_CLOCK_SRC_LTC,
	/* TODO: perhaps TCO exists. */
};

struct snd_ff_protocol {
	void (*handle_midi_msg)(struct snd_ff *ff, unsigned int offset,
				__le32 *buf, size_t length);
	int (*fill_midi_msg)(struct snd_ff *ff,
			     struct snd_rawmidi_substream *substream,
			     unsigned int port);
	int (*get_clock)(struct snd_ff *ff, unsigned int *rate,
			 enum snd_ff_clock_src *src);
	int (*switch_fetching_mode)(struct snd_ff *ff, bool enable);
	int (*begin_session)(struct snd_ff *ff, unsigned int rate);
	void (*finish_session)(struct snd_ff *ff);
	void (*dump_status)(struct snd_ff *ff, struct snd_info_buffer *buffer);
};

extern const struct snd_ff_protocol snd_ff_protocol_ff800;
extern const struct snd_ff_protocol snd_ff_protocol_ff400;
extern const struct snd_ff_protocol snd_ff_protocol_latter;

int snd_ff_transaction_register(struct snd_ff *ff);
int snd_ff_transaction_reregister(struct snd_ff *ff);
void snd_ff_transaction_unregister(struct snd_ff *ff);

int amdtp_ff_set_parameters(struct amdtp_stream *s, unsigned int rate,
			    unsigned int pcm_channels);
int amdtp_ff_add_pcm_hw_constraints(struct amdtp_stream *s,
				    struct snd_pcm_runtime *runtime);
int amdtp_ff_init(struct amdtp_stream *s, struct fw_unit *unit,
		  enum amdtp_stream_direction dir);

int snd_ff_stream_get_multiplier_mode(enum cip_sfc sfc,
				      enum snd_ff_stream_mode *mode);
int snd_ff_stream_init_duplex(struct snd_ff *ff);
void snd_ff_stream_destroy_duplex(struct snd_ff *ff);
int snd_ff_stream_start_duplex(struct snd_ff *ff, unsigned int rate);
void snd_ff_stream_stop_duplex(struct snd_ff *ff);
void snd_ff_stream_update_duplex(struct snd_ff *ff);

void snd_ff_stream_lock_changed(struct snd_ff *ff);
int snd_ff_stream_lock_try(struct snd_ff *ff);
void snd_ff_stream_lock_release(struct snd_ff *ff);

void snd_ff_proc_init(struct snd_ff *ff);
const char *snd_ff_proc_get_clk_label(enum snd_ff_clock_src src);

int snd_ff_create_midi_devices(struct snd_ff *ff);

int snd_ff_create_pcm_devices(struct snd_ff *ff);

int snd_ff_create_hwdep_devices(struct snd_ff *ff);

#endif
